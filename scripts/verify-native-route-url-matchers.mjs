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
  process.env.SUITECUT_NATIVE_ROUTE_URL_MATCHERS_DIRECTORY ?? '.suitecut/native-route-url-matchers',
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
  const matcher = new RegExp('^' + origin.replaceAll('.', '[.]') + '/regexp$', 'gy')
  matcher.lastIndex = 99
  let pageCalls = 0
  await page.route(matcher, async (route) => {
    ++pageCalls
    await route.fulfill({ body: 'actual-regexp' })
  })
  assert.equal(await read('/regexp'), 'actual-regexp')
  assert.equal(await read('/regexp'), 'actual-regexp')
  assert.equal(matcher.lastIndex, 99)
  assert.equal(pageCalls, 2)
  assert.equal(wire.includes('/regexp'), false)
  checks.push(
    'Page RegExp g/y runs from zero without mutating author lastIndex or sending real wire',
  )

  let predicateCalls = 0,
    shared = 0
  await context.route(
    (url) => {
      if (url.pathname === '/shared-matcher') ++predicateCalls
      return url.origin === origin && url.pathname === '/shared-matcher'
    },
    async (route) => {
      ++shared
      await route.fulfill({ body: 'actual-context-once' })
    },
    { times: 1 },
  )
  const future = await context.newPage()
  await future.goto(origin)
  const values = await Promise.all([
    read('/shared-matcher'),
    future.evaluate(
      (url) => fetch(url).then((response) => response.text()),
      origin + '/shared-matcher',
    ),
  ])
  assert.deepEqual(values.sort(), ['actual-context-once', 'actual-wire'])
  assert.equal(shared, 1)
  assert.ok(predicateCalls >= 1)
  assert.equal(wire.filter((url) => url === '/shared-matcher').length, 1)
  checks.push('A real URL predicate and native global times serve current and future actual Pages')

  const order = z.array(z.string()).parse([])
  await context.route(
    (url) => url.origin === origin && url.pathname === '/original',
    async (route, request) => {
      order.push('context')
      assert.equal(request.url(), origin + '/rewritten')
      await route.fulfill({ body: 'original-policy' })
    },
  )
  await page.route(origin + '/original', async (route) => {
    order.push('page')
    await route.fallback({ url: origin + '/rewritten' })
  })
  assert.equal(await read('/original'), 'original-policy')
  assert.deepEqual(order, ['page', 'context'])
  assert.equal(wire.includes('/original'), false)
  assert.equal(wire.includes('/rewritten'), false)
  checks.push(
    'Native Page priority and fallback retain original matching URL with actual outgoing overrides',
  )

  const removed = new RegExp('/removed-regexp$')
  let removedCalls = 0
  await page.route(removed, async (route) => {
    ++removedCalls
    await route.fulfill({ body: 'removed-must-not-run' })
  })
  await page.unroute(new RegExp(removed.source, removed.flags))
  assert.equal(await read('/removed-regexp'), 'actual-wire')
  assert.equal(removedCalls, 0)
  checks.push(
    'Actual unroute matches an equivalent RegExp registration and acknowledges native removal',
  )

  /**
   * This exact author function is the registered removal identity.
   * @param {URL} url Actual native request URL.
   */
  const removedPredicate = (url) => url.pathname === '/removed-predicate'
  await context.route(removedPredicate, async (route) => {
    ++removedCalls
    await route.fulfill({ body: 'removed-must-not-run' })
  })
  await context.unroute(removedPredicate)
  assert.equal(await read('/removed-predicate'), 'actual-wire')
  assert.equal(removedCalls, 0)
  checks.push('Actual predicate identity removes context policy without inventing an invocation')

  const discovered = context.waitForServiceWorker({ timeout: 5000 })
  discovered.catch(() => undefined)
  await page.evaluate(async () => {
    await navigator.serviceWorker.register('/service.js')
    await navigator.serviceWorker.ready
  })
  const worker = await discovered
  let brokerCalls = 0
  await context.route(
    (url) => url.origin === origin && url.pathname === '/broker-matcher',
    async (route, request) => {
      ++brokerCalls
      assert.equal(request.serviceWorker(), worker)
      assert.throws(() => request.frame(), /frame/iu)
      await route.fulfill({ body: 'actual-broker-predicate' })
    },
  )
  await future.close()
  await page.close()
  assert.equal(
    await worker.evaluate(
      (url) => fetch(url).then((response) => response.text()),
      origin + '/broker-matcher',
    ),
    'actual-broker-predicate',
  )
  assert.equal(brokerCalls, 1)
  assert.equal(wire.includes('/broker-matcher'), false)
  checks.push(
    'Page-less actual service worker owner executes one native-admitted context predicate',
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
          'Public RegExp/synchronous predicate matching only; custom CORS parent-before-Fetch ordering and asynchronous predicates are not claimed. No private overlay or synthetic network events.',
      },
      null,
      2,
    ),
  )
}
assert.deepEqual(failures, [])
assert.equal(checks.length, 6)
