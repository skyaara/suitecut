/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- Verification fixture uses JSDoc parameter types. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { promisify } from 'node:util'

import { decodeManifest } from '../dist/manifest.js'
import { createNativeVideoRecorder } from '../dist/native-recorder.js'
import { resolveFfmpeg, resolveFfprobe } from '../dist/process.js'
import { createAttempt } from '../dist/recording-output.js'
import { renderSuiteCut } from '../dist/render.js'

const execute = promisify(execFile)
const ffmpeg = await resolveFfmpeg()
const ffprobe = await resolveFfprobe(ffmpeg)
const directory = resolve('.suitecut/native-cadence-verification')
await mkdir(directory, { recursive: true })
const width = 64
const height = 16
const epochMs = 250

/**
 * Encodes an eight-bit frame ID into large black/white blocks.
 * @param {number} id
 */
function pixels(id) {
  const bgra = Buffer.alloc(width * height * 4)
  for (let y = 0; y < height; y++)
    for (let x = 0; x < width; x++) {
      const offset = (y * width + x) * 4
      const value = id & (1 << Math.floor(x / 8)) ? 255 : 0
      bgra.fill(value, offset, offset + 3)
      bgra[offset + 3] = 255
    }
  return bgra
}

/**
 * Decodes actual pixels, without an FPS filter that could hide bad cadence.
 * @param {string} path
 */
async function frameIds(path) {
  const rawPath = `${path}.rgb`
  await execute(ffmpeg, [
    '-v',
    'error',
    '-y',
    '-i',
    path,
    '-an',
    '-fps_mode',
    'passthrough',
    '-pix_fmt',
    'rgb24',
    '-f',
    'rawvideo',
    rawPath,
  ])
  const raw = await readFile(rawPath)
  const bytesPerFrame = width * height * 3
  assert.equal(raw.length % bytesPerFrame, 0, 'Partial decoded frame')
  const ids = []
  for (let frame = 0; frame < raw.length / bytesPerFrame; frame++) {
    let id = 0
    for (let bit = 0; bit < 8; bit++) {
      const offset = frame * bytesPerFrame + (8 * width + bit * 8 + 4) * 3
      if (raw[offset] > 128) id |= 1 << bit
    }
    ids.push(id)
  }
  return ids
}

const cases = []
for (const [captureFps, outputFps] of [
  [24, 120],
  [120, 24],
  [120, 120],
  [59, 37],
]) {
  const name = `${captureFps}-to-${outputFps}`
  const outputPath = resolve(directory, `${name}.webm`)
  const recorder = createNativeVideoRecorder({
    source: { width, height, pixelFormat: 'bgra', framesPerSecond: captureFps },
    framesPerSecond: outputFps,
    ffmpegPath: ffmpeg,
    outputPath,
    size: { width, height },
    audio: true,
  })
  // This supplies timestamped test pixels/PCM to the real recorder and encoder;
  // it does not pretend to prove CEF's delivered capture performance.
  const pcm = Buffer.alloc(48_000 * 4)
  for (let sample = 0; sample < 48_000; sample++) {
    const value = Math.round(6000 * Math.sin((2 * Math.PI * 440 * sample) / 48_000))
    pcm.writeInt16LE(value, sample * 4)
    pcm.writeInt16LE(value, sample * 4 + 2)
  }
  recorder.pushAudio(pcm, epochMs)
  for (let index = 0; index < captureFps; index++)
    await Promise.race([
      recorder.push(pixels(index), epochMs + (index * 1000) / captureFps),
      recorder.failure,
    ])
  await recorder.stop(epochMs + 1000)
  const ids = await frameIds(outputPath)
  const expected = Array.from({ length: outputFps }, (_, index) =>
    Math.floor((index * captureFps) / outputFps),
  )
  assert.deepEqual(ids, expected, `${name}: wrong decoded hold/drop sequence or frame count`)
  const audioPath = `${outputPath}.pcm`
  await execute(ffmpeg, [
    '-v',
    'error',
    '-y',
    '-i',
    outputPath,
    '-vn',
    '-ac',
    '2',
    '-ar',
    '48000',
    '-f',
    's16le',
    audioPath,
  ])
  const decodedAudio = await readFile(audioPath)
  assert.equal(decodedAudio.length, 48_000 * 4, `${name}: audio duration differs from one second`)
  for (let start = 0; start < 48_000; start += 480) {
    let energy = 0
    for (let sample = start; sample < start + 480; sample++)
      energy += decodedAudio.readInt16LE(sample * 4) ** 2
    assert(Math.sqrt(energy / 480) > 2500, `${name}: missing audio in 10 ms window`)
  }
  cases.push({
    captureFps,
    outputFps,
    decodedFrames: ids.length,
    distinctFrames: new Set(ids).size,
    decodedAudioSamples: decodedAudio.length / 4,
  })
}

// Exercise the shared manifest renderer and a second resampling stage.
const manifestPath = resolve(directory, 'render-manifest.json')
const startedAt = '2026-10-14T00:00:00.000Z'
const attempt = await createAttempt(
  {
    attemptId: 'cadence-attempt',
    clock: { originEpochMs: Date.parse(startedAt), originMonotonicMs: 0, startedAt },
    endedAtMs: 1000,
    pages: [
      { id: 'page', kind: 'main', initialUrl: 'https://example.test/cadence', createdAtMs: 0 },
    ],
    events: [],
    artifacts: [],
    videos: [
      {
        artifactId: 'video',
        pageId: 'page',
        attachmentName: '120-to-120.webm',
        firstFrameEpochMs: Date.parse(startedAt),
        sourceStartedAtMs: 0,
      },
    ],
  },
  directory,
  manifestPath,
  'absolute',
  [],
)
const manifest = decodeManifest({
  schemaVersion: 1,
  startedAt,
  endedAt: '2026-10-14T00:00:01.000Z',
  status: 'passed',
  rootDirectory: directory,
  tests: [
    {
      id: 'cadence-test',
      order: 0,
      title: 'Native cadence',
      titlePath: ['Native cadence'],
      projectName: 'native-cadence',
      location: { file: resolve('scripts/verify-native-cadence.mjs'), line: 1, column: 1 },
      attempts: [attempt],
    },
  ],
})
await writeFile(manifestPath, JSON.stringify(manifest, null, 2))
const renders = []
for (const fps of [1, 24, 37, 59, 60, 120]) {
  const renderedPath = resolve(directory, `rendered-${fps}.mp4`)
  await renderSuiteCut({
    manifestPath,
    outputPath: renderedPath,
    config: { output: { width, height, framesPerSecond: fps } },
  })
  assert.deepEqual(
    await frameIds(renderedPath),
    Array.from({ length: fps }, (_, index) => Math.floor((index * 120) / fps)),
    `Final renderer changed frame IDs or duration at ${fps} FPS`,
  )
  renders.push({ inputFps: 120, outputFps: fps, decodedFrames: fps })
}
const report = {
  fixture: 'timestamped synthetic pixels and PCM through real FFmpeg; CEF delivery unverified',
  cases,
  renders,
  ffmpeg,
  ffprobe,
}
await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
console.log(JSON.stringify(report, null, 2))
