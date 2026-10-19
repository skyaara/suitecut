// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { gzipSync } from 'node:zlib'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage, NativeAPIRequestContext } from '../dist/native.js'

const directory = resolve('.suitecut/native-http')
await mkdir(directory, { recursive: true })
await rm(resolve(directory, 'report.json'), { force: true })
const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const binaryHash = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const started = new Date().toISOString(),
  checks = [],
  failures = [],
  diagnostics = []
let diagnosticBytes = 0
let observePendingRequest = () => undefined
const server = createServer((request, response) => {
  void (async () => {
    const chunks = []
    for await (const chunk of request) {
      if (!Buffer.isBuffer(chunk)) throw new Error('Expected binary HTTP test body')
      chunks.push(chunk)
    }
    const body = Buffer.concat(chunks),
      url = new URL(request.url ?? '/', 'http://native.test')
    if (url.pathname === '/page') {
      response.setHeader('Content-Type', 'text/html')
      response.end('<!doctype html><body>native HTTP context</body>')
    } else if (url.pathname === '/redirect') {
      response.writeHead(302, { Location: '/echo?redirect=complete' })
      response.end('redirecting')
    } else if (url.pathname === '/loop') {
      response.writeHead(302, { Location: '/loop' })
      response.end()
    } else if (url.pathname === '/error') {
      response.writeHead(500, { 'Content-Type': 'text/plain' })
      response.end('actual HTTP failure body')
    } else if (url.pathname === '/cookies') {
      response.setHeader('Set-Cookie', ['native_a=1; Path=/', 'native_b=2; Path=/'])
      response.end('cookies saved')
    } else if (url.pathname === '/unusual-headers') {
      const socket = response.socket
      assert.ok(socket)
      socket.end(
        Buffer.from(
          'HTTP/1.1 200 OK\r\nconstructor: actual constructor header\r\n__proto__: first actual value\r\n__proto__: second actual value\r\nX-Latin1: caf\u00e9\r\nContent-Length: 0\r\nConnection: close\r\n\r\n',
          'latin1',
        ),
      )
    } else if (url.pathname === '/gzip') {
      response.setHeader('Content-Type', 'application/json')
      response.setHeader('Content-Encoding', 'gzip')
      response.end(gzipSync(Buffer.from('{"nativeCompression":true}')))
    } else if (
      url.pathname === '/auth' &&
      request.headers.authorization !== `Basic ${Buffer.from('native:password').toString('base64')}`
    ) {
      response.writeHead(401, {
        'WWW-Authenticate': 'Basic realm="native"',
        'Content-Type': 'text/plain',
      })
      response.end('auth required')
    } else if (url.pathname === '/delayed') {
      observePendingRequest()
      observePendingRequest = () => undefined
      /* Leave the actual transport pending until its native deadline cancels it. */
    } else {
      response.setHeader('Content-Type', 'application/json')
      response.end(
        JSON.stringify({
          method: request.method,
          url: request.url,
          headers: request.headers,
          body: body.toString('hex'),
        }),
      )
    }
  })().catch((error) =>
    response.destroy(error instanceof Error ? error : new Error('Native HTTP test server failure')),
  )
})
await new Promise((resolveListen, rejectListen) => {
  server.once('error', rejectListen)
  server.listen(0, '127.0.0.1', resolveListen)
})
const port = z.object({ port: z.number().int() }).parse(server.address()).port,
  origin = `http://127.0.0.1:${port}`
const Echo = z
  .object({
    method: z.string(),
    url: z.string(),
    headers: z.record(z.string(), z.string()),
    body: z.string(),
  })
  .superRefine((echo, context) => {
    if (echo.headers['x-suitecut-native-api'] !== undefined)
      context.addIssue({ code: 'custom', message: 'Native private request token leaked to origin' })
  })
let browser, independent, authenticated, finalState
try {
  browser = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    framesPerSecond: 30,
    headless: true,
    onDiagnostic: (text) => {
      if (diagnosticBytes < 256 * 1024) {
        diagnostics.push(text)
        diagnosticBytes += Buffer.byteLength(text)
      }
    },
  })
  const page = createNativePage(browser)
  await page.goto(`${origin}/page`)
  await page.evaluate(() => {
    document.cookie = 'native_page=shared; path=/'
  })
  const cookies = await page.request.get(`${origin}/echo`)
  assert.match(Echo.parse(await cookies.json()).headers.cookie, /native_page=shared/u)
  await cookies.dispose()
  checks.push('page-associated request shares genuine CEF cookie jar')

  const created = await page.request.post(`${origin}/echo?existing=one`, {
    params: { native: 'query value' },
    data: { native: true },
    headers: { 'X-Native-API': 'header-value' },
  })
  const createdData = Echo.parse(await created.json())
  assert.equal(createdData.method, 'POST')
  assert.equal(createdData.url, '/echo?existing=one&native=query+value')
  assert.equal(createdData.headers['content-type'], 'application/json')
  assert.equal(createdData.headers['x-native-api'], 'header-value')
  assert.equal(Buffer.from(createdData.body, 'hex').toString(), '{"native":true}')
  await created.dispose()
  checks.push('native POST/query/headers/JSON transport')
  const empty = await page.request.post(`${origin}/echo`, { data: '' })
  assert.equal(Echo.parse(await empty.json()).body, '')
  await empty.dispose()
  const framed = await page.request.post(`${origin}/echo`, {
    headers: { 'Content-Length': '9999' },
    data: Buffer.from([0, 255, 13, 10]),
  })
  const actualFraming = Echo.parse(await framed.json())
  assert.equal(actualFraming.headers['content-length'], '4')
  assert.equal(actualFraming.body, '00ff0d0a')
  await framed.dispose()
  checks.push('native empty request body and actual byte framing replace stale Content-Length')

  for (const method of ['GET', 'PUT', 'PATCH', 'DELETE']) {
    const response = await page.request.fetch(`${origin}/echo`, { method })
    assert.equal(Echo.parse(await response.json()).method, method)
    await response.dispose()
  }
  const head = await page.request.head(`${origin}/echo`)
  assert.equal((await head.body()).length, 0)
  await head.dispose()
  checks.push('GET/HEAD/PUT/PATCH/DELETE methods')

  const form = await page.request.post(`${origin}/echo`, {
    form: { space: 'two words', number: 42 },
  })
  const formData = Echo.parse(await form.json())
  assert.equal(formData.headers['content-type'], 'application/x-www-form-urlencoded')
  assert.equal(Buffer.from(formData.body, 'hex').toString(), 'space=two+words&number=42')
  await form.dispose()
  const binary = Buffer.alloc(100_003)
  for (let i = 0; i < binary.length; i++) binary[i] = i % 256
  const uploaded = await page.request.put(`${origin}/echo`, { data: binary })
  assert.equal(Echo.parse(await uploaded.json()).body, binary.toString('hex'))
  await uploaded.dispose()
  checks.push('form encoding and exact 100003-byte binary chunked upload')

  const multipart = await page.request.post(`${origin}/echo`, {
    multipart: {
      field: 'native text',
      file: {
        name: 'native.bin',
        mimeType: 'application/octet-stream',
        buffer: Buffer.from([0, 255, 13, 10]),
      },
    },
  })
  const multipartData = Echo.parse(await multipart.json()),
    multipartBytes = Buffer.from(multipartData.body, 'hex')
  assert.match(multipartData.headers['content-type'], /^multipart\/form-data; boundary=suitecut-/u)
  assert.equal(multipartBytes.includes(Buffer.from('filename="native.bin"')), true)
  assert.equal(multipartBytes.includes(Buffer.from([0, 255, 13, 10])), true)
  await multipart.dispose()
  checks.push('multipart field/file framing with exact binary payload')

  const redirected = await page.request.get(`${origin}/redirect`)
  assert.equal(redirected.url(), `${origin}/echo?redirect=complete`)
  assert.equal(Echo.parse(await redirected.json()).url, '/echo?redirect=complete')
  await redirected.dispose()
  const stopped = await page.request.get(`${origin}/redirect`, { maxRedirects: 0 })
  assert.equal(stopped.status(), 302)
  await stopped.dispose()
  await assert.rejects(page.request.get(`${origin}/loop`, { maxRedirects: 2 }), /redirect count/u)
  checks.push('native redirect following, no-follow and limit failure')

  const error = await page.request.get(`${origin}/error`)
  assert.equal(error.status(), 500)
  assert.equal(error.ok(), false)
  assert.equal(await error.text(), 'actual HTTP failure body')
  await error.dispose()
  await assert.rejects(
    page.request.get(`${origin}/error`, { failOnStatusCode: true }),
    /HTTP status 500/u,
  )
  const compressed = await page.request.get(`${origin}/gzip`)
  assert.deepEqual(await compressed.json(), { nativeCompression: true })
  await compressed.dispose()
  checks.push('ordinary HTTP errors, explicit failure option and decoded gzip body')

  const unusual = await page.request.get(`${origin}/unusual-headers`)
  const unusualHeaders = unusual.headers()
  assert.equal(Object.getPrototypeOf(unusualHeaders), Object.prototype)
  assert.equal(Object.hasOwn(unusualHeaders, 'constructor'), true)
  assert.equal(Object.hasOwn(unusualHeaders, '__proto__'), true)
  assert.equal(unusualHeaders.constructor, 'actual constructor header')
  assert.equal(unusualHeaders.__proto__, 'first actual value, second actual value')
  assert.equal(unusualHeaders['x-latin1'], 'caf\u00e9')
  assert.equal(unusual.headersArray().filter((field) => field.name === '__proto__').length, 2)
  await unusual.dispose()
  checks.push('actual constructor/__proto__ response fields and Latin1 header bytes')

  const repeated = await page.request.get(`${origin}/cookies`)
  try {
    assert.equal(
      repeated.headersArray().filter((header) => header.name.toLowerCase() === 'set-cookie').length,
      2,
      'The actual HTTP response must preserve both Set-Cookie fields',
    )
    checks.push('actual repeated Set-Cookie response fields')
  } catch (error) {
    failures.push(error instanceof Error ? error : new Error('Unknown response header failure'))
  }
  await repeated.dispose()
  const shared = await page.request.get(`${origin}/echo`)
  assert.match(Echo.parse(await shared.json()).headers.cookie, /native_a=1/u)
  await shared.dispose()
  independent = await NativeAPIRequestContext.create(browser, { baseURL: origin })
  const isolated = await independent.get('/echo')
  assert.equal(Echo.parse(await isolated.json()).headers.cookie, undefined)
  await isolated.dispose()
  checks.push('saved cookies and isolated API request context')
  const independentCookies = await independent.get('/cookies')
  await independentCookies.dispose()
  const storage = await independent.storageState()
  assert.deepEqual(storage.origins, [])
  assert.equal(
    storage.cookies.filter((cookie) => cookie.name === 'native_a' || cookie.name === 'native_b')
      .length,
    2,
  )
  assert.ok(
    storage.cookies.every(
      (cookie) => cookie.domain === '127.0.0.1' && cookie.path === '/' && cookie.expires === -1,
    ),
  )
  checks.push('independent native API state exports actual CEF cookie profile')

  const unauthorized = await independent.get('/auth')
  try {
    assert.equal(unauthorized.status(), 401)
    assert.equal(
      await unauthorized.text(),
      'auth required',
      'An ordinary 401 response must preserve its actual body',
    )
    checks.push('unauthenticated HTTP challenge preserves status and actual response body')
  } catch (error) {
    failures.push(error instanceof Error ? error : new Error('Unknown HTTP challenge body failure'))
  }
  await unauthorized.dispose()

  const getBody = await independent.get('/echo', { data: 'native GET body' })
  try {
    const echoed = Echo.parse(await getBody.json())
    assert.equal(echoed.method, 'GET', 'Native API GET data must keep the requested GET method')
    assert.equal(Buffer.from(echoed.body, 'hex').toString(), 'native GET body')
    checks.push('GET method with exact actual outgoing request body')
  } catch (error) {
    failures.push(error instanceof Error ? error : new Error('Unknown native GET body failure'))
  }
  await getBody.dispose()

  authenticated = await NativeAPIRequestContext.create(browser, {
    httpCredentials: { username: 'native', password: 'password', origin },
  })
  const auth = await authenticated.get(`${origin}/auth`)
  assert.equal(auth.status(), 200)
  assert.match(Echo.parse(await auth.json()).headers.authorization, /^Basic /u)
  await auth.dispose()
  checks.push('native HTTP basic authentication challenge')

  const scopedContext = z
    .object({ contextId: z.number().int().positive() })
    .parse(await browser.runContext('create')).contextId
  const scoped = new NativeAPIRequestContext(browser, {}, 'context', scopedContext)
  const retained = await scoped.get(`${origin}/echo`)
  await retained.body()
  const arrived = new Promise((resolvePending) => {
    observePendingRequest = () => resolvePending(undefined)
  })
  const pendingScoped = scoped.get(`${origin}/delayed`)
  const pendingScopedRejected = assert.rejects(pendingScoped, /disposed|closed/u)
  await arrived
  await browser.runContext('close', { contextId: scopedContext })
  await pendingScopedRejected
  await assert.rejects(retained.body(), /disposed/u)
  await assert.rejects(scoped.get(`${origin}/echo`), /disposed/u)
  checks.push(
    'browser-context closure cancels actual pending HTTP transport and invalidates cached SDK bodies',
  )

  await assert.rejects(independent.get('/delayed', { timeout: 50 }), /timed out/u)
  await independent.dispose()
  independent = undefined
  await authenticated.dispose()
  authenticated = undefined
  await page.request.dispose()
  const state = z
    .object({
      httpTransfers: z.number(),
      httpCookieCallbacks: z.number(),
      httpCookieVisits: z.number(),
      httpContexts: z.number(),
      httpActive: z.number(),
      httpResponses: z.number(),
      httpUploads: z.number(),
      httpRetainedBodyBytes: z.number(),
      httpActiveBodyBytes: z.number(),
      httpUploadBytes: z.number(),
      httpActiveUploadBytes: z.number(),
    })
    .passthrough()
    .parse(await browser.runAutomation('diagnostics'))
  finalState = state
  for (const name of [
    'httpTransfers',
    'httpCookieCallbacks',
    'httpCookieVisits',
    'httpContexts',
    'httpActive',
    'httpResponses',
    'httpUploads',
    'httpRetainedBodyBytes',
    'httpActiveBodyBytes',
    'httpUploadBytes',
    'httpActiveUploadBytes',
  ])
    assert.equal(state[name], 0, name)
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binaryHash,
  )
  checks.push('native deadline cancellation and complete request/response/upload cleanup')
  if (failures.length) throw new AggregateError(failures, 'Native HTTP feature verification failed')
  const report = {
    passed: true,
    started,
    completed: new Date().toISOString(),
    binaryHash,
    checks,
    diagnostics: state,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} catch (error) {
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify(
      {
        passed: false,
        started,
        completed: new Date().toISOString(),
        binaryHash,
        checks,
        failures: failures.map((failure) => failure.stack),
        diagnostics: finalState,
        error: error instanceof Error ? error.stack : 'Unknown native HTTP verification failure',
      },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  await independent?.dispose()
  await authenticated?.dispose()
  await browser?.close()
  server.closeAllConnections()
  await new Promise((resolveClose) => server.close(resolveClose))
  await writeFile(resolve(directory, 'native-stderr.log'), diagnostics.join(''))
}
