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
const directory = resolve(z.string().min(1).parse(process.env.SUITECUT_NATIVE_PROOF_DIRECTORY))
await mkdir(directory, { recursive: true })
const counters = z.object({
  sessions: z.number().int().nonnegative(),
  nativeWorkers: z.number().int().nonnegative(),
  nativeWorkerHandles: z.number().int().nonnegative(),
  nativeWorkerPhysical: z.number().int().nonnegative(),
  nativeWorkerOperations: z.number().int().nonnegative(),
  nativeWorkerBootstraps: z.number().int().nonnegative(),
  nativeCoveragePhysical: z.number().int().nonnegative(),
  nativeCoverageRecordings: z.number().int().nonnegative(),
  nativeCoverageBackendPhysical: z.number().int().nonnegative(),
})
const workerJS =
  'function actualNestedFrameWorker(){return 71;}actualNestedFrameWorker();const nested=new Worker("/nested.js");nested.onmessage=e=>postMessage(e.data);'
const nestedJS =
  'function actualNestedFrameLeaf(){return 73;}actualNestedFrameLeaf();postMessage(73);'
const server = createServer((request, response) => {
  const address = server.address()
  assert(address && typeof address !== 'string')
  const body =
    request.url === '/worker.js' ? workerJS : request.url === '/nested.js' ? nestedJS : undefined
  response.setHeader('Content-Type', body === undefined ? 'text/html' : 'text/javascript')
  response.end(
    body ??
      (request.url === '/parent'
        ? `<iframe src="http://localhost:${address.port}/outer"></iframe>`
        : request.url === '/outer'
          ? `<iframe src="http://127.0.0.2:${address.port}/inner"></iframe>`
          : '<body>Actual nested Inspector ownership</body>'),
  )
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
/** @type {import('../dist/native.js').NativeBrowser | undefined} */
let source
let stderr = ''
/** @type {Promise<void>[]} */
const receipts = []
/** @type {string[]} */
const failures = []
/**
 * Preserve both the original native operation and its eventual cleanup receipt.
 * @template T
 * @param {Promise<T>} operation The actual operation.
 * @returns {Promise<T>} The same operation.
 */
function observe(operation) {
  receipts.push(
    operation.then(
      () => undefined,
      (error) => {
        failures.push(String(error))
      },
    ),
  )
  return operation
}
/** @type {{result:string,binarySHA256:string,scriptSHA256:string,groups:string[],before?:z.infer<typeof counters>,after?:z.infer<typeof counters>,final?:z.infer<typeof counters>,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,failures?:string[]}} */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  scriptSHA256: createHash('sha256')
    .update(await readFile(new URL(import.meta.url)))
    .digest('hex'),
  groups: [],
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
  const actualSource = source
  const page = createNativePage(source)
  page.setDefaultTimeout(5000)
  const diagnostics = async () => counters.parse(await actualSource.runAutomation('diagnostics'))
  await page.goto(origin + '/parent')
  // Establish the real lazy native controller and retire its domain ACKs before
  // requiring its counters; absence before creation is not a measured zero.
  await page.coverage.startJSCoverage()
  await page.coverage.stopJSCoverage()
  report.before = await diagnostics()
  assert(
    report.before.sessions >= 2,
    'Both genuine cross-origin iframe Inspector sessions must exist',
  )
  const unrelatedWait = observe(page.waitForEvent('worker'))
  await page.evaluate(
    'globalThis.unrelatedWorker=new Worker(URL.createObjectURL(new Blob(["onmessage=()=>postMessage(42)"],{type:"text/javascript"})))',
  )
  const unrelated = await unrelatedWait
  const inner = page
    .frames()
    .find((frame) => frame.url() === `http://127.0.0.2:${address.port}/inner`)
  assert(inner)
  await page.coverage.startJSCoverage({ includeWorkers: true })
  assert.equal(
    await inner.evaluate(
      'new Promise(resolve=>{globalThis.actualWorker=new Worker("/worker.js");actualWorker.onmessage=e=>resolve(e.data)})',
    ),
    73,
  )
  const entries = await page.coverage.stopJSCoverage()
  for (const [sourceText, name] of [
    [workerJS, 'actualNestedFrameWorker'],
    [nestedJS, 'actualNestedFrameLeaf'],
  ]) {
    const entry = entries.find((value) => value.source === sourceText)
    assert(entry)
    assert.equal(entry.functions.find((value) => value.functionName === name)?.ranges[0]?.count, 1)
  }
  const descendants = page.workers().filter((worker) => worker !== unrelated)
  assert.equal(descendants.length, 2)
  const closed = descendants.map((worker) => observe(worker.waitForEvent('close')))
  await page.evaluate('document.querySelector("iframe").remove()')
  await Promise.all(closed)
  const deadline = performance.now() + 5000
  do {
    report.after = await diagnostics()
  } while (
    (report.after.sessions !== 0 || report.after.nativeWorkers !== 1) &&
    performance.now() < deadline
  )
  assert.equal(report.after.sessions, 0)
  assert.equal(report.after.nativeWorkers, 1)
  assert.equal(await unrelated.evaluate('6*7'), 42)
  assert.deepEqual(page.workers(), [unrelated])
  report.groups.push(
    'Actual iframe→iframe→worker→worker closure retires only genuine descendants; unrelated live worker remains usable',
  )
  await page.goto(origin + '/empty')
  const finalDeadline = performance.now() + 5000
  do {
    report.final = await diagnostics()
  } while (report.final.nativeWorkers !== 0 && performance.now() < finalDeadline)
  /** @type {(keyof z.infer<typeof counters>)[]} */
  const ownedKeys = [
    'nativeWorkers',
    'nativeWorkerHandles',
    'nativeWorkerPhysical',
    'nativeWorkerOperations',
    'nativeWorkerBootstraps',
    'nativeCoveragePhysical',
    'nativeCoverageRecordings',
    'nativeCoverageBackendPhysical',
  ]
  for (const key of ownedKeys) assert.equal(report.final[key], 0, key)
  report.groups.push(
    'Actual navigation completes exact channel retirement with zero worker and coverage ownership',
  )
  report.result = 'pass'
} catch (error) {
  report.result = 'failed'
  report.error = String(error)
  throw error
} finally {
  try {
    await source?.close()
    report.nativeShutdown = 'clean'
  } catch (error) {
    report.nativeShutdown = String(error)
  }
  await Promise.all(receipts)
  report.failures = failures
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(directory + '/native-stderr.log', stderr)
  await writeFile(directory + '/report.json', JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
assert.deepEqual(failures, [])
console.log('Actual nested iframe/worker session closure:2groups passed')
