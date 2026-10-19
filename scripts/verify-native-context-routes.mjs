// SPDX-License-Identifier: MIT
/* global navigator, fetch */
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
  process.env.SUITECUT_NATIVE_CONTEXT_ROUTES_DIRECTORY ?? '.suitecut/native-context-routes',
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
    request.url === '/service.js' ? 'application/javascript' : 'text/html',
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
let close = () => Promise.resolve()
try {
  const source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (value) => diagnostics.push(value),
  })
  close = () => source.close()
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
  /**
   * Read the actual authored URL.
   * @param {string} url The route path.
   */
  const read = (url) =>
    page.evaluate((actual) => fetch(actual).then((response) => response.text()), origin + url)
  let existing = 0
  await context.route(origin + '/existing', async (route) => {
    ++existing
    await route.fulfill({ body: 'context-existing' })
  })
  assert.equal(await read('/existing'), 'context-existing')
  assert.equal(existing, 1)
  assert.equal(wire.includes('/existing'), false)
  checks.push('An existing actual Page invokes the context handler through its real callback owner')

  let shared = 0
  await context.route(
    origin + '/shared',
    async (route) => {
      ++shared
      await route.fulfill({ body: 'context-once' })
    },
    { times: 1 },
  )
  const future = await context.newPage()
  await future.goto(origin)
  const values = await Promise.all([
    read('/shared'),
    future.evaluate((url) => fetch(url).then((response) => response.text()), origin + '/shared'),
  ])
  assert.deepEqual(values.sort(), ['actual-wire', 'context-once'])
  assert.equal(shared, 1)
  assert.equal(wire.filter((url) => url === '/shared').length, 1)
  checks.push(
    'One context-global times counter is shared by current and genuinely future Page targets',
  )

  const order = z.array(z.string()).parse([])
  await context.route(origin + '/order', async (route) => {
    order.push('context')
    await route.fulfill({ body: 'ordered-context' })
  })
  await page.route(origin + '/order', async (route) => {
    order.push('page')
    await route.fallback()
  })
  assert.equal(await read('/order'), 'ordered-context')
  assert.deepEqual(order, ['page', 'context'])
  checks.push(
    'The native original match plan invokes Page priority before genuine context fallback',
  )

  /**
   * Fulfill the actual selected native invocation.
   * @param {import('../dist/native.js').NativeRoute} route The real route.
   */
  const removed = async (route) => {
    await route.fulfill({ body: 'removed-handler' })
  }
  await context.route(origin + '/removed', removed)
  await context.unroute(origin + '/removed', removed)
  assert.equal(await read('/removed'), 'actual-wire')
  checks.push('Actual unroute acknowledgement removes the selected context registration')

  await page.route(origin + '/page-only', async (route) => {
    await route.fulfill({ body: 'page-still-owned' })
  })
  await context.unrouteAll()
  assert.equal(await read('/existing'), 'actual-wire')
  assert.equal(await read('/page-only'), 'page-still-owned')
  checks.push('Context unrouteAll removes only context registrations and preserves Page policy')

  const discovered = context.waitForServiceWorker({ timeout: 5000 })
  discovered.catch(() => undefined)
  await page.evaluate(async () => {
    await navigator.serviceWorker.register('/service.js')
    await navigator.serviceWorker.ready
  })
  const worker = await discovered
  let broker = 0
  await context.route(origin + '/broker', async (route, request) => {
    ++broker
    assert.equal(request.serviceWorker(), worker)
    assert.throws(() => request.frame(), /frame/iu)
    await route.fulfill({ body: 'page-less-context' })
  })
  await future.close()
  await page.close()
  assert.equal(
    await worker.evaluate(
      (url) => fetch(url).then((response) => response.text()),
      origin + '/broker',
    ),
    'page-less-context',
  )
  assert.equal(broker, 1)
  assert.equal(wire.includes('/broker'), false)
  checks.push(
    'A real page-less broker retains context routing after initiating Pages physically close',
  )
  await source.close()
  await source.processClosed
  assert.deepEqual(source.processExit?.(), { code: 0, signal: null })
  receipts.push({ phase: 'actual helper process and stdio closure', value: source.processExit?.() })
} catch (error) {
  failures.push(error instanceof Error ? (error.stack ?? error.message) : String(error))
} finally {
  await close().catch((error) => failures.push(String(error)))
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
          'Public string-glob context routes only; RegExp/HAR ordering/custom predicates and popup vendor first-wire gaps remain unclaimed.',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
assert.equal(checks.length, 6)
