// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { readFile, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

const directory = resolve(process.argv[2] ?? '.suitecut/native-stream-timing-tone-proof')
/**
 * Decodes actual received media.
 * @param {string[]} args
 */
function run(args) {
  const result = spawnSync('/usr/bin/ffmpeg', args, { timeout: 30000, maxBuffer: 32 * 1024 * 1024 })
  if (result.error) throw result.error
  assert.equal(result.status, 0, result.stderr.toString())
  assert.equal(result.stderr.length, 0)
  return result.stdout
}
const metadataSchema = z.object({
  rate: z.number(),
  frames: z.number(),
  encoded: z.number(),
  audio: z.number(),
  first: z.number(),
  pause: z.number(),
  resume: z.number(),
  select: z.number(),
  gap: z.number(),
  overlap: z.number(),
  pausedAudio: z.number(),
  pausedInputAudio: z.number(),
  switchedAudio: z.number(),
  peakPictures: z.number(),
  picturesAtEnd: z.number(),
})
const results = []
for (const rate of [120, 59.94, 29.97]) {
  const name = `rate-${Math.round(rate * 100)}`,
    meta = metadataSchema.parse(
      JSON.parse(await readFile(join(directory, `${name}.json`), 'utf8')),
    ),
    path = join(directory, `${name}.webm`)
  const pcm = run([
    '-v',
    'error',
    '-i',
    path,
    '-map',
    '0:a',
    '-f',
    's16le',
    '-ar',
    '48000',
    '-ac',
    '2',
    'pipe:1',
  ])
  const frames = run([
    '-v',
    'error',
    '-i',
    path,
    '-map',
    '0:v',
    '-fps_mode',
    'passthrough',
    '-f',
    'rawvideo',
    '-pix_fmt',
    'gray',
    'pipe:1',
  ])
  assert.equal(pcm.length / 4, meta.audio)
  assert.equal(frames.length / (64 * 64), meta.frames)
  assert.equal(meta.frames, meta.encoded)
  /**
   * Excludes the actual pause and source-transition boundaries.
   * @param {number} t
   */
  const excluded = (t) =>
    (t > (meta.pause - meta.first) / 1000 - 0.04 && t < (meta.resume - meta.first) / 1000 + 0.04) ||
    Math.abs(t - (meta.select - meta.first) / 1000) < 0.04
  const rms = []
  const audioEdges = z.array(z.number()).parse([])
  let prior = false
  for (let first = 0; first + 240 <= pcm.length / 4; first += 240) {
    let squares = 0
    for (let i = first; i < first + 240; ++i) squares += pcm.readInt16LE(i * 4) ** 2
    const value = Math.sqrt(squares / 240),
      at = first / 48000
    const pulse = value > 5500
    if (pulse && !prior) audioEdges.push(at)
    prior = pulse
    if (at > 0.2 && at < pcm.length / 4 / 48000 - 0.1 && !excluded(at)) rms.push({ at, value })
  }
  const worst = rms.reduce((a, b) => (a.value < b.value ? a : b))
  assert(worst.value > 1800, `Decoded continuous tone dropout ${name}: ${JSON.stringify(worst)}`)
  const visualEdges = z.array(z.number()).parse([])
  prior = false
  for (let i = 0; i < meta.frames; ++i) {
    const pulse = z.number().parse(frames[i * 4096 + 32 * 64 + 32]) > 200
    if (pulse && !prior && i / rate > 0.2 && !excluded(i / rate)) visualEdges.push(i / rate)
    prior = pulse
  }
  assert(visualEdges.length >= 6)
  const errors = visualEdges.map((at) =>
    Math.min(...audioEdges.map((audio) => Math.abs(audio - at))),
  )
  const bound = 2 / 120 + 1 / rate + 0.01
  assert(
    Math.max(...errors) < bound,
    `Actual synchronized pulse mismatch ${rate}: ${errors.join(',')}`,
  )
  results.push({
    rate,
    meta,
    decodedStereoFrames: pcm.length / 4,
    lowestContinuousTone5msRMS: worst,
    actualVisualPulseTimes: visualEdges,
    actualAudioPulseTimes: audioEdges,
    actualPulseMaxErrorMs: Math.max(...errors) * 1000,
    pulseErrorBoundMs: bound * 1000,
  })
}
await writeFile(
  join(directory, 'decoded-report.json'),
  JSON.stringify(
    {
      completed: true,
      sourceCaptureVerified: false,
      scope:
        'Standalone real-clock generated C++ media, received TCP and decoded FFmpeg pixels/Opus',
      results,
    },
    null,
    2,
  ) + '\n',
)
console.log(
  'Native causal media timing: exact samples, continuous tone and synchronized pulses passed',
)
