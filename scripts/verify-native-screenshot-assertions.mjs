// SPDX-License-Identifier: MIT
// Genuine Source capture/retirement and native filesystem publication. This
// script never supplies comparison observations or installs a soft bridge.
/* global document */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { readFile, readdir, mkdir, writeFile, stat } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

import image from 'sharp'
import * as z from 'zod'

const sdk = process.env.SUITECUT_NATIVE_SDK_DIRECTORY
const callable = z.custom((value) => typeof value === 'function')
const loaded = z
  .object({
    launchNativeBrowser: callable,
    createNativePage: callable,
    expectNative: callable,
    NativeAssertionError: callable,
  })
  .parse(await import(sdk ? pathToFileURL(join(sdk, 'native.js')).href : '../dist/native.js'))
const api = /** @type {typeof import('../src/native.js')} */ (loaded)
const { launchNativeBrowser, createNativePage, expectNative, NativeAssertionError } = api
const executable = process.env.SUITECUT_NATIVE_EXECUTABLE
assert(executable)
const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/screenshot-assertions',
)
await mkdir(directory, { recursive: true })
/** @type {import('../src/native.js').NativeBrowser|undefined} */
let source
let diagnostic = '',
  frames = 0,
  frameBytes = 0
/** @type {Record<string, import('../src/untrusted.js').UntrustedInput>} */
const report = { completed: false, groups: [], metrics: [] }
/** @type {string[]} */
const groups = []
/** @type {Record<string, string|number>[]} */
const metrics = []
const fields = [
  'artifacts',
  'artifactPhysicalCapture',
  'nativeScreenshotComparisonLogical',
  'nativeScreenshotComparisonPhysical',
  'nativeScreenshotComparisonBytes',
  'nativeScreenshotAssertionScopes',
  'nativeScreenshotAssertionPhysical',
  'nativeSelectorWaits',
  'nativeSelectorWaitPhysical',
]
/** Read real Linux process CPU counters. No ownership is inferred from names. */
async function helperIdentity() {
  const children = []
  for (const entry of await readdir('/proc')) {
    if (!/^\d+$/.test(entry)) continue
    try {
      const raw = await readFile(`/proc/${entry}/stat`, 'utf8')
      const row = raw
        .slice(raw.lastIndexOf(')') + 1)
        .trim()
        .split(/\s+/)
      if (Number(row[1]) === process.pid) children.push({ pid: Number(entry), start: row[19] })
    } catch (error) {
      if (
        !(error instanceof Error) ||
        !('code' in error) ||
        (error.code !== 'ENOENT' && error.code !== 'ESRCH')
      )
        throw error
    }
  }
  assert.equal(children.length, 1)
  return z.object({ pid: z.number().int().positive(), start: z.string() }).parse(children[0])
}
try {
  source = await launchNativeBrowser({
    executablePath: executable,
    headless: true,
    width: 80,
    height: 60,
    framesPerSecond: 60,
    onDiagnostic: (text) => {
      diagnostic += text
    },
  })
  const active = source,
    page = createNativePage(active),
    helper = await helperIdentity()
  report.helper = helper
  active.onFrame((frame) => {
    ++frames
    frameBytes += frame.data.byteLength
  })
  const cpu = async () => {
    const raw = await readFile(`/proc/${helper.pid}/stat`, 'utf8')
    const row = raw
      .slice(raw.lastIndexOf(')') + 1)
      .trim()
      .split(/\s+/)
    assert.equal(row[19], helper.start)
    return Number(row[11]) + Number(row[12])
  }
  const empty = async () => {
    const end = performance.now() + 5000
    do {
      const actual = z.record(z.string(), z.json()).parse(await active.runAutomation('diagnostics'))
      if (fields.every((key) => actual[key] === 0)) return actual
    } while (performance.now() < end)
    throw Error('Actual screenshot/selector receipts did not retire')
  }
  /**
   * Measure the actual native pass or mismatch path.
   * @param {string} name Sample description.
   * @param {()=>Promise<void>} callback Actual operation.
   */
  const measure = async (name, callback) => {
    const started = performance.now(),
      ticks = await cpu(),
      count = frames,
      bytes = frameBytes
    await callback()
    metrics.push({
      name,
      elapsedMs: performance.now() - started,
      nativeProcessCPUClockTicks: (await cpu()) - ticks,
      actualIPCFrames: frames - count,
      actualIPCFrameBytes: frameBytes - bytes,
    })
  }
  await page.setContent(
    '<style>html,body{margin:0;background:rgb(0,0,255)}</style><div id="target"></div>',
  )
  const before = await page.evaluate(() => document.documentElement.outerHTML)
  const baseline = await page.screenshot({ scale: 'css', animations: 'disabled' })
  const baselinePath = join(directory, 'baseline.png')
  await writeFile(baselinePath, baseline)
  await measure('matching explicit native baseline', async () => {
    await expectNative(page).toHaveScreenshot(baselinePath, {
      timeout: 2000,
      scale: 'css',
      style: '#target{opacity:1}',
    })
  })
  assert.equal(await page.evaluate(() => document.documentElement.outerHTML), before)
  await empty()
  groups.push('Real stable pass restores root styles and retires actual work')

  await page.evaluate(() => {
    document.body.style.background = 'rgb(255,0,0)'
  })
  await expectNative(page).not.toHaveScreenshot(baselinePath, { timeout: 2000, scale: 'css' })
  await empty()
  groups.push('Negated assertion compares actual different pixels')
  const artifacts = join(directory, 'artifacts')
  await measure('observed mismatch with native atomic PNG publication', async () => {
    await assert.rejects(
      expectNative(page, 'Actual capture context').toHaveScreenshot(baselinePath, {
        timeout: 300,
        scale: 'css',
        artifactsDir: artifacts,
      }),
      (error) =>
        error instanceof NativeAssertionError &&
        error.message.startsWith('Actual capture context\n\n') &&
        error.message.includes('Different pixels:'),
    )
  })
  const outputs = await readdir(artifacts)
  assert.equal(outputs.length, 1)
  const own = z.string().parse(outputs[0])
  const actualPath = join(artifacts, own, 'actual.png'),
    diffPath = join(artifacts, own, 'diff.png')
  const actual = await readFile(actualPath),
    diff = await readFile(diffPath)
  for (const png of [actual, diff]) {
    const decoded = await image(png).metadata()
    assert.equal(decoded.width, 80)
    assert.equal(decoded.height, 60)
  }
  const actualPixels = await image(actual).ensureAlpha().raw().toBuffer()
  const diffPixels = await image(diff).ensureAlpha().raw().toBuffer()
  assert.deepEqual([...actualPixels.subarray(0, 4)], [255, 0, 0, 255])
  assert.deepEqual([...diffPixels.subarray(0, 4)], [255, 0, 0, 255])
  assert((await stat(actualPath)).isFile())
  assert((await stat(diffPath)).isFile())
  assert.deepEqual(await readFile(baselinePath), baseline)
  await empty()
  groups.push('Native committed actual/diff PNGs preserve explicit baseline')
  report.artifacts = {
    actualPath,
    diffPath,
    actualPNGBytes: actual.length,
    diffPNGBytes: diff.length,
  }

  await assert.rejects(
    expectNative(page).toHaveScreenshot(join(directory, 'missing.png'), { timeout: 400 }),
    (error) => !(error instanceof NativeAssertionError),
  )
  await empty()
  const invalid = join(directory, 'invalid.png')
  await writeFile(invalid, 'actual invalid PNG bytes')
  await assert.rejects(
    expectNative(page).toHaveScreenshot(invalid, { timeout: 400 }),
    (error) => !(error instanceof NativeAssertionError),
  )
  await empty()
  await assert.rejects(
    expectNative(page).toHaveScreenshot(baselinePath, { timeout: 200, artifactsDir: baselinePath }),
    (error) => !(error instanceof NativeAssertionError),
  )
  await empty()
  groups.push('Missing/invalid baseline and actual publication IO failure stay hard')

  const controller = new AbortController(),
    reason = new Error('Actual owner screenshot abort')
  const pending = expectNative(page).toHaveScreenshot(baselinePath, {
    timeout: 3000,
    signal: controller.signal,
  })
  const rejected = assert.rejects(pending, (error) => error === reason)
  let admitted = false
  const end = performance.now() + 2000
  while (performance.now() < end) {
    const state = z
      .object({ nativeScreenshotAssertionPhysical: z.number() })
      .parse(await active.runAutomation('diagnostics'))
    if (state.nativeScreenshotAssertionPhysical > 0) {
      admitted = true
      break
    }
  }
  assert(admitted, 'Abort follows a real native physical admission')
  controller.abort(reason)
  await rejected
  report.final = await empty()
  groups.push('Actual owner cancel preserves original Error through native physical retirement')
  report.completed = true
} catch (error) {
  report.error = error instanceof Error ? error.stack : String(error)
} finally {
  try {
    await source?.close()
    report.closed = true
  } catch (error) {
    report.closeError = String(error)
  }
  report.groups = groups
  report.metrics = metrics
  report.nativeStderrBytes = Buffer.byteLength(diagnostic)
  await writeFile(join(directory, 'native-stderr.log'), diagnostic)
  await writeFile(join(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
}
assert.equal(report.completed, true, typeof report.error === 'string' ? report.error : undefined)
assert.equal(report.closed, true)
assert.equal(diagnostic, '')
console.log(`PASS ${groups.length} genuine screenshot assertion capture/publication groups`)
