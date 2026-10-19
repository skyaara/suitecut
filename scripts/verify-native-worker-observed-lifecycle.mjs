// SPDX-License-Identifier: MIT
/* global navigator */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'

import * as z from 'zod'

import {
  launchNativeBrowser,
  NativeBrowserContexts,
  NativeRequest,
  NativeResponse,
} from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-worker-observed-lifecycle',
)
await mkdir(directory, { recursive: true })
/** @type {import('node:http').ServerResponse | undefined} */
let heldScript
const script = `void fetch('/startup-observation');console.log('actual native startup console');self.addEventListener('install',event=>event.waitUntil(skipWaiting()));self.addEventListener('activate',event=>event.waitUntil(clients.claim()));`
const server = createServer((request, response) => {
  response.setHeader('Cache-Control', 'no-store')
  if (request.url === '/service.js') {
    response.setHeader('Content-Type', 'application/javascript')
    response.flushHeaders()
    response.write('/* actual held service worker prefix */\n')
    heldScript = response
  } else if (request.url === '/startup-observation') {
    response.setHeader('Content-Type', 'text/plain')
    response.end('actual startup request response')
  } else {
    response.setHeader('Content-Type', 'text/html')
    response.end('<!doctype html><body>actual worker observation owner</body>')
  }
})
await new Promise((done) => server.listen(0, '127.0.0.1', () => done(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
/** @typedef {{worker:import('../dist/native.js').NativeWorker,request:import('../dist/native.js').NativeRequest,inventoryContains:boolean}} Observed */
/** @type {Observed[]} */
const observed = []
/** @type {import('../dist/native.js').NativeBrowser | undefined} */
let source
let stderr = ''
/** @type {{result:string,binarySHA256:string,checks:string[],unexercised:string[],metadata:{id:string,type:string,url:string,publishedAtDelivery:boolean}[],error?:string,nativeShutdown?:string,nativeStderrBytes?:number,logicalBoundary?:{workerClosed:boolean,processExited:boolean},actualProcessExit?:{code:number|null,signal:NodeJS.Signals|null}|undefined}} */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  checks: [],
  unexercised: [],
  metadata: [],
}
/** @type {Promise<unknown>[]} */
const pending = []
/**
 * Yield actual server/native observations, within the authored proof budget.
 * @param {()=>boolean} predicate The actual receipt predicate.
 * @param {number} budget The observation budget, not a native operation timeout.
 */
async function observedWithin(predicate, budget) {
  const end = performance.now() + budget
  while (!predicate() && performance.now() < end) await setImmediate()
  return predicate()
}
function releaseScript() {
  if (heldScript && !heldScript.writableEnded) heldScript.end(script)
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 180,
    headless: true,
    onDiagnostic: (message) => {
      stderr += message
    },
  })
  const active = source
  assert(typeof active.runContext === 'function')
  assert(typeof active.pageSource === 'function')
  assert(typeof active.onContextEvent === 'function')
  const context = new NativeBrowserContexts({
    ...active,
    runContext: active.runContext.bind(active),
    pageSource: active.pageSource.bind(active),
    onContextEvent: active.onContextEvent.bind(active),
  }).defaultContext()
  const page = context.pages()[0]
  assert(page)
  /**
   * Record genuine worker attribution from request or response delivery.
   * @param {import('../dist/native.js').NativeRequest} request The canonical observed request.
   */
  const recordRequest = (request) => {
    const info = request.info.workerInfo
    if (info?.type !== 'service_worker' || observed.some((item) => item.request === request)) return
    const worker = request.serviceWorker()
    assert(worker)
    const inventoryContains = context.serviceWorkers().some((item) => item.info.id === info.id)
    observed.push({ worker, request, inventoryContains })
    report.metadata.push({
      id: info.id,
      type: info.type,
      url: request.url(),
      publishedAtDelivery: inventoryContains,
    })
  }
  context.on('request', (request) => {
    assert(request instanceof NativeRequest)
    recordRequest(request)
  })
  context.on('response', (response) => {
    assert(response instanceof NativeResponse)
    recordRequest(response.request())
  })
  await page.goto(origin)
  const ready = context.waitForServiceWorker({ timeout: 10000 })
  const readyObserved = ready.then(
    (worker) => ({ worker }),
    (/** @type {unknown} */ error) => ({ error }),
  )
  pending.push(readyObserved)
  const registration = page.evaluate(async () => {
    await navigator.serviceWorker.register('/service.js')
  })
  pending.push(
    registration.then(
      () => ({ registered: true }),
      (/** @type {unknown} */ error) => ({ error }),
    ),
  )
  assert(
    await observedWithin(() => heldScript !== undefined, 5000),
    'Actual worker script request did not reach the server',
  )
  const beforeRelease = await observedWithin(
    () => observed.some((item) => !item.inventoryContains),
    1000,
  )
  if (!beforeRelease)
    report.unexercised.push(
      'No actual pre-ready worker metadata while script body remained held; bootstrap-failure-before-ready is not proved',
    )
  /** @type {import('../dist/native.js').NativeWorker} */
  let worker
  if (beforeRelease) {
    const early = observed.find((item) => !item.inventoryContains)
    assert(early)
    worker = early.worker
    assert(!context.serviceWorkers().some((item) => item.info.id === worker.info.id))
    report.checks.push(
      'Actual native request metadata precedes ready inventory; real helper shutdown begins while script bytes remain held',
    )
  } else {
    releaseScript()
    const outcome = await readyObserved
    if ('error' in outcome) throw outcome.error
    worker = outcome.worker
    await registration
    assert(
      await observedWithin(() => observed.length > 0, 5000),
      'Actual service worker startup request metadata was not delivered',
    )
    assert.equal(await worker.evaluate(() => 42), 42)
    report.checks.push(
      'Actual startup request retains the genuine canonical service worker after real ready publication',
    )
  }
  const first = observed.find((item) => item.worker.info.id === worker.info.id)
  assert(first)
  assert.equal(first.worker, worker)
  assert.equal(first.request.serviceWorker(), worker)
  assert.equal(first.worker.info.id, first.request.info.workerInfo?.id)
  assert.equal(worker.isClosed(), false)
  assert(active.processClosed, 'Actual helper-process closure promise is unavailable')
  active.signal.addEventListener(
    'abort',
    () => {
      report.logicalBoundary = {
        workerClosed: worker.isClosed(),
        processExited: active.processExit?.() !== undefined,
      }
    },
    { once: true },
  )
  const shutdown = active.close()
  assert.deepEqual(report.logicalBoundary, { workerClosed: false, processExited: false })
  await shutdown
  await active.processClosed
  assert.equal(worker.isClosed(), true)
  assert.equal(first.request.serviceWorker(), worker)
  report.actualProcessExit = active.processExit?.()
  assert.deepEqual(report.actualProcessExit, { code: 0, signal: null })
  report.checks.push(
    'Logical shutdown preserves worker identity without fake closure; real helper exit settles the same worker',
  )
  report.result = 'passed'
} catch (error) {
  report.result = 'failed'
  report.error = error instanceof Error ? (error.stack ?? error.message) : String(error)
  process.exitCode = 1
} finally {
  releaseScript()
  if (source)
    try {
      await source.close()
      report.nativeShutdown = 'clean'
    } catch (error) {
      report.nativeShutdown = String(error)
      report.result = 'failed'
      process.exitCode = 1
    }
  await Promise.allSettled(pending)
  server.closeAllConnections()
  await new Promise((done) => server.close(() => done(undefined)))
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(resolve(directory, 'native-stderr.log'), stderr)
  if (stderr) {
    report.result = 'failed'
    process.exitCode = 1
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
}
console.log(JSON.stringify(report))
