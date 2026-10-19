// SPDX-License-Identifier: MIT
/* global fetch, navigator */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { NativeWorkerControlSchema } from '../dist/native-worker-protocol.js'
import {
  launchNativeBrowser,
  NativeBrowserContexts,
  NativeRequest,
  NativeResponse,
} from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_WORKER_NETWORK_DIRECTORY ?? '.suitecut/native-context-worker-network',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const wire = z.array(z.object({ url: z.string(), method: z.string(), body: z.string() })).parse([])
const events = z.array(z.object({ event: z.string(), url: z.string(), id: z.string() })).parse([])
const controls = z
  .array(z.object({ phase: z.string(), owner: z.string(), generation: z.string() }))
  .parse([])
const checks = z.array(z.string()).parse([])
const failures = z.array(z.string()).parse([])
const diagnostics = z.array(z.string()).parse([])
const server = createServer((request, response) => {
  void (async () => {
    const chunks = []
    for await (const chunk of request) {
      assert.ok(Buffer.isBuffer(chunk))
      chunks.push(chunk)
    }
    const bytes = Buffer.concat(chunks)
    const url = request.url ?? ''
    wire.push({ url, method: request.method ?? '', body: bytes.toString('base64') })
    if (url === '/service.js') {
      response.setHeader('content-type', 'application/javascript')
      response.end(
        "self.addEventListener('install',e=>e.waitUntil(skipWaiting()));" +
          "self.addEventListener('activate',e=>e.waitUntil(clients.claim()));",
      )
    } else if (url.startsWith('/worker-')) {
      response.setHeader('content-type', 'application/octet-stream')
      response.end(bytes)
    } else {
      response.setHeader('content-type', 'text/html')
      response.end('<body>actual context worker network</body>')
    }
  })().catch((error) => {
    failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
    response.destroy()
  })
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
let closeSource = () => Promise.resolve()
try {
  const source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (value) => diagnostics.push(value),
  })
  closeSource = () => source.close()
  const context = new NativeBrowserContexts(source).defaultContext()
  const page = context.pages()[0]
  assert.ok(page)
  const pageRequests = z.array(z.string()).parse([])
  const records = z
    .map(
      z.string(),
      z.object({
        requests: z.array(z.instanceof(NativeRequest)),
        responses: z.array(z.instanceof(NativeResponse)),
        finished: z.array(z.instanceof(NativeRequest)),
      }),
    )
    .parse(new Map())
  const record = (input) => {
    const url = z.string().parse(input)
    let value = records.get(url)
    if (!value) {
      value = { requests: [], responses: [], finished: [] }
      records.set(url, value)
    }
    return value
  }
  page.on('request', (request) => pageRequests.push(request.url()))
  context.on('request', (request) => {
    assert.ok(request instanceof NativeRequest)
    record(request.url()).requests.push(request)
    events.push({ event: 'request', url: request.url(), id: request.info.id })
  })
  context.on('response', (response) => {
    assert.ok(response instanceof NativeResponse)
    record(response.url()).responses.push(response)
    events.push({ event: 'response', url: response.url(), id: response.request().info.id })
  })
  context.on('requestfinished', (request) => {
    assert.ok(request instanceof NativeRequest)
    record(request.url()).finished.push(request)
    events.push({ event: 'requestfinished', url: request.url(), id: request.info.id })
  })
  await page.goto(origin)
  const discovered = context.waitForServiceWorker({ timeout: 10000 })
  await page.evaluate(async () => {
    await navigator.serviceWorker.register('/service.js')
    await navigator.serviceWorker.ready
  })
  const worker = await discovered
  const control = NativeWorkerControlSchema.parse(worker.info.control)
  const nativeInventory = source
    .contextWorkerInfos?.(context.id)
    .find((info) => info.id === worker.info.id)
  assert.deepEqual(nativeInventory?.control, control)
  controls.push({ phase: 'actual-native-ready', ...control })
  const opaque = await worker.evaluateHandle(() => ({ actualNativeControl: true }))
  assert.deepEqual(await opaque.jsonValue(), { actualNativeControl: true })
  await opaque.dispose()
  const consoleWait = context.waitForEvent('console', { timeout: 5000 })
  await worker.evaluate(() => console.log('actual canonical worker console'))
  const message = await consoleWait
  assert.equal(message.worker(), worker)
  assert.equal(message.page(), null)
  const argument = message.args()[0]
  assert.ok(argument)
  assert.equal(await argument.jsonValue(), 'actual canonical worker console')
  await argument.dispose()
  const expected = Array.from({ length: 256 }, (_, index) => index)
  for (const phase of ['before', 'after']) {
    if (phase === 'after') await page.close()
    const url = `${origin}/worker-${phase}`
    const requestWait =
      phase === 'after' ? context.waitForEvent('request', { timeout: 5000 }) : undefined
    const responseWait =
      phase === 'after' ? context.waitForEvent('response', { timeout: 5000 }) : undefined
    const finishedWait =
      phase === 'after' ? context.waitForEvent('requestfinished', { timeout: 5000 }) : undefined
    const actual = await worker.evaluate(
      async ({ url, bytes }) =>
        Array.from(
          new Uint8Array(
            await (
              await fetch(url, { method: 'POST', body: Uint8Array.from(bytes) })
            ).arrayBuffer(),
          ),
        ),
      { url, bytes: expected },
    )
    assert.deepEqual(actual, expected)
    const observed = record(url)
    assert.equal(observed.requests.length, 1)
    assert.equal(observed.responses.length, 1)
    const request = observed.requests[0]
    const response = observed.responses[0]
    assert.ok(request && response)
    assert.deepEqual(request.postDataBuffer(), Buffer.from(expected))
    assert.equal(request.info.workerInfo?.id, worker.info.id)
    assert.equal(request.info.serviceWorker, true)
    assert.equal(request.serviceWorker(), worker)
    assert.deepEqual(worker.info.control, control)
    controls.push({
      phase: `${phase}-Page-close-request-observer`,
      ...NativeWorkerControlSchema.parse(request.info.workerInfo?.control),
    })
    assert.throws(() => request.frame(), /service worker|no longer known/u)
    assert.ok(response.request() === request)
    assert.deepEqual(await response.body(), Buffer.from(expected))
    assert.equal(response.fromServiceWorker(), false)
    if (phase === 'after') {
      assert.ok((await requestWait) === request)
      assert.ok((await responseWait) === response)
      assert.ok((await finishedWait) === request)
    }
    assert.equal(pageRequests.includes(url), false)
    checks.push(`Canonical exact binary POST and response body ${phase} Page close`)
  }
  const ordinary = record(`${origin}/`).requests[0]
  assert.ok(ordinary)
  assert.equal(ordinary.serviceWorker(), null)
  if (!source.runContext) throw new Error('Native context command transport unavailable')
  const native = z
    .object({ networkBodyOwners: z.literal(0), networkPhysicalControls: z.literal(0) })
    .parse(await source.runContext('worker', { contextId: context.id, action: 'diagnostics' }))
  assert.equal(native.networkBodyOwners, 0)
  checks.push('Native body receipt/take and response control owners settle after ACK')
  const retained = record(`${origin}/worker-after`).requests[0]
  assert.ok(retained)
  await context.close()
  assert.equal(retained.serviceWorker(), worker)
  assert.equal(message.worker(), worker)
  checks.push('Context and physical broker close cleanly')
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await closeSource().catch((error) => failures.push(String(error)))
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  assert.equal(
    createHash('sha256')
      .update(await readFile(executable))
      .digest('hex'),
    binarySHA256,
  )
  if (diagnostics.length) failures.push(`Unexpected native diagnostics: ${diagnostics.length}`)
  await writeFile(resolve(directory, 'stderr.log'), diagnostics.join('\n'))
  const report = { binarySHA256, checks, failures, wire, events, controls }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2))
  console.log(JSON.stringify(report, null, 2))
  if (failures.length) process.exitCode = 1
}
