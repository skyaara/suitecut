// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import * as z from 'zod'

import { launchNativeBrowser, createNativePage } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-coverage-workers',
)
await mkdir(directory, { recursive: true })
const counters = z.object({
  nativeCoveragePhysical: z.number().int().nonnegative(),
  nativeCoverageRecordings: z.number().int().nonnegative(),
  nativeCoverageQueuedSources: z.number().int().nonnegative(),
  nativeCoverageSourceBytes: z.number().nonnegative(),
  nativeCoverageSharedPayloadBytes: z.number().nonnegative(),
  nativeCoverageBackendPhysical: z.number().int().nonnegative(),
  nativeWorkers: z.number().int().nonnegative(),
  nativeWorkerHandles: z.number().int().nonnegative(),
  nativeWorkerPhysical: z.number().int().nonnegative(),
  nativeWorkerOperations: z.number().int().nonnegative(),
  nativeWorkerBootstraps: z.number().int().nonnegative(),
  sessions: z.number().int().nonnegative(),
})
const workerJS =
  'function actualWorkerExecuted(){return 73;}onmessage=()=>postMessage(actualWorkerExecuted());'
const firstJS =
  'function initialWorkerExecuted(){return 19;}initialWorkerExecuted();postMessage(19);'
const childJS =
  'function childWorkerExecuted(){return 23;}childWorkerExecuted();const nested=new Worker("/nested.js");nested.onmessage=e=>postMessage(e.data);'
const nestedJS =
  'function nestedWorkerExecuted(){return 29;}nestedWorkerExecuted();postMessage(29);'
const server = createServer((request, response) => {
  const address = server.address()
  assert(address && typeof address !== 'string')
  const body =
    request.url === '/worker.js'
      ? workerJS
      : request.url === '/first.js'
        ? firstJS
        : request.url === '/child-worker.js'
          ? childJS
          : request.url === '/nested.js'
            ? nestedJS
            : undefined
  response.setHeader('Content-Type', body === undefined ? 'text/html' : 'text/javascript')
  response.end(
    body ??
      (request.url === '/frames'
        ? `<iframe src="http://localhost:${address.port}/child"></iframe>`
        : '<body>Actual worker coverage</body>'),
  )
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
const childOrigin = `http://localhost:${address.port}`
/** @type {import('../dist/native.js').NativeBrowser | undefined} */
let source
let stderr = '',
  phase = 'launch'
/** @type {{result:string,binarySHA256:string,groups:string[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,observedFailures?:string[],existing?:import('../dist/native.js').NativeJSCoverageEntry[],future?:import('../dist/native.js').NativeJSCoverageEntry[],nested?:import('../dist/native.js').NativeJSCoverageEntry[],diagnostics?:z.infer<typeof counters>,workerInterrupt?:{elapsed:number,workerPhysical:number,coveragePhysical:number}}} */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  groups: [],
}
/** @type {Promise<void>[]} */
const pendingOperations = []
/** @type {string[]} */
const observedFailures = []
/**
 * Observe a concurrent native call immediately and retain its cleanup receipt.
 * @template T
 * @param {Promise<T>} operation The actual native operation.
 * @returns {Promise<T>} The original operation for normal awaiting.
 */
function observe(operation) {
  pendingOperations.push(
    operation.then(
      () => undefined,
      (error) => {
        observedFailures.push(String(error))
      },
    ),
  )
  return operation
}

try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 360,
    height: 240,
    headless: true,
    onDiagnostic: (text) => {
      stderr += text
    },
  })
  const activeSource = source,
    page = createNativePage(source)
  const diagnostics = async () => counters.parse(await activeSource.runAutomation('diagnostics'))
  const settled = async () => {
    const until = performance.now() + 5000
    while (performance.now() < until) {
      const actual = await diagnostics()
      if (actual.nativeCoveragePhysical === 0 && actual.nativeCoverageRecordings === 0) {
        assert.equal(actual.nativeCoverageQueuedSources, 0)
        assert.equal(actual.nativeCoverageSourceBytes, 0)
        assert.equal(actual.nativeCoverageSharedPayloadBytes, 0)
        assert.equal(actual.nativeCoverageBackendPhysical, 0)
        return actual
      }
    }
    throw Error('Actual worker coverage physical cleanup did not settle')
  }
  const createWorker = async () => {
    const observed = observe(page.waitForEvent('worker', { timeout: 5000 }))
    await page.evaluate('globalThis.actualWorker=new Worker("/worker.js")')
    return await observed
  }
  await page.goto(origin + '/empty')
  phase = 'default document-only capture and genuine existing worker'
  const worker = await createWorker()
  assert.equal(worker.info.type, 'worker')
  await page.coverage.startJSCoverage()
  await page.coverage.startCSSCoverage()
  assert.equal((await diagnostics()).nativeCoverageRecordings, 2)
  assert.equal(await worker.evaluate('actualWorkerExecuted()'), 73)
  assert.deepEqual(await page.coverage.stopJSCoverage(), [])
  assert.deepEqual(await page.coverage.stopCSSCoverage(), [])
  await settled()
  report.groups.push('default JS and CSS capture only document domains while real worker executes')
  phase = 'existing actual worker source and precise function count'
  await page.coverage.startJSCoverage({ includeWorkers: true })
  assert.equal((await diagnostics()).nativeCoverageRecordings, 2)
  for (let i = 0; i < 3; ++i) assert.equal(await worker.evaluate('actualWorkerExecuted()'), 73)
  const units =
    'function actualWorkerUnits(){return 7;}actualWorkerUnits();\n//' +
    String.fromCharCode(0xd800) +
    ' __suitecutNativeUTF16Scalar\n//# sourceURL=actual-worker-units.js'
  assert.equal(await worker.evaluate(`eval(${JSON.stringify(units)})`), 7)
  report.existing = await page.coverage.stopJSCoverage()
  const existing = report.existing.find((entry) => entry.source === workerJS)
  assert(existing)
  assert.equal(existing.url, origin + '/worker.js')
  assert.equal(
    existing.functions.find((fn) => fn.functionName === 'actualWorkerExecuted')?.ranges[0]?.count,
    3,
  )
  await settled()
  report.groups.push('existing genuine dedicated worker exact Debugger source and Profiler count3')
  const exactUnits = report.existing.find((entry) => entry.url === 'actual-worker-units.js')
  assert(exactUnits)
  assert.equal(exactUnits.source, units)
  assert.equal(
    exactUnits.functions.find((fn) => fn.functionName === 'actualWorkerUnits')?.ranges[0]?.count,
    1,
  )
  report.groups.push(
    'actual worker isolated UTF16 source units, literal private-tag text and real function count1',
  )
  const closed = observe(worker.waitForEvent('close'))
  await page.evaluate('actualWorker.terminate()')
  await closed
  phase = 'future actual worker initial-author coverage barrier'
  await page.coverage.startJSCoverage({ includeWorkers: true })
  assert.equal(
    await page.evaluate(
      'new Promise(resolve=>{globalThis.actualWorker=new Worker("/first.js");actualWorker.onmessage=e=>resolve(e.data)})',
    ),
    19,
  )
  assert.equal((await diagnostics()).nativeCoverageRecordings, 2)
  report.future = await page.coverage.stopJSCoverage()
  const first = report.future.find((entry) => entry.source === firstJS)
  assert(first)
  assert.equal(
    first.functions.find((fn) => fn.functionName === 'initialWorkerExecuted')?.ranges[0]?.count,
    1,
  )
  await settled()
  report.groups.push(
    'future actual dedicated-worker Debugger/Profiler ACKs precede first author function count1',
  )
  phase = 'actual OOPIF worker and nested dedicated worker'
  await page.goto(origin + '/frames')
  assert(
    z
      .object({ sessions: z.number().int().positive() })
      .parse(await activeSource.runAutomation('diagnostics')).sessions > 0,
  )
  const child = page.frames().find((frame) => frame.url() === childOrigin + '/child')
  assert(child)
  await page.coverage.startJSCoverage({ includeWorkers: true })
  assert.equal(
    await child.evaluate(
      'new Promise(resolve=>{globalThis.childWorker=new Worker("/child-worker.js");childWorker.onmessage=e=>resolve(e.data)})',
    ),
    29,
  )
  assert.equal((await diagnostics()).nativeCoverageRecordings, 4)
  report.nested = await page.coverage.stopJSCoverage()
  for (const [text, name] of [
    [childJS, 'childWorkerExecuted'],
    [nestedJS, 'nestedWorkerExecuted'],
  ]) {
    /** @type {import('../dist/native.js').NativeJSCoverageEntry | undefined} */
    const actual = report.nested.find((entry) => entry.source === text)
    assert(actual)
    assert.equal(actual.functions.find((fn) => fn.functionName === name)?.ranges[0]?.count, 1)
  }
  await settled()
  report.groups.push(
    'genuine OOPIF-origin and nested dedicated-worker actual sources and initial counts1',
  )
  phase = 'worker termination reports unsampled counter loss'
  await page.goto(origin + '/empty')
  const doomed = await createWorker()
  await page.coverage.startJSCoverage({ includeWorkers: true })
  const terminated = observe(doomed.waitForEvent('close'))
  await page.evaluate('actualWorker.terminate()')
  await terminated
  await assert.rejects(
    page.coverage.stopJSCoverage(),
    /detached before.*counters|context.*destroyed/i,
  )
  await settled()
  report.groups.push('actual worker termination explicitly reports unavailable engine counters')
  phase = 'real navigation resets old dedicated-worker scope'
  await createWorker()
  await page.coverage.startJSCoverage({ includeWorkers: true })
  await page.goto(origin + '/empty?new-document')
  assert.deepEqual(await page.coverage.stopJSCoverage(), [])
  await settled()
  report.groups.push(
    'genuine main-document navigation resets old worker scope without invented retained counts',
  )
  phase = 'actual worker Inspector interrupts busy author evaluation'
  const heldWorker = await createWorker()
  const announced = observe(heldWorker.waitForEvent('console', { timeout: 5000 }))
  let heldCompleted = false
  const held = observe(
    heldWorker
      .evaluate(
        '(()=>{console.log("actual-worker-coverage-hold");const until=performance.now()+1600;while(performance.now()<until){}return 71})()',
      )
      .then((value) => {
        heldCompleted = true
        return z.number().parse(value)
      }),
  )
  await announced
  page.setDefaultTimeout(120)
  const start = performance.now()
  await page.coverage.startJSCoverage({ includeWorkers: true })
  const elapsed = performance.now() - start
  assert(elapsed < 1200, String(elapsed))
  const during = await diagnostics()
  assert.equal(heldCompleted, false)
  assert(during.nativeWorkerPhysical > 0)
  assert.equal(during.nativeCoveragePhysical, 0)
  assert.equal(during.nativeCoverageRecordings, 2)
  report.workerInterrupt = {
    elapsed,
    workerPhysical: during.nativeWorkerPhysical,
    coveragePhysical: during.nativeCoveragePhysical,
  }
  assert.equal(await held, 71)
  page.setDefaultTimeout(30000)
  await page.coverage.stopJSCoverage()
  await settled()
  await page.coverage.startJSCoverage({ includeWorkers: true })
  await page.coverage.stopJSCoverage()
  await settled()
  report.groups.push(
    'genuine worker Profiler startup completes under original120ms budget while author evaluation remains physically pending, then clean stop/restart',
  )
  phase = 'unsupported worker CSS and navigation retention remain explicit'
  await assert.rejects(
    activeSource.runAutomation('coverage', {
      action: 'startCSS',
      includeWorkers: true,
      timeoutMs: 1000,
    }),
    /worker CSS is unsupported/i,
  )
  await assert.rejects(
    page.coverage.startJSCoverage({ includeWorkers: true, resetOnNavigation: false }),
    /retention.*not implemented|unsupported/i,
  )
  await settled()
  await page.goto(origin + '/empty?final')
  report.diagnostics = await settled()
  const end = performance.now() + 5000
  while (report.diagnostics.nativeWorkers > 0 && performance.now() < end) {
    report.diagnostics = await diagnostics()
  }
  assert.equal(report.diagnostics.nativeWorkers, 0)
  assert.equal(report.diagnostics.nativeWorkerHandles, 0)
  assert.equal(report.diagnostics.nativeWorkerPhysical, 0)
  assert.equal(report.diagnostics.nativeWorkerOperations, 0)
  assert.equal(report.diagnostics.nativeWorkerBootstraps, 0)
  report.groups.push(
    'worker CSS and non-resetting retention reject explicitly with actual zero final ownership',
  )
  report.result = 'pass'
} catch (error) {
  report.result = 'failed'
  report.phase = phase
  report.error = String(error)
  throw error
} finally {
  try {
    await source?.close()
    report.nativeShutdown = 'clean'
  } catch (error) {
    report.nativeShutdown = String(error)
  }
  await Promise.all(pendingOperations)
  report.observedFailures = observedFailures
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(directory + '/native-stderr.log', stderr)
  await writeFile(directory + '/report.json', JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
assert.deepEqual(observedFailures, [])
console.log('Actual native dedicated-worker JS coverage and document scope:9groups passed')
