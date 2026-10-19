/* global document, location */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import process from 'node:process'

import * as z from 'zod'

import { nativeURLPredicatePhysicalCount } from '../dist/native-url-predicate.js'
import { launchNativeBrowser, NativeBrowserContexts, createNativePage } from '../dist/native.js'

const folder = new URL('../.suitecut/native-url-wait-proof/', import.meta.url)
await mkdir(folder, { recursive: true })
const report = { phases: [], stderr: '' }
/**
 * Records a completed browser check.
 * @param {string} value - Completed check description.
 */
const phase = (value) => {
  report.phases.push(value)
  console.log(value)
}
/**
 * Actual document response held by the fixture.
 */
let delayedResponse = () => undefined,
  documentHeld = false
/**
 * Actual subresource response held by the fixture.
 */
let holdResponse = () => undefined,
  resourceHeld = false
const server = createServer((q, r) => {
  r.setHeader('content-type', 'text/html')
  if (q.url === '/loading') {
    r.write('<h1>loading</h1><img src="/hold">')
    documentHeld = true
    delayedResponse = () => {
      if (!r.writableEnded) r.end('</body>')
    }
    return
  }
  if (q.url === '/hold') {
    resourceHeld = true
    holdResponse = () => {
      if (!r.writableEnded) r.end('x')
    }
    return
  }
  r.end('<h1>actual ' + q.url + '</h1>')
})
await new Promise((done) => server.listen(0, '127.0.0.1', done))
const address = server.address()
assert(address && typeof address !== 'string')
const port = address.port,
  origin = 'http://localhost:' + port,
  cross = 'http://127.0.0.1:' + port
/**
 * Native helper lifetime.
 */
let source
/**
 * Preserve the first failure across cleanup.
 */
let failure
try {
  source = await launchNativeBrowser({
    ...(process.env.SUITECUT_NATIVE_EXECUTABLE
      ? { executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE }
      : {}),
    headless: true,
    onDiagnostic: (text) => (report.stderr += text),
  })
  const browser = new NativeBrowserContexts(source),
    context = await browser.newContext({ baseURL: origin }),
    page = await context.newPage()
  await page.waitForURL('about:blank')
  await page.goto(origin + '/start')
  await page.waitForURL('/start')
  await page.mainFrame().waitForURL('**/start')
  phase('actual-current-url-page-and-main-frame')
  const glob = page.waitForURL('/{next,last}?*', { waitUntil: 'commit' })
  await page.goto('/next?q=1')
  await glob
  phase('relative-baseurl-glob-future-navigation')
  const regex = /\/hash#target/gu
  regex.lastIndex = 99
  const regexWait = page.waitForURL(regex)
  await page.goto('/hash')
  await page.evaluate(() => (location.hash = 'target'))
  await regexWait
  assert.equal(regex.lastIndex, 99)
  phase('same-document-regexp-original-state-preserved')
  let predicateCalls = 0
  const predicate = page.waitForURL(async (url) => {
    ++predicateCalls
    await Promise.resolve()
    return url.pathname === '/predicate' && url.searchParams.get('real') === '1'
  })
  await page.goto('/predicate?real=1')
  await predicate
  assert.ok(predicateCalls >= 1 && predicateCalls < 10)
  phase('actual-url-object-async-host-predicate')
  const childEvent = page.waitForEvent('framenavigated', {
    predicate: (f) => f.url() === cross + '/child',
  })
  await page.setContent(`<iframe src="${cross}/child"></iframe>`)
  const child = await childEvent
  await child.waitForLoadState()
  await child.waitForURL('**/child')
  const childWait = child.waitForURL('**/child-next#yes')
  await child.goto(cross + '/child-next')
  await child.evaluate(() => (location.hash = 'yes'))
  await childWait
  assert.equal(child.url(), cross + '/child-next#yes')
  phase('actual-cross-origin-frame-scope')
  const reached = new AbortController(),
    release = new AbortController()
  const reachedPromise = new Promise((done) =>
    reached.signal.addEventListener('abort', () => done(undefined), { once: true }),
  )
  const releasePromise = new Promise((done) =>
    release.signal.addEventListener('abort', () => done(undefined), { once: true }),
  )
  let hostReturned = false
  const stale = page.waitForURL(
    async (url) => {
      if (url.pathname === '/stale') {
        reached.abort()
        await releasePromise
        hostReturned = true
        return true
      }
      return false
    },
    { waitUntil: 'commit', timeout: 10000 },
  )
  void stale.catch(() => undefined)
  await page.goto('/stale', { waitUntil: 'commit' })
  await reachedPromise
  await page.goto('/final', { waitUntil: 'commit' })
  release.abort()
  await page.waitForURL('/final')
  await assert.rejects(stale, (error) => error instanceof Error && error.name === 'TimeoutError')
  assert.equal(hostReturned, true)
  phase('stale-host-match-cannot-complete-new-document')
  const commit = page.waitForURL('/loading', { waitUntil: 'commit' })
  const load = page.waitForURL('/loading')
  let loaded = false
  void load.then(
    () => (loaded = true),
    () => undefined,
  )
  await page.goto('/loading', { waitUntil: 'commit' })
  await commit
  assert.equal(loaded, false)
  assert(documentHeld)
  delayedResponse()
  await page.waitForFunction(() => !!document.querySelector('img'))
  assert.equal(loaded, false)
  assert(resourceHeld)
  holdResponse()
  await load
  phase('real-commit-versus-load-no-new-deadline')
  await page.waitForURL('/loading', { waitUntil: 'networkidle' })
  phase('native-descendant-network-idle')
  context.setDefaultNavigationTimeout(35)
  await assert.rejects(
    page.waitForURL('**/never'),
    (e) => e instanceof Error && e.name === 'TimeoutError' && e.message.includes('waitForURL'),
  )
  context.setDefaultNavigationTimeout(30000)
  const controller = new AbortController(),
    aborted = createNativePage(page.source, controller.signal).waitForURL('**/abort', {
      timeout: 0,
    })
  controller.abort(new Error('URL wait canceled'))
  await assert.rejects(aborted, /URL wait canceled/)
  phase('native-default-deadline-and-abort')
  const detachEvent = page.waitForEvent('framenavigated', {
    predicate: (f) => f.url() === cross + '/detach-source',
  })
  await page.setContent(`<iframe src="${cross}/detach-source"></iframe>`)
  const detachFrame = await detachEvent
  await detachFrame.waitForLoadState()
  const detached = detachFrame.waitForURL('**/detached', { timeout: 0 })
  void detached.catch(() => undefined)
  await page.evaluate(() => document.querySelector('iframe')?.remove())
  await assert.rejects(detached, /detach/i)
  phase('exact-frame-detach-cancellation')
  const throwing = page.waitForURL(() => {
    throw new Error('URL callback failure')
  })
  await assert.rejects(throwing, /URL callback failure/)
  phase('host-predicate-error-propagation')
  const settled = new AbortController(),
    started = new AbortController()
  const raw = new Promise((done) =>
    settled.signal.addEventListener('abort', () => done(undefined), { once: true }),
  ).then(() => false)
  const physicalStarted = new Promise((done) =>
    started.signal.addEventListener('abort', () => done(undefined), { once: true }),
  )
  const physicalPromise = page.waitForURL(
    () => {
      started.abort()
      return raw
    },
    { timeout: 35 },
  )
  void physicalPromise.catch(() => undefined)
  await physicalStarted
  assert.equal(nativeURLPredicatePhysicalCount(page.source), 1)
  await assert.rejects(physicalPromise, (e) => e instanceof Error && e.name === 'TimeoutError')
  assert.equal(nativeURLPredicatePhysicalCount(page.source), 1)
  settled.abort()
  await raw
  await Promise.resolve()
  assert.equal(nativeURLPredicatePhysicalCount(page.source), 0)
  phase('physical-host-promise-retained-after-native-timeout')
  const diagnostics = z
    .object({ pendingURLPredicates: z.number(), pendingURLWaits: z.number() })
    .parse(await page.source.runAutomation('diagnostics', {}))
  report.diagnostics = diagnostics
  assert.equal(diagnostics.pendingURLPredicates, 0)
  assert.equal(diagnostics.pendingURLWaits, 0)
  await context.close()
  await source.close()
  report.exit = source.processExit()
  assert.deepEqual(report.exit, { code: 0, signal: null })
  assert.equal(report.stderr, '')
  phase('clean-native-wait-registry-and-shutdown')
  await writeFile(new URL('report.json', folder), JSON.stringify(report, null, 2))
} catch (error) {
  failure = error instanceof Error ? error : new Error(String(error))
  report.error = String(failure)
  report.stack = failure.stack
  await writeFile(new URL('failure.json', folder), JSON.stringify(report, null, 2))
} finally {
  delayedResponse()
  holdResponse()
  await source?.close().catch((error) => (report.cleanupError = String(error)))
  await new Promise((done) => server.close(done))
  await writeFile(new URL('final.json', folder), JSON.stringify(report, null, 2))
}
if (failure) throw failure
