// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { NativeBrowserContexts, launchNativeBrowser, recordNative } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_RECORDING_LIFETIMES_DIRECTORY ??
    '.suitecut/native-recording-lifetimes-proof',
)
await mkdir(directory, { recursive: true })
const checks = []
const completed = []
const pixelFormat = z
  .enum(['bgra', 'i420'])
  .parse(process.env.SUITECUT_NATIVE_RECORDING_LIFETIMES_PIXEL_FORMAT ?? 'i420')
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 160,
  height: 96,
  framesPerSecond: 120,
  pixelFormat,
  headless: true,
})
const contexts = new NativeBrowserContexts(source)
const probeSchema = z.object({
  streams: z.array(
    z.object({
      codec_name: z.string(),
      codec_type: z.string(),
      width: z.number().optional(),
      height: z.number().optional(),
      r_frame_rate: z.string(),
      nb_read_frames: z.string().optional(),
      color_range: z.string().optional(),
      color_space: z.string().optional(),
      color_transfer: z.string().optional(),
      color_primaries: z.string().optional(),
    }),
  ),
})
/**
 * Inspects real encoded output through a bounded codec process.
 * @param {string} program
 * @param {string[]} args
 */
function inspect(program, args) {
  const result = spawnSync(program, args, { timeout: 60_000, maxBuffer: 64 * 1024 * 1024 })
  if (result.error) throw result.error
  assert.equal(result.status, 0, result.stderr.toString('utf8'))
  return result.stdout
}
/**
 * Initializes an actual animated CEF scene with known primary RGB values.
 * @param {import('../dist/native-page.js').NativePage} page
 */
async function scene(page) {
  await page.setContent(`<style>body{margin:0;overflow:hidden}</style><canvas width=160 height=96></canvas><script>
    globalThis.mediaFrames=0;const ctx=document.querySelector('canvas').getContext('2d');
    function draw(){ctx.fillStyle=(Math.floor(mediaFrames/15)%2)?'#2244ee':'#ee2244';ctx.fillRect(0,0,160,96);mediaFrames++;requestAnimationFrame(draw)}draw();
  </script>`)
  await page.waitForFunction('globalThis.mediaFrames >= 12')
}
/**
 * Verifies actual native frames and BT.709 limited-range I420 interpretation.
 * @param {import('../dist/native-recording-protocol.js').NativeRecordingInfo} info
 * @param {number} fps
 */
function verify(info, fps) {
  assert.equal(info.phase, 'complete')
  assert.equal(info.failure, '')
  assert.equal(info.framesPerSecond, fps)
  const probe = probeSchema.parse(
    JSON.parse(
      inspect('/usr/bin/ffprobe', [
        '-v',
        'error',
        '-count_frames',
        '-show_streams',
        '-of',
        'json',
        info.outputPath,
      ]).toString('utf8'),
    ),
  )
  const stream = probe.streams.find((value) => value.codec_type === 'video')
  assert(stream)
  assert.equal(stream.codec_name, 'vp9')
  if (pixelFormat === 'i420') {
    assert.equal(stream.color_space, 'bt709')
    assert.equal(stream.color_primaries, 'bt709')
    assert.equal(stream.color_transfer, 'iec61966-2-1')
    assert.equal(stream.color_range, 'tv')
  }
  const [num, den] = stream.r_frame_rate.split('/').map(Number)
  assert(Math.abs(num / den - fps) < 1e-8)
  assert.equal(Number(stream.nb_read_frames), info.outputFrames)
  const rgb = inspect('/usr/bin/ffmpeg', [
    '-v',
    'error',
    '-i',
    info.outputPath,
    '-map',
    '0:v',
    '-fps_mode',
    'passthrough',
    '-f',
    'rawvideo',
    '-pix_fmt',
    'rgb24',
    'pipe:1',
  ])
  assert.equal(rgb.length, info.outputFrames * 160 * 96 * 3)
  const colors = new Set()
  for (let frame = 0; frame < info.outputFrames; ++frame) {
    const at = frame * 160 * 96 * 3 + (48 * 160 + 80) * 3
    const actual = [...rgb.subarray(at, at + 3)]
    const redError = actual.reduce(
      (sum, value, index) => sum + Math.abs(value - [238, 34, 68][index]),
      0,
    )
    const blueError = actual.reduce(
      (sum, value, index) => sum + Math.abs(value - [34, 68, 238][index]),
      0,
    )
    assert(
      Math.min(redError, blueError) < 24,
      `I420 source matrix/range altered actual RGB: ${actual.join(',')}`,
    )
    colors.add(redError < blueError ? 'red' : 'blue')
  }
  assert.equal(colors.size, 2, 'native output lost actual animated captures')
  return probe
}
try {
  for (const mode of ['public-fractional', 'page-close', 'context-close']) {
    const context = await contexts.newContext()
    const page = await context.newPage()
    await scene(page)
    let terminal
    const detach = page.source.onRecordingMessage?.((value) => {
      if (value.phase === 'complete' || value.phase === 'failed') terminal = value
    })
    try {
      const rate = mode === 'public-fractional' ? 29.97 : 120
      const result = await recordNative(
        mode,
        async ({ page: capturePage }) => {
          const first = z.number().parse(await capturePage.evaluate('globalThis.mediaFrames'))
          await capturePage.waitForFunction(`globalThis.mediaFrames >= ${first + 60}`)
          if (mode === 'page-close') await capturePage.close()
          if (mode === 'context-close') await context.close()
        },
        {
          source: page.source,
          ffmpegPath: '/usr/bin/ffmpeg',
          capture: { framesPerSecond: rate, size: { width: 160, height: 96 } },
          output: {
            directory: join(directory, mode),
            manifestPath: join(directory, `${mode}-manifest.json`),
          },
        },
      )
      assert.equal(result.manifest.status, 'passed')
      assert(terminal, 'recording lifecycle omitted real terminal metadata')
      const probe = verify(terminal, rate)
      completed.push({
        mode,
        native: terminal,
        probe,
        unavailableColorMetadata: ['color_primaries', 'color_transfer'].filter(
          (field) => !(field in probe.streams[0]),
        ),
      })
      assert.equal(
        source.state,
        'running',
        'closing a page/context incorrectly closed the native helper',
      )
      assert.equal(source.processExit?.(), undefined)
      if (mode !== 'public-fractional') assert(page.isClosed())
      checks.push(
        mode === 'public-fractional'
          ? 'public-recordNative29.97-output-from-integer120CEF-capture'
          : `actual-native-${mode}-finalizes-recording-and-keeps-browser-running`,
      )
    } finally {
      detach?.()
      await context.close()
    }
  }
  const diagnostics = z
    .object({ nativeRecordingPhysical: z.literal(0) })
    .parse(await source.runAutomation('diagnostics'))
  checks.push(
    pixelFormat === 'i420'
      ? 'actual-I420-BT709-limited-tags-and-decoded-RGB-preserved'
      : 'actual-BGRA-decoded-RGB-preserved',
  )
  checks.push('physical-native-recording-ownership-zero-after-page-and-context-close')
  const hash = async (path) =>
    createHash('sha256')
      .update(await readFile(z.string().parse(path)))
      .digest('hex')
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        sourceCaptureVerified: true,
        pixelFormat,
        binarySHA256: await hash(executable),
        cefSHA256: await hash(join(dirname(executable), 'libcef.so')),
        checks,
        diagnostics,
        recordings: completed,
      },
      null,
      2,
    ),
  )
  console.log(`Native public fractional/page/context/${pixelFormat} lifecycle proof passed`)
} catch (error) {
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      {
        completed: false,
        checks,
        recordings: completed,
        error: error instanceof Error ? error.stack : String(error),
      },
      null,
      2,
    ),
  )
  throw error
} finally {
  await source.close().catch(() => undefined)
}
