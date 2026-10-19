// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as timer } from 'node:timers/promises'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(z.string().min(1).parse(process.env.SUITECUT_NATIVE_PROOF_DIRECTORY))
await mkdir(directory, { recursive: true })
const counters = z.object({
  sessions: z.number().int().nonnegative(),
  rendererGeneration: z.number().int().nonnegative(),
  nativeWorkers: z.number().int().nonnegative(),
  nativeWorkerHandles: z.number().int().nonnegative(),
  nativeWorkerOperations: z.number().int().nonnegative(),
  nativeWorkerPhysical: z.number().int().nonnegative(),
  nativeWorkerBootstraps: z.number().int().nonnegative(),
  nativeCoveragePhysical: z.number().int().nonnegative(),
  nativeCoverageRecordings: z.number().int().nonnegative(),
  nativeCoverageQueuedSources: z.number().int().nonnegative(),
  nativeCoverageSourceBytes: z.number().nonnegative(),
  nativeCoverageSharedPayloadBytes: z.number().nonnegative(),
  nativeCoverageBackendPhysical: z.number().int().nonnegative(),
})
const generations = { root: 0, child: 0 }
/** @type {Map<string,{value:number,source:string,css:string}>} */
const documents = new Map()
/** @type {Map<string,string>} */
const workerBodies = new Map()
const server = createServer((request, response) => {
  response.setHeader('Cache-Control', 'no-store')
  const address = server.address()
  assert(address && typeof address !== 'string')
  if (request.url === '/parent' || request.url === '/child') {
    const kind = request.url === '/parent' ? 'root' : 'child'
    const generation = ++generations[kind]
    const value = (kind === 'root' ? 1000 : 2000) + generation * 100 + 77
    const source = `function ${kind}AfterRecovery(){return ${value};}${kind}AfterRecovery();`
    const css = `div{color:rgb(${(kind === 'root' ? 10 : 30) + generation},22,33)}`
    documents.set(kind, { value, source, css })
    response.setHeader('Content-Type', 'text/html')
    response.end(
      `<style>${css}</style><div>actual ${kind} document ${generation}</div><script>${source}</script>` +
        (kind === 'root' ? `<iframe src="http://localhost:${address.port}/child"></iframe>` : ''),
    )
    return
  }
  if (request.url?.startsWith('/worker-')) {
    const value = 3000 + generations.child * 100
    const body = `function actualRecoveredWorker(){return ${value};}actualRecoveredWorker();onmessage=()=>postMessage(actualRecoveredWorker());postMessage(${value});`
    workerBodies.set(request.url, body)
    response.setHeader('Content-Type', 'text/javascript')
    response.end(body)
    return
  }
  response.writeHead(204)
  response.end()
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
/** @type {Promise<void>[]} */
const receipts = []
/** @type {string[]} */
const failures = []
/**
 * Retain a completion receipt immediately without changing the actual operation.
 * @template T
 * @param {Promise<T>} operation The native operation.
 * @returns {Promise<T>} The same native operation.
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
/**
 * Bound a genuine operation under this authored verifier's observation deadline.
 * @template T
 * @param {Promise<T>} operation The real native operation.
 * @returns {Promise<T>} Its real result.
 */
async function bounded(operation) {
  const timerOwner = new AbortController()
  try {
    return await Promise.race([
      operation,
      timer(10000, undefined, { signal: timerOwner.signal }).then(() => {
        throw new Error('Actual recovery proof observation deadline exceeded')
      }),
    ])
  } finally {
    timerOwner.abort()
  }
}
/** @type {{result:string,binarySHA256:string,scriptSHA256:string,scope:string,cycles:Record<string,unknown>[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,failures?:string[]}} */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  scriptSHA256: createHash('sha256')
    .update(await readFile(new URL(import.meta.url)))
    .digest('hex'),
  scope:
    'Actual context child page main-renderer crash/reload while genuine OOPIF JavaScript/CSS and its dedicated-worker JS domains are active; initial helper root remains alive',
  cycles: [],
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 320,
    height: 220,
    headless: true,
    onDiagnostic: (text) => {
      stderr += text
    },
  })
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Actual native context transport is unavailable')
  const contexts = new NativeBrowserContexts(
    Object.assign(source, {
      runContext: source.runContext.bind(source),
      pageSource: source.pageSource.bind(source),
      onContextEvent: source.onContextEvent.bind(source),
    }),
  )
  const context = await contexts.newContext()
  const page = await context.newPage()
  page.setDefaultTimeout(5000)
  const identity = page.source
  const diagnostics = async () => counters.parse(await page.source.runAutomation('diagnostics'))
  /**
   * Create a real OOPIF-owned worker, retaining its native publication receipt.
   * @param {import('../dist/native.js').NativeFrame} frame The actual child frame.
   * @param {string} url The genuine fixture script URL.
   * @returns {Promise<import('../dist/native.js').NativeWorker>} The actual worker.
   */
  async function createWorker(frame, url) {
    const published = observe(page.waitForEvent('worker', { timeout: 5000 }))
    const value = z
      .number()
      .parse(
        await frame.evaluate(
          `new Promise(resolve=>{globalThis.actualWorker=new Worker(${JSON.stringify(url)});actualWorker.onmessage=event=>resolve(event.data)})`,
        ),
      )
    assert.equal(value, 3000 + generations.child * 100)
    return await published
  }
  await page.goto(origin + '/parent')
  for (let cycle = 1; cycle <= 2; ++cycle) {
    phase = `actual renderer recovery cycle${cycle}`
    const oldChild = page.frames().find((frame) => frame.url() === childOrigin + '/child')
    assert(oldChild)
    const oldWorker = await createWorker(oldChild, `/worker-before-${cycle}.js`)
    await page.coverage.startJSCoverage({ includeWorkers: true })
    await page.coverage.startCSSCoverage()
    const before = await diagnostics()
    assert(before.sessions > 0)
    assert.equal(before.nativeCoverageRecordings, 5)
    const beforeRoot = z.number().parse(await page.evaluate('rootAfterRecovery()'))
    const beforeChild = z.number().parse(await oldChild.evaluate('childAfterRecovery()'))
    const beforeWorker = z.number().parse(await oldWorker.evaluate('actualRecoveredWorker()'))
    const oldSignal = page.source.signal
    const oldFailure = observe(page.source.failure.catch((error) => String(error)))
    const crashWait = observe(page.waitForEvent('crash', { timeout: 5000 }))
    const actualCrash = observe(
      page.source.sendDevToolsCommand('Page.crash').then(
        (value) => JSON.stringify(value),
        (error) => String(error),
      ),
    )
    assert.equal(await bounded(crashWait), page)
    assert(oldSignal.aborted)
    assert.equal(page.source.pageSignal?.aborted, false)
    const failure = await bounded(oldFailure)
    const crash = await bounded(actualCrash)
    const afterCrash = await page.source.runAutomation('diagnostics')
    const response = await bounded(page.reload())
    assert.equal(response?.status(), 200)
    assert.equal(page.source, identity)
    assert.equal(page.source.state, 'running')
    assert.notEqual(page.source.signal, oldSignal)
    assert(!page.source.signal.aborted)
    assert(oldSignal.aborted)
    const currentChild = page.frames().find((frame) => frame.url() === childOrigin + '/child')
    assert(currentChild)
    const currentURL = `/worker-after-${cycle}.js`
    const currentWorker = await createWorker(currentChild, currentURL)
    const currentRoot = z.number().parse(await page.evaluate('rootAfterRecovery()'))
    const currentChildValue = z.number().parse(await currentChild.evaluate('childAfterRecovery()'))
    const currentWorkerValue = z
      .number()
      .parse(await currentWorker.evaluate('actualRecoveredWorker()'))
    assert.notEqual(currentRoot, beforeRoot)
    assert.notEqual(currentChildValue, beforeChild)
    assert.notEqual(currentWorkerValue, beforeWorker)
    const afterReload = await diagnostics()
    assert(afterReload.rendererGeneration > before.rendererGeneration)
    assert(afterReload.sessions > 0)
    assert.equal(afterReload.nativeCoverageRecordings, 5)
    const css = await page.coverage.stopCSSCoverage()
    const js = await page.coverage.stopJSCoverage()
    for (const kind of ['root', 'child']) {
      const expected = documents.get(kind)
      assert(expected)
      const url = kind === 'root' ? origin + '/parent' : childOrigin + '/child'
      const actualJS = js.find((entry) => entry.url === url)
      assert(actualJS)
      assert.equal(actualJS.source, expected.source)
      assert.equal(
        actualJS.functions.find((fn) => fn.functionName === kind + 'AfterRecovery')?.ranges[0]
          ?.count,
        2,
      )
      const actualCSS = css.find((entry) => entry.url === url)
      assert(actualCSS)
      assert.equal(actualCSS.text, expected.css)
      assert(actualCSS.ranges.length > 0)
    }
    const workerEntry = js.find((entry) => entry.url === childOrigin + currentURL)
    assert(workerEntry)
    assert.equal(workerEntry.source, workerBodies.get(currentURL))
    assert.equal(
      workerEntry.functions.find((fn) => fn.functionName === 'actualRecoveredWorker')?.ranges[0]
        ?.count,
      2,
    )
    assert(!js.some((entry) => entry.url.includes('/worker-before-')))
    await assert.rejects(oldWorker.evaluate('42'), /closed|unavailable|destroyed|terminated/iu)
    const afterStop = await diagnostics()
    /** @type {(keyof z.infer<typeof counters>)[]} */
    const profileKeys = [
      'nativeCoveragePhysical',
      'nativeCoverageRecordings',
      'nativeCoverageQueuedSources',
      'nativeCoverageSourceBytes',
      'nativeCoverageSharedPayloadBytes',
      'nativeCoverageBackendPhysical',
    ]
    for (const key of profileKeys) assert.equal(afterStop[key], 0, key)
    const terminated = observe(currentWorker.waitForEvent('close'))
    await currentChild.evaluate('actualWorker.terminate()')
    await terminated
    const afterTerminate = await diagnostics()
    /** @type {(keyof z.infer<typeof counters>)[]} */
    const workerKeys = [
      'nativeWorkers',
      'nativeWorkerHandles',
      'nativeWorkerOperations',
      'nativeWorkerPhysical',
      'nativeWorkerBootstraps',
    ]
    for (const key of workerKeys) assert.equal(afterTerminate[key], 0, key)
    report.cycles.push({
      cycle,
      before,
      beforeRoot,
      beforeChild,
      beforeWorker,
      failure,
      crash,
      afterCrash,
      afterReload,
      currentRoot,
      currentChild: currentChildValue,
      currentWorker: currentWorkerValue,
      js,
      css,
      afterStop,
      afterTerminate,
    })
    await writeFile(directory + '/report.json', JSON.stringify(report, null, 2) + '\n')
  }
  await context.close()
  const root = z
    .object({ result: z.object({ value: z.number() }) })
    .parse(await source.runAutomation('evaluate', { expression: '6*7' }))
  assert.equal(root.result.value, 42)
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
console.log('Actual OOPIF and dedicated-worker coverage renderer recovery:2cycles passed')
