// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve('.suitecut/native-network-metadata')
await mkdir(directory, { recursive: true })
const sha256 = () =>
  readFile(executable).then((bytes) => createHash('sha256').update(bytes).digest('hex'))
const executableSha256 = await sha256(),
  started = new Date().toISOString(),
  checks = [],
  failures = [],
  stderr = []
const server = createServer((request, response) => {
  if (request.url === '/sse') {
    response.writeHead(200, { 'Content-Type': 'text/event-stream', 'Cache-Control': 'no-cache' })
    response.write('data: actual-native-message\n\n')
    return
  }
  if (request.url === '/headers') {
    response.setHeader('__proto__', 'actual-response')
    response.setHeader('constructor', 'actual-constructor')
    response.setHeader('Set-Cookie', ['actual_a=1; Path=/', 'actual_b=2; Path=/'])
    response.setHeader('Header-A', ['one', 'two'])
    response.end(JSON.stringify({ raw: request.rawHeaders }))
    return
  }
  request.resume()
  response.setHeader('Content-Type', 'text/html')
  response.end('<!doctype html><body>genuine native metadata</body>')
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const origin = `http://127.0.0.1:${z.object({ port: z.number() }).parse(server.address()).port}`
let source
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 320,
    height: 180,
    onDiagnostic: (message) => stderr.push(message),
  })
  const page = createNativePage(source),
    navigation = await page.goto(`${origin}/page`)
  assert.ok(navigation)
  assert.equal(navigation.request().postDataJSON(), null)
  checks.push('actual native no-body request postDataJSON returns null')
  const requestPromise = page.waitForRequest(`${origin}/headers`),
    responsePromise = page.waitForResponse(`${origin}/headers`)
  const fetchPromise = page.evaluate(
    `fetch('/headers',{headers:[['__proto__','actual-request'],['constructor','actual-request-constructor']]}).then(response=>response.text())`,
  )
  const request = await requestPromise,
    response = await responsePromise
  const requestHeaders = await request.allHeaders(),
    responseHeaders = await response.allHeaders()
  const actual = z
    .object({ raw: z.array(z.string()) })
    .parse(JSON.parse(z.string().parse(await fetchPromise)))
  const expected = Object.fromEntries(
    Array.from({ length: actual.raw.length / 2 }, (_, index) => [
      z
        .string()
        .parse(actual.raw[index * 2])
        .toLowerCase(),
      z.string().parse(actual.raw[index * 2 + 1]),
    ]),
  )
  assert.deepEqual(requestHeaders, expected)
  assert.equal(requestHeaders.__proto__, 'actual-request')
  assert.equal(responseHeaders.__proto__, 'actual-response')
  assert.equal(responseHeaders.constructor, 'actual-constructor')
  checks.push(
    'native full request headers equal actual origin wire fields and preserve own poison-named request/response headers',
  )
  assert.deepEqual(await response.headerValues('set-cookie'), [
    'actual_a=1; Path=/',
    'actual_b=2; Path=/',
  ])
  assert.equal(
    (await response.headersArray()).filter((header) => header.name.toLowerCase() === 'set-cookie')
      .length,
    2,
  )
  checks.push('genuine repeated Set-Cookie response fields remain distinct through native metadata')
  if (responseHeaders['header-a'] === 'one, two')
    checks.push('duplicate non-cookie response fields normalize to comma-delimited values')
  else
    failures.push({
      group: 'duplicate response header normalization',
      actual: responseHeaders['header-a'],
      expected: 'one, two',
    })
  const ssePromise = page.waitForRequest(`${origin}/sse`)
  assert.equal(
    await page.evaluate(
      `(() => {const actual=new EventSource('/sse');globalThis.actualSSE=actual;return new Promise(resolve=>actual.onmessage=event=>resolve(event.data));})()`,
    ),
    'actual-native-message',
  )
  const sse = await ssePromise
  if (sse.resourceType() === 'eventsource')
    checks.push('actual EventSource native request resource type')
  else
    failures.push({
      group: 'EventSource native request resource type',
      actual: sse.resourceType(),
      expected: 'eventsource',
      headers: await sse.allHeaders(),
    })
  await page.evaluate('globalThis.actualSSE.close()')
  const binaryPromise = page.waitForRequest(`${origin}/binary`)
  await page.evaluate(
    `fetch('/binary',{method:'POST',body:new Uint8Array(Array.from({length:256},(_,index)=>index))}).then(response=>response.text())`,
  )
  const binary = await binaryPromise
  const bytes = binary.postDataBuffer()
  if (bytes?.equals(Buffer.from(Array.from({ length: 256 }, (_, index) => index))))
    checks.push('genuine original binary body bytes')
  else
    failures.push({
      group: 'original binary body capture',
      actualLength: bytes?.length,
      expectedLength: 256,
    })
  const multipartPromise = page.waitForRequest(`${origin}/multipart`)
  await page.evaluate(
    `(() => { const data=new FormData();data.append('name1','value1');data.append('file',new File(['file-value'],'foo.txt'));data.append('name2','value2');return fetch('/multipart',{method:'POST',body:data}).then(response=>response.text()); })()`,
  )
  const multipart = await multipartPromise
  const multipartBytes = multipart.postDataBuffer()
  if (multipartBytes?.includes(Buffer.from('file-value')))
    checks.push('genuine original multipart file bytes captured')
  else
    failures.push({
      group: 'original multipart body capture',
      actualLength: multipartBytes?.length,
    })
} catch (error) {
  failures.push({
    group: 'native metadata',
    error: error instanceof Error ? error.stack : String(error),
  })
} finally {
  if (source) await source.close().catch(() => undefined)
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  assert.equal(await sha256(), executableSha256)
  await writeFile(resolve(directory, 'stderr.log'), stderr.join('\n'))
  const report = {
    ok: failures.length === 0,
    started,
    ended: new Date().toISOString(),
    executableSha256,
    checks,
    failures,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
  if (failures.length) process.exitCode = 1
}
