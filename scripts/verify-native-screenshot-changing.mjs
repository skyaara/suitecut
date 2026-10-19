// SPDX-License-Identifier: MIT
/* global document, requestAnimationFrame */
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
} from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-screenshot-changing',
)
await mkdir(directory, { recursive: true })
const stateSchema = z.object({
  artifacts: z.number().int().nonnegative(),
  artifactPhysicalCapture: z.number().int().nonnegative(),
  nativeScreenshotComparisonLogical: z.number().int().nonnegative(),
  nativeScreenshotComparisonPhysical: z.number().int().nonnegative(),
  nativeScreenshotComparisonBytes: z.number().nonnegative(),
  nativeTraceBackgroundPhysical: z.number().int().nonnegative(),
})
const mismatchSchema = z.object({
  passed: z.literal(false),
  stable: z.literal(false),
  timedOut: z.literal(true),
  attempts: z.number().int().min(2),
  differentPixels: z.literal(4800),
  actual: z.string().min(1),
  diff: z.string().min(1),
})
/** @type {{completed:boolean,binarySHA256:string,groups:string[],state?:z.infer<typeof stateSchema>,samples?:number,error?:string,shutdown?:string,stderrBytes?:number}} */
const report = {
  completed: false,
  binarySHA256: createHash('sha256')
    .update(await readFile(executable))
    .digest('hex'),
  groups: [],
}
/** @type {import('../dist/native.js').NativeBrowser|undefined} */
let source
let stderr = ''
/**
 * Inspect genuine PNG pixels, independently of the native mismatch metadata.
 * @param {Buffer} actual
 * @param {Buffer} diff
 */
async function checkImages(actual, diff) {
  const actualImage = image(actual),
    diffImage = image(diff)
  for (const decoder of [actualImage, diffImage]) {
    const metadata = await decoder.metadata()
    assert.equal(metadata.width, 80)
    assert.equal(metadata.height, 60)
  }
  const pixels = await actualImage.ensureAlpha().raw().toBuffer()
  const difference = await diffImage.ensureAlpha().raw().toBuffer()
  for (let offset = 0; offset < pixels.length; offset += 4) {
    // Every real author frame is opaque red/green, unlike the blue baseline.
    assert.equal(pixels[offset + 2], 0)
    assert.equal(pixels[offset + 3], 255)
    assert.deepEqual([...difference.subarray(offset, offset + 4)], [255, 0, 0, 255])
  }
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    width: 80,
    height: 60,
    framesPerSecond: 120,
    deviceScaleFactor: 1,
    headless: true,
    onDiagnostic: (text) => {
      stderr += text
    },
  })
  const active = source,
    page = createNativePage(active)
  const empty = async () => {
    const end = performance.now() + 5000
    while (performance.now() < end) {
      const state = stateSchema.parse(await active.runAutomation('diagnostics'))
      if (Object.values(state).every((value) => value === 0)) return state
    }
    throw Error('Real screenshot comparison/capture receipts did not settle')
  }
  await page.setContent('<style>html,body{margin:0;background:rgb(0,0,255)}</style>')
  const baseline = await page.screenshot({ animations: 'disabled', scale: 'css' })
  const baselinePath = join(directory, 'baseline.png')
  await writeFile(baselinePath, baseline)
  await page.evaluate(() => {
    let frame = 0
    function draw() {
      ++frame
      document.documentElement.style.background = `rgb(${1 + (frame % 200)},${Math.floor(frame / 200) % 200},0)`
      document.body.style.background = 'transparent'
      requestAnimationFrame(draw)
    }
    requestAnimationFrame(draw)
  })
  const result = mismatchSchema.parse(
    await page.runAutomation('artifact', {
      action: 'compareScreenshot',
      expected: baseline.toString('base64'),
      threshold: 0,
      isNot: false,
      timeoutMs: 1200,
      screenshot: {
        format: 'png',
        scale: 'css',
        animations: 'allow',
        caret: 'hide',
        fullPage: false,
        omitBackground: false,
        maskColor: '#FF00FF',
      },
    }),
  )
  await checkImages(Buffer.from(result.actual, 'base64'), Buffer.from(result.diff, 'base64'))
  report.samples = result.attempts
  report.state = await empty()
  report.groups.push('Real unstable timeout retains a completed baseline/actual/diff pair')

  const artifacts = join(directory, 'artifacts')
  await assert.rejects(
    expectNative(page).toHaveScreenshot(baselinePath, {
      timeout: 1200,
      threshold: 0,
      animations: 'allow',
      artifactsDir: artifacts,
    }),
    (error) =>
      error instanceof NativeAssertionError &&
      error.message.includes('consecutive decoded pixels stable: false') &&
      error.message.includes('Different pixels: 4800/4800') &&
      error.message.includes('Difference: '),
  )
  const outputs = (await readdir(artifacts)).filter((name) =>
    name.startsWith('screenshot-comparison-'),
  )
  assert.equal(outputs.length, 1)
  const own = join(artifacts, z.string().parse(outputs[0]))
  await checkImages(await readFile(join(own, 'actual.png')), await readFile(join(own, 'diff.png')))
  assert.deepEqual(await readFile(baselinePath), baseline)
  report.state = await empty()
  report.groups.push(
    'Public matcher writes genuine unstable mismatch artifacts without updating baseline',
  )
  report.completed = true
} catch (error) {
  report.error = error instanceof Error ? (error.stack ?? error.message) : String(error)
} finally {
  try {
    await source?.close()
    report.shutdown = 'clean'
  } catch (error) {
    report.shutdown = String(error)
  }
  report.stderrBytes = Buffer.byteLength(stderr)
  await writeFile(join(directory, 'native-stderr.log'), stderr)
  await writeFile(join(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.completed, true)
assert.equal(report.shutdown, 'clean')
assert.equal(stderr, '')
console.log('Real native unstable screenshot timeout/diff artifacts:2groups passed')
