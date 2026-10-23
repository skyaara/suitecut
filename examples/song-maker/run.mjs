/* global AbortController */
import 'dotenv/config'

import assert from 'node:assert/strict'
import { constants } from 'node:fs'
import { access, mkdir, readFile, writeFile } from 'node:fs/promises'
import { delimiter, dirname, isAbsolute, join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'
import { fileURLToPath, URL } from 'node:url'

import { midi } from '@tonaljs/note'
import { launchBrowser } from '@suitecut/browser-control'
import { renderSuiteCut } from 'suitecut'
import { recordNative } from 'suitecut/native'
import { z } from 'zod'

import { verifyMedia } from './verify-media.mjs'

const config = z
  .object({
    SUITECUT_NATIVE_EXECUTABLE: z
      .string()
      .min(1)
      .refine(isAbsolute, 'Use an absolute engine executable path'),
    SUITECUT_EXAMPLE_HEADLESS: z.enum(['0', '1']).default('1'),
  })
  .parse(process.env)
const BoxSchema = z.object({
  x: z.number(),
  y: z.number(),
  width: z.number().positive(),
  height: z.number().positive(),
})
const output = resolve(process.env.SUITECUT_EXAMPLE_OUTPUT ?? `output/run-${Date.now()}`)
await mkdir(dirname(output), { recursive: true })
await mkdir(output, { recursive: false })

async function findTool(variable, name) {
  if (process.env[variable]) {
    await access(process.env[variable], constants.X_OK)
    return resolve(process.env[variable])
  }
  for (const directory of (process.env.PATH ?? '').split(delimiter)) {
    const candidate = join(directory, name)
    try {
      await access(candidate, constants.X_OK)
      return resolve(candidate)
    } catch (error) {
      if (!['ENOENT', 'EACCES'].includes(error.code)) throw error
    }
  }
  throw new Error(`Install ${name} or set ${variable} to its absolute executable path`)
}

const ffmpeg = await findTool('SUITECUT_FFMPEG_PATH', 'ffmpeg')
const ffprobe = await findTool('SUITECUT_FFPROBE_PATH', 'ffprobe')
process.env.SUITECUT_FFMPEG_PATH = ffmpeg
process.env.SUITECUT_FFPROBE_PATH = ffprobe
const controller = new AbortController()
const cancel = () => controller.abort(new Error('Example interrupted'))
process.once('SIGINT', cancel)
process.once('SIGTERM', cancel)
const timer = setTimeout(
  () => controller.abort(new Error('Example exceeded its six-minute budget')),
  360_000,
)
const actions = []
const appUrl = 'https://musiclab.chromeexperiments.com/Song-Maker/'
let browser, recording, render, verification, failure
let diagnosticBytes = 0
const diagnostics = []
const capture = { deliveredFrames: 0, maximumNativeDroppedFrames: 0, playbackWindows: [] }
let unsubscribeCapture
let browserFailure
const started = performance.now()
function step(name, detail = {}) {
  const entry = { name, elapsedMs: Math.round(performance.now() - started), ...detail }
  actions.push(entry)
  process.stdout.write(`${name}\n`)
}

try {
  // These are installed package imports. No dist/src paths or workspace links.
  const imports = Object.fromEntries(
    ['suitecut', '@suitecut/browser-control', '@tonaljs/note', 'zod', 'dotenv'].map((name) => [
      name,
      import.meta.resolve(name),
    ]),
  )
  const modules = join(dirname(fileURLToPath(import.meta.url)), 'node_modules')
  for (const [name, resolved] of Object.entries(imports)) {
    assert(
      fileURLToPath(resolved).startsWith(`${modules}/`),
      `${name} must resolve from this consumer's node_modules`,
    )
  }
  await writeFile(join(output, 'imports.json'), JSON.stringify(imports, null, 2))
  browser = await launchBrowser({
    backend: 'cef',
    executablePath: config.SUITECUT_NATIVE_EXECUTABLE,
    width: 1920,
    height: 1080,
    framesPerSecond: 60,
    pixelFormat: 'i420',
    headless: config.SUITECUT_EXAMPLE_HEADLESS === '1',
    signal: controller.signal,
    onDiagnostic(message) {
      diagnosticBytes += message.length
      if (diagnosticBytes <= 128 * 1024) diagnostics.push(message)
    },
  })
  assert.equal(browser.backend, 'cef')
  void browser.source.failure.catch((error) => {
    browserFailure = error
  })
  step('CEF engine launched', { cefVersion: browser.version() })
  recording = await recordNative(
    'Compose and perform a real Song Maker song',
    async ({ page, suitecut, source }) => {
      page.setDefaultTimeout(30_000)
      await page.goto(appUrl)
      await page.locator('#instrument-canvas').waitFor({ state: 'visible', timeout: 60_000 })
      assert.match(await page.title(), /Song Maker/)
      step('Opened the live Song Maker application')
      unsubscribeCapture = source.onFrame((frame) => {
        capture.deliveredFrames++
        capture.maximumNativeDroppedFrames = Math.max(
          capture.maximumNativeDroppedFrames,
          frame.droppedFrames,
        )
      })
      async function perform(arrangement) {
        const framesBefore = capture.deliveredFrames
        const start = performance.now()
        await suitecut.hold(10_000)
        const seconds = (performance.now() - start) / 1000
        capture.playbackWindows.push({
          arrangement,
          seconds,
          deliveredFrames: capture.deliveredFrames - framesBefore,
          deliveredFramesPerSecond: (capture.deliveredFrames - framesBefore) / seconds,
        })
      }
      await suitecut.checkpoint('Empty composition', { durationMs: 350 })

      const box = BoxSchema.parse(await page.locator('#instrument-canvas').boundingBox())
      const percussion = BoxSchema.parse(await page.locator('#percussion-canvas').boundingBox())
      const melody = [
        'C4',
        'E4',
        'G4',
        'A4',
        'G4',
        'E4',
        'D4',
        'C4',
        'F4',
        'A4',
        'C5',
        'B4',
        'G4',
        'E4',
        'D4',
        'C4',
      ]
      const scale = [
        'C4',
        'D4',
        'E4',
        'F4',
        'G4',
        'A4',
        'B4',
        'C5',
        'D5',
        'E5',
        'F5',
        'G5',
        'A5',
        'B5',
      ].map(midi)
      for (const [index, note] of melody.entries()) {
        const pitch = midi(note)
        const row = scale.indexOf(pitch)
        assert(pitch !== null && row >= 0, `Unsupported score note: ${note}`)
        const column = index * 2
        const x = box.x + (box.width * (column + 0.5)) / 32
        const y = box.y + (box.height * (13 - row + 0.5)) / 14
        await page.mouse.move(x, y, { steps: 5 })
        await page.mouse.click(x, y)
      }
      for (let column = 0; column < 32; column += 2) {
        await page.mouse.click(
          percussion.x + (percussion.width * (column + 0.5)) / 32,
          percussion.y + percussion.height * (column % 4 === 0 ? 0.25 : 0.75),
        )
      }
      assert.equal(
        await page.locator('#save-button').isEnabled(),
        true,
        'Native input must change the real composition',
      )
      await page.locator('input[name="tempo"]').fill('112')
      await page.locator('input[name="tempo"]').press('Tab')
      assert.equal(await page.locator('input[name="tempo"]').inputValue(), '112')
      step('Composed melody and percussion through real native mouse input', {
        notes: melody,
        tempo: 112,
      })
      await page.screenshot({ path: join(output, 'composition.png') })
      await suitecut.checkpoint('Composition ready', { durationMs: 350 })

      await page.getByRole('button', { name: 'Play', exact: true }).click()
      await page.getByRole('button', { name: 'Stop', exact: true }).waitFor({ state: 'visible' })
      step('Playing the first arrangement with real Web Audio')
      await perform('Marimba')
      await page.getByRole('button', { name: 'Stop', exact: true }).click()
      await page.locator('#instrument-toggle-button').click()
      assert.match(await page.locator('#instrument-toggle-button').innerText(), /Piano/)
      const beforeUndo = await page.locator('#percussion-canvas').screenshot()
      await page.locator('#undo-button').click()
      const afterUndo = await page.locator('#percussion-canvas').screenshot()
      assert(!beforeUndo.equals(afterUndo), 'Undo must visibly change the percussion arrangement')
      step('Stopped, changed Marimba to Piano, and undid the final percussion edit')
      await suitecut.checkpoint('Piano variation', { durationMs: 350 })
      await page.getByRole('button', { name: 'Play', exact: true }).click()
      await page.getByRole('button', { name: 'Stop', exact: true }).waitFor({ state: 'visible' })
      step('Playing the piano variation')
      await perform('Piano')
      await page.screenshot({ path: join(output, 'playing.png') })
      await page.getByRole('button', { name: 'Stop', exact: true }).click()
      await page.getByRole('button', { name: 'Play', exact: true }).waitFor({ state: 'visible' })
      step('Playback stopped cleanly; composition remains editable')
    },
    {
      source: browser.source,
      signal: controller.signal,
      ffmpegPath: ffmpeg,
      capture: { audio: true, framesPerSecond: 60, size: { width: 1920, height: 1080 } },
      output: { directory: join(output, 'recording'), manifestPath: join(output, 'manifest.json') },
    },
  )
  unsubscribeCapture?.()
  unsubscribeCapture = undefined
  await browser.close()
  if (browserFailure) throw browserFailure
  assert.equal(browser.isConnected(), false)
  const processExit = browser.source.processExit()
  assert.deepEqual(
    processExit,
    { code: 0, signal: null },
    'CEF must shut down cleanly without a forced kill',
  )
  step('Recording finalized and the owned CEF process closed', { processExit })
  const videoPath = join(output, 'song-maker-1080p60.mp4')
  render = await renderSuiteCut({
    manifestPath: recording.manifestPath,
    outputPath: videoPath,
    signal: controller.signal,
    config: {
      ffmpegPath: ffmpeg,
      output: { width: 1920, height: 1080, framesPerSecond: 60, quality: 'standard' },
      narrationEnabled: false,
      sourceAudioEnabled: true,
      resultHoldMs: 0,
    },
  })
  step('Rendered the completed 1080p60 video')
  verification = await verifyMedia({ videoPath, ffmpeg, ffprobe, signal: controller.signal })
  step('Decoded and verified the final video and audible page audio', verification)
  assert.equal(recording.manifest.status, 'passed')
} catch (error) {
  failure = error
  process.exitCode = 1
} finally {
  unsubscribeCapture?.()
  try {
    await browser?.close()
  } catch (error) {
    failure ??= error
    process.exitCode = 1
  }
  clearTimeout(timer)
  process.off('SIGINT', cancel)
  process.off('SIGTERM', cancel)
  const installed = JSON.parse(await readFile(new URL('./package.json', import.meta.url), 'utf8'))
  await writeFile(
    join(output, 'report.json'),
    JSON.stringify(
      {
        passed: !failure && verification?.passed === true,
        appUrl,
        backend: 'cef',
        requestedCaptureFps: 60,
        capture,
        actions,
        verification,
        dependencies: installed.dependencies,
        manifestPath: recording?.manifestPath,
        render,
        browserFailure: describeError(browserFailure),
        error: describeError(failure),
      },
      null,
      2,
    ),
  )
  await writeFile(join(output, 'native-stderr.log'), diagnostics.join(''))
  process.stdout.write(`Artifacts: ${output}\n`)
}
if (failure) throw failure

function describeError(error) {
  if (!error) return null
  if (!(error instanceof Error)) return { message: String(error), stack: undefined, cause: '' }
  return { message: error.message, stack: error.stack, cause: String(error.cause ?? '') }
}
