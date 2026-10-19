// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

const executable = resolve(
  z.string().min(1).parse(process.env.SUITECUT_STREAMING_CONTROLS_EXECUTABLE),
)
const directory = resolve(
  process.env.SUITECUT_STREAMING_CONTROLS_DIRECTORY ?? '.suitecut/native-streaming-controls-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Runs the real C++ publisher manager or decodes its actual received bytes.
 * @param {string} program
 * @param {string[]} args
 */
function run(program, args) {
  const result = spawnSync(program, args, { timeout: 120_000, maxBuffer: 64 * 1024 * 1024 })
  if (result.error) throw result.error
  assert.equal(result.status, 0, result.stderr.toString('utf8'))
  return result.stdout
}
run(executable, ['/usr/bin/ffmpeg', directory])
const probeSchema = z.object({
  streams: z.array(
    z.object({
      codec_name: z.string(),
      codec_type: z.string(),
      r_frame_rate: z.string(),
      nb_read_frames: z.string(),
      color_space: z.string().optional(),
      color_range: z.string().optional(),
      color_primaries: z.string().optional(),
      color_transfer: z.string().optional(),
    }),
  ),
})
const outputs = []
for (const name of ['selected-source.webm', 'context-close.webm', 'i420-709.webm']) {
  const file = join(directory, name)
  const metadata = probeSchema.parse(
    JSON.parse(
      run('/usr/bin/ffprobe', [
        '-v',
        'error',
        '-count_frames',
        '-show_streams',
        '-of',
        'json',
        file,
      ]).toString('utf8'),
    ),
  )
  const video = metadata.streams.find((stream) => stream.codec_type === 'video')
  assert(video)
  assert.equal(video.codec_name, 'vp9')
  assert.equal(video.r_frame_rate, '2997/100')
  const count = Number(video.nb_read_frames)
  assert(count > 0)
  const rgb = run('/usr/bin/ffmpeg', [
    '-v',
    'error',
    '-i',
    file,
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
  assert.equal(rgb.length, count * 64 * 64 * 3)
  if (name === 'i420-709.webm') {
    assert.equal(video.color_space, 'bt709')
    assert.equal(video.color_range, 'tv')
    assert.equal(video.color_primaries, 'bt709')
    assert.equal(video.color_transfer, 'iec61966-2-1')
    for (let frame = 0; frame < count; ++frame) {
      const actual = [...rgb.subarray(frame * 64 * 64 * 3, frame * 64 * 64 * 3 + 3)]
      assert(
        actual.reduce((sum, value, channel) => sum + Math.abs(value - [235, 34, 67][channel]), 0) <
          12,
        `Native publisher changed actual BT709 input RGB: ${actual.join(',')}`,
      )
    }
  }
  outputs.push({ name, metadata })
}
const deadline = z
  .object({
    actualPid: z.number().int().positive(),
    laterDeadlineMs: z.literal(40),
    elapsedMs: z.number().nonnegative().max(999),
    deadlineHonored: z.literal(true),
    physicalCount: z.literal(0),
  })
  .parse(JSON.parse(await readFile(join(directory, 'shorter-stop.json'), 'utf8')))
await writeFile(
  join(directory, 'report.json'),
  JSON.stringify(
    {
      completed: true,
      sourceCaptureVerified: false,
      scope:
        'Authored C++ streaming manager and actual FFmpeg TCP bytes; CEF Source activation pending',
      binarySHA256: createHash('sha256')
        .update(await readFile(executable))
        .digest('hex'),
      checks: [
        'native-page-source-selection-and-old-input-rejection',
        'native-original-owner-and-selected-context-lifetime',
        'real-EOF-before-page-context-close-continuation',
        'actual-cancellation-and-no-late-command-reply',
        'native-destination-exclusivity-and-physical-zero',
        'actual-I420-BT709-29.97-stream-decode',
        'real-SIGSTOPed-encoder-earlier-unlimited-stop-bounded-by-original-later-40ms-deadline',
      ],
      outputs,
      deadline,
    },
    null,
    2,
  ) + '\n',
)
console.log('Native streaming manager and real publisher byte decoding passed')
