// SPDX-License-Identifier: MIT
/* global navigator, fetch, document */
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executable) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the pinned native engine')
const directory = resolve(
  process.env.SUITECUT_NATIVE_CONTEXT_SERVICE_ROUTE_DIRECTORY ??
    '.suitecut/native-context-service-route',
)
await mkdir(directory, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(executable))
  .digest('hex')
const checks = z.array(z.string()).parse([]),
  failures = z.array(z.string()).parse([]),
  diagnostics = z.array(z.string()).parse([])
const receipts = z.array(z.object({ phase: z.string(), value: z.unknown() })).parse([])
const wire = z.array(z.string()).parse([])
const server = createServer((request, response) => {
  wire.push(request.url ?? '')
  response.setHeader(
    'content-type',
    request.url === '/service.js' ? 'application/javascript' : 'text/plain',
  )
  response.end(
    request.url === '/service.js'
      ? "self.addEventListener('install',e=>e.waitUntil(skipWaiting()));self.addEventListener('activate',e=>e.waitUntil(clients.claim()));"
      : 'actual-wire',
  )
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert.ok(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
/** @type {Awaited<ReturnType<typeof launchNativeBrowser>> | undefined} */
let source
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (value) => diagnostics.push(value),
  })
  const runContext = source.runContext?.bind(source),
    pageSource = source.pageSource?.bind(source),
    onContextEvent = source.onContextEvent?.bind(source)
  assert.ok(runContext && pageSource && onContextEvent)
  const context = new NativeBrowserContexts({
    ...source,
    runContext,
    pageSource,
    onContextEvent,
  }).defaultContext()
  const page = context.pages()[0]
  assert.ok(page)
  await page.goto(origin)
  const discovered = context.waitForServiceWorker({ timeout: 5000 })
  discovered.catch(() => undefined)
  await page.evaluate(async () => {
    await navigator.serviceWorker.register('/service.js')
    await navigator.serviceWorker.ready
  })
  const worker = await discovered
  const routePackets = z.array(z.unknown()).parse([])
  const stop = source.onContextRoute?.((record) => routePackets.push(record))
  let calls = 0
  await context.route(
    origin + '/alive',
    async (route, request) => {
      ++calls
      assert.equal(request.serviceWorker(), worker)
      assert.throws(() => request.frame(), /frame/iu)
      await route.fulfill({ body: 'actual-context-fulfillment' })
    },
    { times: 1 },
  )
  try {
    const first = await worker.evaluate(
      (url) => fetch(url).then((response) => response.text()),
      origin + '/alive',
    )
    assert.equal(first, 'actual-context-fulfillment')
    assert.equal(calls, 1)
    assert.equal(routePackets.length, 1)
    assert.equal(wire.includes('/alive'), false)
    assert.equal(await page.evaluate(() => document.location.origin), origin)
    receipts.push({
      phase: 'actual live Page and worker callback owner',
      value: { worker: worker.info, routePackets },
    })
    checks.push(
      'An actual service-worker fetch with its Page alive invokes one context callback and reaches no server wire',
    )
    const second = await worker.evaluate(
      (url) => fetch(url).then((response) => response.text()),
      origin + '/alive',
    )
    assert.equal(second, 'actual-wire')
    assert.equal(calls, 1)
    assert.equal(routePackets.length, 1)
    assert.equal(wire.filter((url) => url === '/alive').length, 1)
    checks.push(
      'The single native context-global times use remains exhausted across the real Page and broker observers',
    )
  } finally {
    stop?.()
  }
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  if (source) {
    await source.close().catch((error) => failures.push(String(error)))
    await source.processClosed
    const exit = source.processExit?.()
    receipts.push({ phase: 'actual helper process and stdio closure', value: exit })
    if (exit?.code !== 0 || exit.signal !== null)
      failures.push(`Unexpected actual helper exit: ${JSON.stringify(exit)}`)
  }
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
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        binarySHA256,
        checks,
        failures,
        receipts,
        wire,
        scope:
          'Separate live-Page service-worker interception and global-times proof; original context-route6 remains unchanged.',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
assert.equal(checks.length, 2)
