// SPDX-License-Identifier: MIT
/* global document, getComputedStyle */
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

import { launchNativeBrowser, createNativePage, NativeTimeoutError } from '../dist/native.js'

const binary = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-coverage-ownership',
)
await mkdir(directory, { recursive: true })
const counterSchema = z.object({
  nativeCoveragePhysical: z.number().int().nonnegative(),
  nativeCoverageRecordings: z.number().int().nonnegative(),
  nativeCoverageQueuedSources: z.number().int().nonnegative(),
  nativeCoverageSourceBytes: z.number().nonnegative(),
  sessions: z.number().int().nonnegative(),
})
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  const address = server.address()
  assert(address && typeof address !== 'string')
  response.end(
    request.url === '/oopif'
      ? `<iframe src="http://localhost:${address.port}/child"></iframe>`
      : request.url === '/child'
        ? '<script>function genuineChild(){return 7;}genuineChild();</script>'
        : '<div>Actual source units</div>',
  )
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`
/**
 * Actual helper endpoint, retained for unconditional cleanup.
 * @type {import('../dist/native.js').NativeBrowser | undefined}
 */
let source
let stderr = '',
  phase = 'launch'
/**
 * Exact native measurements and cleanup evidence.
 * @type {{result:string,binarySHA256:string,groups:string[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,observedFailures?:string[],expectedCancellation?:string,scripts?:import("../dist/native.js").NativeJSCoverageEntry[],styles?:import("../dist/native.js").NativeCSSCoverageEntry[],diagnostics?:z.infer<typeof counterSchema>,timeout?:{elapsed:number,physical:number,settled:number}}}
 */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(binary))
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
    executablePath: binary,
    width: 320,
    height: 240,
    headless: true,
    onDiagnostic: (m) => (stderr += m),
  })
  const page = createNativePage(source)
  const activeSource = source
  const settled = async () => {
    const end = performance.now() + 3000
    while (performance.now() < end) {
      const d = counterSchema.parse(await activeSource.runAutomation('diagnostics'))
      if (d.nativeCoveragePhysical === 0 && d.nativeCoverageRecordings === 0) {
        return d
      }
    }
    throw Error('Native physical coverage cleanup failed to settle')
  }
  await page.goto(origin + '/')
  phase = 'actual UTF16 JS source from real Debugger.getScriptSource'
  const js =
    'function actualUnits(){return 1;}actualUnits();\n//' +
    String.fromCharCode(0xd800) +
    ' __suitecutNativeUTF16Scalar\n//# sourceURL=actual-units.js'
  await page.coverage.startJSCoverage()
  await page.evaluate(() => {
    const script = document.createElement('script')
    script.textContent =
      'function actualUnits(){return 1;}actualUnits();\n//' +
      String.fromCharCode(0xd800) +
      ' __suitecutNativeUTF16Scalar\n//# sourceURL=actual-units.js'
    document.body.appendChild(script)
  })
  const scripts = await page.coverage.stopJSCoverage()
  const actual = scripts.find((e) => e.url === 'actual-units.js')
  assert(actual)
  assert.equal(actual.source, js)
  assert.equal(actual.source.charCodeAt(js.indexOf(String.fromCharCode(0xd800))), 0xd800)
  report.scripts = scripts
  report.groups.push('actual isolated JS UTF16 source and literal private-tag text')
  phase = 'actual UTF16 CSS source from real CSS.getStyleSheetText'
  const css =
    'div {color:rgb(3,4,5)}\n/*' +
    String.fromCharCode(0xdfff) +
    ' __suitecutNativeUTF16Scalar */\n/*# sourceURL=actual-units.css */'
  await page.coverage.startCSSCoverage()
  await page.evaluate(() => {
    const style = document.createElement('style')
    style.textContent =
      'div {color:rgb(3,4,5)}\n/*' +
      String.fromCharCode(0xdfff) +
      ' __suitecutNativeUTF16Scalar */\n/*# sourceURL=actual-units.css */'
    document.head.appendChild(style)
    const target = document.querySelector('div')
    if (!target) throw new Error('Actual coverage div absent')
    return getComputedStyle(target).color
  })
  const styles = await page.coverage.stopCSSCoverage()
  const actualCSS = styles.find((e) => e.url === 'actual-units.css')
  assert(actualCSS)
  assert.equal(actualCSS.text, css)
  assert(actualCSS.ranges.length > 0)
  report.styles = styles
  report.groups.push('actual isolated CSS UTF16 source and used rule range')
  phase = 'genuine V8 function-wide option'
  await page.coverage.startJSCoverage({ useBlockCoverage: false })
  await page.evaluate(() => {
    const script = document.createElement('script')
    script.textContent =
      'function actualBranch(x){if(x)return 1;return 2;}actualBranch(true);\n//# sourceURL=actual-branch.js'
    document.body.appendChild(script)
  })
  const functions = await page.coverage.stopJSCoverage()
  assert(
    functions
      .find((e) => e.url === 'actual-branch.js')
      ?.functions.some(
        (f) => f.functionName === 'actualBranch' && !f.isBlockCoverage && f.ranges.length === 1,
      ),
  )
  report.groups.push('genuine function-wide V8 coverage option')
  phase = 'held renderer original timeout and late physical startup ACK'
  let ready = observe(page.waitForEvent('console'))
  let held = observe(
    page.evaluate(() => {
      console.log('actual-held-timeout')
      const end = performance.now() + 1600
      while (performance.now() < end) {
        void 0
      }
      return 42
    }),
  )
  await ready
  page.setDefaultTimeout(120)
  const start = performance.now()
  await assert.rejects(page.coverage.startJSCoverage(), (error) => {
    assert(
      error instanceof NativeTimeoutError,
      'Coverage deadline must retain the public NativeTimeoutError class: ' + String(error),
    )
    assert.equal(error.name, 'TimeoutError')
    assert.match(error.message, /timeout|timed out|deadline/i)
    return true
  })
  const elapsed = performance.now() - start
  assert(elapsed >= 90 && elapsed < 1200, String(elapsed))
  const physical = counterSchema.parse(await source.runAutomation('diagnostics'))
  assert(physical.nativeCoveragePhysical > 0)
  assert.equal(physical.nativeCoverageRecordings, 1)
  await assert.rejects(page.coverage.startJSCoverage(), /already enabled|cleanup/i)
  assert.equal(await held, 42)
  const afterTimeout = await settled()
  page.setDefaultTimeout(30000)
  report.timeout = {
    elapsed,
    physical: physical.nativeCoveragePhysical,
    settled: afterTimeout.nativeCoveragePhysical,
  }
  report.groups.push(
    'original120ms logical timeout retains real pending debugger ACK until renderer release',
  )
  phase = 'actual explicit cancel keeps physical ownership'
  ready = observe(page.waitForEvent('console'))
  held = observe(
    page.evaluate(() => {
      console.log('actual-held-cancel')
      const end = performance.now() + 1600
      while (performance.now() < end) {
        void 0
      }
      return 43
    }),
  )
  await ready
  const controller = new AbortController()
  const canceled = page.runAutomation(
    'coverage',
    { action: 'startJS', timeoutMs: 5000 },
    controller.signal,
  )
  pendingOperations.push(
    canceled.then(
      () => undefined,
      (error) => {
        report.expectedCancellation = String(error)
      },
    ),
  )
  const observed = counterSchema.parse(await source.runAutomation('diagnostics'))
  assert(observed.nativeCoveragePhysical > 0)
  controller.abort(new Error('actual coverage caller canceled'))
  await assert.rejects(canceled, /actual coverage caller canceled/)
  const retained = counterSchema.parse(await source.runAutomation('diagnostics'))
  assert(retained.nativeCoveragePhysical > 0)
  assert.equal(await held, 43)
  await settled()
  await page.coverage.startJSCoverage()
  await page.coverage.stopJSCoverage()
  report.groups.push('explicit caller cancel retains backend then allows real restart')
  phase = 'actual future OOPIF domains and child source ownership'
  await page.coverage.startJSCoverage()
  await page.goto(origin + '/oopif')
  assert(counterSchema.parse(await source.runAutomation('diagnostics')).sessions > 0)
  const childCoverage = await page.coverage.stopJSCoverage()
  const childScript = childCoverage.find((entry) => entry.url.endsWith('/child'))
  assert(childScript)
  assert.equal(childScript.source, 'function genuineChild(){return 7;}genuineChild();')
  assert.equal(
    childScript.functions.find((entry) => entry.functionName === 'genuineChild')?.ranges[0]?.count,
    1,
  )
  await settled()
  await page.coverage.startCSSCoverage()
  assert.deepEqual(await page.coverage.stopCSSCoverage(), [])
  report.groups.push('actual future OOPIF child count1 and real JS/CSS domain cleanup')
  report.diagnostics = await settled()
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
  } finally {
    await Promise.all(pendingOperations)
    report.observedFailures = observedFailures
    server.closeAllConnections()
    await new Promise((resolve) => server.close(resolve))
    report.nativeStderrBytes = Buffer.byteLength(stderr)
    await writeFile(directory + '/native-stderr.log', stderr)
    await writeFile(directory + '/report.json', JSON.stringify(report, null, 2))
  }
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
assert.deepEqual(observedFailures, [])
console.log(JSON.stringify(report))
