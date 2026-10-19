// SPDX-License-Identifier: MIT
/* global document, HTMLElement */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import image from 'sharp'
import * as z from 'zod'

import {
  createNativePage,
  expectNative,
  launchNativeBrowser,
  NativeAssertionError,
  NativeBrowserContexts,
} from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-screenshot-expect',
)
await mkdir(directory, { recursive: true })
const diagnosticSchema = z.object({
  artifacts: z.number().int().nonnegative(),
  artifactPhysicalCapture: z.number().int().nonnegative(),
  nativeScreenshotComparisonLogical: z.number().int().nonnegative(),
  nativeScreenshotComparisonPhysical: z.number().int().nonnegative(),
  nativeScreenshotComparisonBytes: z.number().nonnegative(),
  nativeTraceBackgroundPhysical: z.number().int().nonnegative(),
})
/** @type {{result:string,binarySHA256:string,groups:string[],phase?:string,error?:string,nativeShutdown?:string,nativeStderrBytes?:number,diagnostics?:z.infer<typeof diagnosticSchema>,expectedFailures:string[],samples?:number}} */
const report = {
  result: 'running',
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  groups: [],
  expectedFailures: [],
}
/** @type {import('../dist/native.js').NativeBrowser|undefined} */
let source
let stderr = '',
  phase = 'launch'
/** @type {Promise<void>[]} */
const observed = []
/**
 * Observe every concurrent operation immediately, retaining its real receipt.
 * @template T
 * @param {Promise<T>} promise
 */
function observe(promise) {
  observed.push(
    promise.then(
      () => undefined,
      (error) => {
        report.expectedFailures.push(String(error))
      },
    ),
  )
  return promise
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 80,
    height: 60,
    deviceScaleFactor: 1,
    framesPerSecond: 120,
    headless: true,
    onDiagnostic: (text) => {
      stderr += text
    },
  })
  const active = source,
    page = createNativePage(active)
  const diagnostics = async () => diagnosticSchema.parse(await active.runAutomation('diagnostics'))
  /**
   * Poll actual native ownership, without controlling any native deadline.
   * @param {(value:z.infer<typeof diagnosticSchema>)=>boolean} predicate
   */
  const until = async (predicate) => {
    const end = performance.now() + 5000
    while (performance.now() < end) {
      const value = await diagnostics()
      if (predicate(value)) return value
    }
    throw Error('Actual native screenshot ownership did not reach expected state')
  }
  const empty = async () => {
    const value = await until(
      (value) =>
        value.artifacts === 0 &&
        value.artifactPhysicalCapture === 0 &&
        value.nativeScreenshotComparisonLogical === 0 &&
        value.nativeScreenshotComparisonPhysical === 0 &&
        value.nativeScreenshotComparisonBytes === 0 &&
        value.nativeTraceBackgroundPhysical === 0,
    )
    return value
  }
  const blue =
    '<style>html,body{margin:0;background:rgb(0,0,255)}#one{position:absolute;left:0;top:0;width:1px;height:1px}</style><div id=one></div>'
  await page.setContent(blue)
  const baseline = await page.screenshot({ animations: 'disabled', scale: 'css' })
  const metadata = await image(baseline).metadata()
  assert.equal(metadata.width, 80)
  assert.equal(metadata.height, 60)
  const path = join(directory, 'baseline.png')
  await writeFile(path, baseline)
  phase = 'explicit baseline path and genuine consecutive native PNG captures'
  await expectNative(page).toHaveScreenshot(path, { timeout: 0 })
  const sample = z
    .object({
      passed: z.literal(true),
      stable: z.literal(true),
      attempts: z.number().int().min(2),
      differentPixels: z.literal(0),
    })
    .parse(
      await page.runAutomation('artifact', {
        action: 'compareScreenshot',
        expected: baseline.toString('base64'),
        threshold: 0.2,
        isNot: false,
        timeoutMs: 3000,
        screenshot: {
          format: 'png',
          scale: 'css',
          animations: 'disabled',
          caret: 'hide',
          fullPage: false,
          omitBackground: false,
          maskColor: '#FF00FF',
        },
      }),
    )
  report.samples = sample.attempts
  await empty()
  report.groups.push(phase)
  phase = 'real one-pixel difference and strictest count/ratio tolerance'
  await page.evaluate(() => {
    const element = document.querySelector('#one')
    if (!(element instanceof HTMLElement)) throw Error('Actual pixel element missing')
    element.style.background = 'rgb(255,0,0)'
  })
  const changed = await page.screenshot({ animations: 'disabled', scale: 'css' }),
    raw = await image(changed).ensureAlpha().raw().toBuffer()
  assert.deepEqual([...raw.subarray(0, 4)], [255, 0, 0, 255])
  assert.deepEqual([...raw.subarray(4, 8)], [0, 0, 255, 255])
  await expectNative(page).toHaveScreenshot(baseline, {
    maxDiffPixels: 1,
    maxDiffPixelRatio: 1 / 4800,
  })
  await assert.rejects(
    expectNative(page).toHaveScreenshot(baseline, {
      maxDiffPixels: 1,
      maxDiffPixelRatio: 0,
      timeout: 300,
    }),
    (error) =>
      error instanceof NativeAssertionError && error.message.includes('Different pixels: 1/4800'),
  )
  await empty()
  report.groups.push(phase)
  phase = 'native-owned negation and original mismatch deadline'
  await expectNative(page).not.toHaveScreenshot(baseline)
  const start = performance.now()
  await assert.rejects(
    expectNative(page).not.toHaveScreenshot(changed, { timeout: 300 }),
    NativeAssertionError,
  )
  assert(performance.now() - start < 3000)
  await empty()
  report.groups.push(phase)
  phase = 'dimensions cannot be hidden by pixel tolerances and real diff artifacts decode'
  const square = await page.screenshot({
    clip: { x: 0, y: 0, width: 8, height: 8 },
    animations: 'disabled',
    scale: 'css',
  })
  const artifacts = join(directory, 'dimension-difference')
  await assert.rejects(
    expectNative(page).toHaveScreenshot(square, {
      clip: { x: 0, y: 0, width: 9, height: 8 },
      maxDiffPixels: 1000,
      maxDiffPixelRatio: 1,
      artifactsDir: artifacts,
      timeout: 300,
    }),
    (error) =>
      error instanceof NativeAssertionError && error.message.includes('Expected 8×8, received 9×8'),
  )
  const own = (await readdir(artifacts)).filter((name) => name.startsWith('screenshot-comparison-'))
  assert.equal(own.length, 1)
  const actual = await image(
      await readFile(join(artifacts, z.string().parse(own[0]), 'actual.png')),
    ).metadata(),
    diff = await image(
      await readFile(join(artifacts, z.string().parse(own[0]), 'diff.png')),
    ).metadata()
  assert.equal(actual.width, 9)
  assert.equal(actual.height, 8)
  assert.equal(diff.width, 9)
  assert.equal(diff.height, 8)
  await empty()
  report.groups.push(phase)
  phase = 'real locator geometry and genuine same-page mask ownership'
  await page.setContent(
    '<style>html,body{margin:0;background:blue}#target{position:absolute;left:7px;top:9px;width:12px;height:8px;background:green}#secret{position:absolute;left:0;top:0;width:4px;height:4px;background:red}</style><div id=target></div><div id=secret></div>',
  )
  const target = page.locator('#target'),
    locatorPNG = await target.screenshot({ scale: 'css', animations: 'disabled' })
  await expectNative(target).toHaveScreenshot(locatorPNG)
  const masked = await page.screenshot({
    scale: 'css',
    animations: 'disabled',
    mask: [page.locator('#secret')],
  })
  await page.evaluate(() => {
    const element = document.querySelector('#secret')
    if (!(element instanceof HTMLElement)) throw Error('Actual secret missing')
    element.style.background = 'yellow'
  })
  await expectNative(page).toHaveScreenshot(masked, { mask: [page.locator('#secret')] })
  await empty()
  report.groups.push(phase)
  phase = 'abort cancels an admitted native assertion and drains real capture and CPU counters'
  const cancel = new AbortController(),
    pending = observe(
      expectNative(page).toHaveScreenshot(baseline, { timeout: 0, signal: cancel.signal }),
    )
  await until((value) => value.nativeScreenshotComparisonLogical === 1)
  cancel.abort(new Error('actualScreenshotCancellation'))
  await assert.rejects(pending)
  await empty()
  report.groups.push(phase)
  phase = 'real child-page close drains comparison independently of initiating page'
  if (!active.runContext || !active.pageSource || !active.onContextEvent)
    throw Error('Actual native contexts unavailable')
  const contexts = new NativeBrowserContexts(
      Object.assign(active, {
        runContext: active.runContext.bind(active),
        pageSource: active.pageSource.bind(active),
        onContextEvent: active.onContextEvent.bind(active),
      }),
    ),
    context = await contexts.newContext(),
    child = await context.newPage()
  await child.setContent('<style>html,body{margin:0;background:red}</style>')
  const closed = observe(expectNative(child).toHaveScreenshot(baseline, { timeout: 5000 }))
  await until((value) => value.nativeScreenshotComparisonLogical === 1)
  await child.close()
  await assert.rejects(closed)
  await context.close()
  await empty()
  assert.equal(await page.evaluate('6*7'), 42)
  report.groups.push(phase)
  phase = 'untrusted IHDR dimensions rejected before native decoder allocation'
  const huge = Buffer.from(baseline)
  huge.writeUInt32BE(0x7fffffff, 16)
  await assert.rejects(expectNative(page).toHaveScreenshot(huge), /dimensions exceed/)
  await empty()
  report.groups.push(phase)
  assert.deepEqual(await readFile(path), baseline)
  report.diagnostics = await empty()
  report.result = 'pass'
} catch (error) {
  report.result = 'fail'
  report.phase = phase
  report.error = error instanceof Error ? (error.stack ?? error.message) : String(error)
} finally {
  try {
    await source?.close()
    report.nativeShutdown = 'clean'
  } catch (error) {
    report.nativeShutdown = String(error)
  }
  await Promise.all(observed)
  report.nativeStderrBytes = Buffer.byteLength(stderr)
  await writeFile(join(directory, 'native-stderr.log'), stderr)
  await writeFile(join(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.result, 'pass')
assert.equal(report.nativeShutdown, 'clean')
assert.equal(stderr, '')
assert.equal(report.expectedFailures.length, 2)
console.log(
  'Actual native screenshot explicit baselines/convergence/tolerances/negation/diffs/ownership:8groups passed',
)
