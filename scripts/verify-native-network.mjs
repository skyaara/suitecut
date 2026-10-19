// SPDX-License-Identifier: MIT
/* global document, fetch */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage, NativeResponse } from '../dist/native.js'

const directory = resolve('.suitecut/native-network')
await mkdir(directory, { recursive: true })
await rm(resolve(directory, 'report.json'), { force: true })
const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned CEF native engine')
const started = new Date().toISOString()
const binaryHash = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const observed = z
  .array(
    z.object({
      url: z.string(),
      method: z.string(),
      body: z.string(),
      headers: z.record(z.string(), z.union([z.string(), z.array(z.string())]).optional()),
    }),
  )
  .parse([])
const server = createServer((request, response) => {
  void (async () => {
    const chunks = []
    for await (const chunk of request) {
      if (!Buffer.isBuffer(chunk)) throw new Error('Expected binary native test HTTP upload')
      chunks.push(chunk)
    }
    const body = Buffer.concat(chunks)
    observed.push({
      url: request.url ?? '',
      method: request.method ?? '',
      headers: request.headers,
      body: body.toString('hex'),
    })
    response.setHeader('Access-Control-Allow-Origin', '*')
    if (request.url === '/') {
      response.setHeader('Content-Type', 'text/html')
      response.end(
        '<!doctype html><title>Native network controls</title><body>native network</body>',
      )
    } else if (request.url === '/redirect') {
      response.writeHead(302, { Location: '/final' })
      response.end()
    } else if (request.url === '/final') {
      response.end('redirect complete')
    } else if (request.url === '/echo') {
      response.setHeader('Content-Type', 'application/json')
      response.end(
        JSON.stringify({
          method: request.method,
          headers: request.headers,
          body: body.toString('hex'),
        }),
      )
    } else if (request.url === '/child') {
      response.setHeader('Content-Type', 'text/html')
      response.end('<!doctype html><body>child frame</body>')
    } else {
      response.end(`native ${request.url}`)
    }
  })().catch((error) =>
    response.destroy(
      error instanceof Error ? error : new Error('Native network test server failed'),
    ),
  )
})
await new Promise((resolveListen, rejectListen) => {
  server.once('error', rejectListen)
  server.listen(0, '0.0.0.0', resolveListen)
})
const port = z.object({ port: z.number().int() }).parse(server.address()).port
const origin = `http://127.0.0.1:${port}`
let browser
let diagnosticBytes = 0
const diagnostics = []
const checks = []
try {
  browser = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 240,
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
  const requests = [],
    responses = [],
    finished = [],
    failed = []
  page.on('request', (request) => requests.push(request))
  page.on('response', (response) => responses.push(response))
  page.on('requestfinished', (request) => finished.push(request))
  page.on('requestfailed', (request) => failed.push(request))
  const navigation = await page.goto(origin)
  assert.equal(navigation.status(), 200)
  assert.equal(navigation.request().isNavigationRequest(), true)
  assert.equal(navigation.request().frame(), page.mainFrame())
  checks.push('navigation request/response identity')

  const listenerOrder = []
  const appended = (response) => {
    if (response instanceof NativeResponse && response.url().endsWith('/listener-order'))
      listenerOrder.push('append')
  }
  const prepended = (response) => {
    if (response instanceof NativeResponse && response.url().endsWith('/listener-order'))
      listenerOrder.push('prepend')
  }
  page.on('response', appended)
  page.prependListener('response', prepended)
  await page.evaluate(async () => await (await fetch('/listener-order')).text())
  page.removeListener('response', appended)
  page.removeListener('response', prepended)
  assert.deepEqual(listenerOrder, ['prepend', 'append'])
  page.setDefaultTimeout(40)
  const timeoutStarted = Date.now()
  await assert.rejects(page.waitForRequest('**/never-arrives'), /Timeout.*40ms|timed out/u)
  assert.ok(Date.now() - timeoutStarted < 2000, 'Network wait must inherit owning Page timeout')
  page.setDefaultTimeout(30000)
  checks.push('native network waits inherit Page timeout and prepended listeners run first')

  await page.setExtraHTTPHeaders({ 'X-Native-Extra': 'header-value' })
  const requestWait = page.waitForRequest('**/echo')
  const responseWait = page.waitForResponse(
    (response) => response.url().endsWith('/echo') && response.status() === 200,
  )
  const [echoedText, request, response] = await Promise.all([
    page.evaluate(
      async () =>
        await (await fetch('/echo', { method: 'POST', body: 'raw native upload' })).text(),
    ),
    requestWait,
    responseWait,
  ])
  const EchoSchema = z.object({
    method: z.string(),
    headers: z.record(z.string(), z.string()),
    body: z.string(),
  })
  const echoed = EchoSchema.parse(JSON.parse(z.string().parse(echoedText)))
  assert.equal(request.method(), 'POST')
  assert.equal(request.postData(), 'raw native upload')
  assert.equal(response.request(), request)
  assert.equal(echoed.headers['x-native-extra'], 'header-value')
  assert.equal(
    EchoSchema.parse(await response.json()).body,
    Buffer.from('raw native upload').toString('hex'),
  )
  assert.equal(await response.finished(), null)
  assert.equal(request.timing().responseEnd >= 0, true)
  checks.push('native request/response waits, headers, post data, body and timing')

  await page.route('**/native-fulfilled', (route) =>
    route.fulfill({ status: 201, json: { from: 'native C++ Fetch interception' } }),
  )
  const fulfilledResponse = page.waitForResponse('**/native-fulfilled')
  const [fulfilled, fulfilledNetworkResponse] = await Promise.all([
    page.evaluate(async () => {
      const result = await fetch('/native-fulfilled')
      return { status: result.status, body: await result.text() }
    }),
    fulfilledResponse,
  ])
  assert.equal(fulfilled.status, 201)
  assert.deepEqual(JSON.parse(fulfilled.body), { from: 'native C++ Fetch interception' })
  assert.equal(fulfilledNetworkResponse.status(), 201)
  assert.equal(
    observed.some((item) => item.url === '/native-fulfilled'),
    false,
  )
  await page.unroute('**/native-fulfilled')
  checks.push('interception fulfillment bypasses origin server')

  const binary = Buffer.alloc(100_001)
  for (let i = 0; i < binary.length; i++) binary[i] = i % 256
  await page.route(
    '**/binary-fulfilled',
    (route) => route.fulfill({ body: binary, contentType: 'application/octet-stream' }),
    { times: 1 },
  )
  const binaryResult = await page.evaluate(async () => {
    const bytes = new Uint8Array(await (await fetch('/binary-fulfilled')).arrayBuffer())
    return {
      length: bytes.length,
      first: bytes[0],
      last: bytes[bytes.length - 1],
      checksum: bytes.reduce((total, value) => total + value, 0),
    }
  })
  assert.deepEqual(binaryResult, {
    length: binary.length,
    first: binary[0],
    last: binary.at(-1),
    checksum: binary.reduce((total, value) => total + value, 0),
  })
  checks.push('exact 100001-byte binary fulfillment through bounded upload chunks')

  await page.route('**/rewritten', (route) =>
    route.continue({
      url: `${origin}/echo`,
      method: 'POST',
      postData: 'modified body',
      headers: { ...route.request().headers(), 'X-Native-Route': 'modified' },
    }),
  )
  const rewrittenText = await page.evaluate(async () => await (await fetch('/rewritten')).text())
  const rewritten = EchoSchema.parse(JSON.parse(z.string().parse(rewrittenText)))
  assert.equal(rewritten.method, 'POST')
  assert.equal(rewritten.headers['x-native-route'], 'modified')
  assert.equal(rewritten.body, Buffer.from('modified body').toString('hex'))
  await page.unroute('**/rewritten')
  checks.push('continue overrides URL, method, body and headers')

  await page.route('**/aborted', (route) => route.abort('blockedbyclient'))
  const failureWait = page.waitForEvent('requestfailed', {
    predicate: (item) => item.url().endsWith('/aborted'),
  })
  const [abortFailed, aborted] = await Promise.all([
    page.evaluate(async () => {
      try {
        await fetch('/aborted')
        return false
      } catch {
        return true
      }
    }),
    failureWait,
  ])
  assert.equal(abortFailed, true)
  assert.match(aborted.failure().errorText, /BLOCKED|ERR_/u)
  assert.equal(
    observed.some((item) => item.url === '/aborted'),
    false,
  )
  await page.unroute('**/aborted')
  checks.push('abort rejects actual fetch and emits requestfailed')

  await page.networkController().setOffline(true)
  assert.equal(
    await page.evaluate(async () => {
      try {
        await fetch('/offline')
        return false
      } catch {
        return true
      }
    }),
    true,
  )
  await page.networkController().setOffline(false)
  assert.equal(
    await page.evaluate(async () => await (await fetch('/online')).text()),
    'native /online',
  )
  checks.push('offline transition and recovery')

  const redirect = await page.goto(`${origin}/redirect`)
  const finalRequest = redirect.request()
  assert.equal(redirect.url(), `${origin}/final`)
  assert.equal(finalRequest.redirectedFrom().url(), `${origin}/redirect`)
  assert.equal(finalRequest.redirectedFrom().redirectedTo(), finalRequest)
  assert.equal(await redirect.text(), 'redirect complete')
  checks.push('redirect request chain and final response body')

  await page.goto(origin)
  await page.route('**/child-data', (route) => route.fulfill({ json: { nativeChild: true } }))
  await page.evaluate(
    (url) =>
      new Promise((resolveFrame) => {
        const frame = document.createElement('iframe')
        frame.src = url
        frame.onload = () => resolveFrame(true)
        document.body.append(frame)
      }),
    `http://localhost:${port}/child`,
  )
  const child = page.frames().find((frame) => frame !== page.mainFrame())
  assert.ok(child)
  const childText = await child.evaluate(async () => await (await fetch('/child-data')).text())
  assert.deepEqual(JSON.parse(z.string().parse(childText)), { nativeChild: true })
  await page.unrouteAll()
  checks.push('out-of-process iframe inherits native interception')

  let boundaryBodyBytes = 0
  await page.route('**/within-interception-boundary', (route) => {
    boundaryBodyBytes = route.request().postDataBuffer()?.length ?? 0
    return route.continue()
  })
  assert.equal(
    await page.evaluate(
      async () =>
        await (
          await fetch('/within-interception-boundary', {
            method: 'POST',
            body: 'x'.repeat(1_020_003),
          })
        ).text(),
    ),
    'native /within-interception-boundary',
  )
  assert.equal(boundaryBodyBytes, 1_020_003)
  const boundaryOrigin = observed.find((request) => request.url === '/within-interception-boundary')
  assert.ok(boundaryOrigin)
  assert.equal(boundaryOrigin.body, Buffer.alloc(1_020_003, 'x').toString('hex'))
  await page.unrouteAll()
  checks.push(
    'actual 1020003-byte POST within serialized interception boundary continues unchanged',
  )

  let largeHandlerCalls = 0
  let largeCapturedBytes = 0
  await page.route('**/large-current-interception', (route) => {
    largeHandlerCalls++
    largeCapturedBytes = route.request().postDataBuffer()?.length ?? 0
    return route.continue()
  })
  assert.equal(
    await page.evaluate(
      async () =>
        await (
          await fetch('/large-current-interception', {
            method: 'POST',
            body: 'x'.repeat(1_100_003),
          })
        ).text(),
    ),
    'native /large-current-interception',
  )
  assert.equal(largeHandlerCalls, 1)
  assert.equal(largeCapturedBytes, 1_100_003)
  assert.equal(
    observed.find((request) => request.url === '/large-current-interception')?.body,
    Buffer.alloc(1_100_003, 'x').toString('hex'),
  )
  await page.unrouteAll()
  checks.push('actual 1100003-byte POST uses native body chunks and continues exact original bytes')

  for (const size of [2_097_152, 2_097_153]) {
    const path = '/capture-limit-' + size
    let calls = 0
    let body
    let unavailable = false
    let readError
    await page.route('**' + path, (route) => {
      calls++
      unavailable = route.request().info.postDataUnavailable === true
      try {
        body = route.request().postDataBuffer()
      } catch (error) {
        readError = error instanceof Error ? error.message : String(error)
      }
      return route.continue()
    })
    assert.equal(
      await page.evaluate(
        async ({ path, size }) =>
          await (await fetch(path, { method: 'POST', body: 'z'.repeat(size) })).text(),
        { path, size },
      ),
      'native ' + path,
    )
    assert.equal(calls, 1)
    assert.equal(
      observed.find((request) => request.url === path)?.body,
      Buffer.alloc(size, 'z').toString('hex'),
    )
    if (size === 2_097_152) {
      assert.equal(z.instanceof(Buffer).parse(body).length, size)
      assert.equal(unavailable, false)
    } else {
      assert.equal(body, undefined)
      assert.equal(unavailable, true)
      assert.match(z.string().parse(readError), /unavailable|capacity/u)
    }
    await page.unrouteAll()
  }
  assert.equal(page.networkController().takeRouteErrors().length, 0)
  checks.push(
    'actual 2 MiB capture limit retains exact bytes, above-limit genuine browser upload continues with explicit unavailable observation',
  )

  const diagnosticsState = z
    .object({
      networkPaused: z.number(),
      networkRoutes: z.number(),
      networkWaiters: z.number(),
      otherOperations: z.number(),
    })
    .passthrough()
    .parse(await browser.runAutomation('diagnostics'))
  assert.equal(diagnosticsState.networkPaused, 0)
  assert.equal(diagnosticsState.networkRoutes, 0)
  assert.equal(diagnosticsState.networkWaiters, 0)
  assert.equal(diagnosticsState.otherOperations, 0)
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binaryHash,
  )
  const report = {
    passed: true,
    started,
    completed: new Date().toISOString(),
    binaryHash,
    checks,
    eventCounts: {
      requests: requests.length,
      responses: responses.length,
      finished: finished.length,
      failed: failed.length,
    },
    diagnostics: diagnosticsState,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} catch (error) {
  const message = error instanceof Error ? error.stack : 'Unknown native network verification error'
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify(
      {
        passed: false,
        started,
        completed: new Date().toISOString(),
        binaryHash,
        checks,
        error: message,
      },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  await browser?.close()
  await new Promise((resolveClose) => server.close(resolveClose))
  await writeFile(resolve(directory, 'native-stderr.log'), diagnostics.join(''))
}
