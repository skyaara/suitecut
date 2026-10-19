// SPDX-License-Identifier: MIT
// Standalone native worker proof. Source/CEF capture requires its separate test.
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

const executable = z.string().min(1).parse(process.env.SUITECUT_MEDIA_TEST_EXECUTABLE)
const ffmpeg = z
  .string()
  .min(1)
  .parse(process.env.SUITECUT_FFMPEG ?? '/usr/bin/ffmpeg')
const ffprobe = z
  .string()
  .min(1)
  .parse(process.env.SUITECUT_FFPROBE ?? '/usr/bin/ffprobe')
const directory = resolve(
  process.env.SUITECUT_NATIVE_MEDIA_CORE_DIRECTORY ?? '.suitecut/native-media-core-proof',
)
await mkdir(directory, { recursive: true })
await rm(join(directory, 'report.json'), { force: true })
/**
 * Runs a bounded helper or codec verification process.
 * @param {string} program
 * @param {string[]} args
 */
const run = (program, args) => {
  const result = spawnSync(program, args, { timeout: 120_000, maxBuffer: 8 * 1024 * 1024 })
  if (result.error) throw result.error
  assert.equal(result.status, 0, result.stderr.toString('utf8'))
  return result.stdout
}
const probeSchema = z.object({
  streams: z.array(
    z.object({
      codec_type: z.string(),
      codec_name: z.string(),
      r_frame_rate: z.string(),
      nb_read_frames: z.string(),
      sample_rate: z.string().optional(),
      channels: z.number().optional(),
      color_space: z.string().optional(),
      color_range: z.string().optional(),
      color_primaries: z.string().optional(),
      color_transfer: z.string().optional(),
    }),
  ),
})
run(executable, [ffmpeg, directory])
const probes = []
for (const [name, rate, count] of [
  ['held-120.webm', '120/1', 36],
  ['pause-audio.webm', '120/1', 24],
  ['fractional.webm', '2997/50', 60],
  ['i420-709.webm', '2997/100', 9],
]) {
  const path = join(directory, name)
  const info = probeSchema.parse(
    JSON.parse(
      run(ffprobe, ['-v', 'error', '-count_frames', '-show_streams', '-of', 'json', path]).toString(
        'utf8',
      ),
    ),
  )
  const video = info.streams.find((stream) => stream.codec_type === 'video')
  assert(video)
  assert.equal(video.r_frame_rate, rate)
  assert.equal(Number(video.nb_read_frames), count)
  const decoded = run(ffmpeg, [
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
    'rgb24',
    'pipe:1',
  ])
  assert.equal(decoded.length, count * 64 * 64 * 3)
  if (name === 'i420-709.webm') {
    assert.equal(video.color_space, 'bt709')
    assert.equal(video.color_range, 'tv')
    assert.equal(video.color_primaries, 'bt709')
    assert.equal(video.color_transfer, 'iec61966-2-1')
    for (let frame = 0; frame < count; ++frame) {
      const at = frame * 64 * 64 * 3
      const rgb = [...decoded.subarray(at, at + 3)]
      assert(
        rgb.reduce((sum, value, channel) => sum + Math.abs(value - [235, 34, 67][channel]), 0) < 12,
        `Native I420 input was reinterpreted: ${rgb.join(',')}`,
      )
    }
  } else
    for (let frame = 0; frame < count; ++frame) {
      const first = frame * 64 * 64 * 3
      const red = name === 'fractional.webm' || frame < 12
      assert(decoded[first + (red ? 0 : 2)] > 220, `${name}: wrong held image at frame ${frame}`)
      assert(decoded[first + (red ? 2 : 0)] < 20, `${name}: unexpected color at frame ${frame}`)
    }
  if (name === 'pause-audio.webm') {
    const audio = info.streams.find((stream) => stream.codec_type === 'audio')
    assert(audio)
    assert.equal(audio.codec_name, 'opus')
    assert.equal(audio.sample_rate, '48000')
    assert.equal(audio.channels, 2)
    const pcm = run(ffmpeg, [
      '-v',
      'error',
      '-i',
      path,
      '-map',
      '0:a',
      '-f',
      's16le',
      '-ac',
      '2',
      '-ar',
      '48000',
      'pipe:1',
    ])
    assert.equal(pcm.length, 9600 * 4)
    const rms = (startMs, endMs) => {
      let squares = 0
      const begin = Math.round(startMs * 48)
      const end = Math.round(endMs * 48)
      for (let frame = begin; frame < end; ++frame) squares += pcm.readInt16LE(frame * 4) ** 2
      return Math.sqrt(squares / (end - begin))
    }
    assert(rms(5, 15) > 5000, 'first captured PCM tone was lost')
    assert(rms(105, 115) > 5000, 'resumed captured PCM tone was lost')
    assert(rms(50, 80) < 1000, 'native audio gap was not aligned')
  }
  probes.push(info)
}
/**
 * Hashes the exact input bytes used by this proof.
 * @param {string} path
 */
const hash = async (path) =>
  createHash('sha256')
    .update(await readFile(path))
    .digest('hex')
await writeFile(
  join(directory, 'report.json'),
  JSON.stringify(
    {
      completed: true,
      kind: 'standalone-native-worker-generated-input',
      sourceCaptureVerified: false,
      helperSHA256: await hash(executable),
      ffmpegSHA256: await hash(ffmpeg),
      sources: await Promise.all(
        ['media_pipeline.h', 'media_pipeline.cc', 'media_pipeline_test.cc'].map(async (name) => ({
          name,
          sha256: await hash(join('native/browser', name)),
        })),
      ),
      checks: [
        'real-120-CFR-decoded-held-images',
        'fractional-59.94-CFR',
        'raw-I420-709-input-tags-and-known-RGB-preserved',
        'real-PCM-Opus-tone-gap-pause-alignment',
        'first-frame-finish-original-deadlines',
        'canceled-stalled-encoder-native-waitpid',
        'forced-encoder-failure',
        'bounded-audio-overflow',
        'malformed-data-rejection',
        'secure-atomic-symlink-replacement',
        'no-private-files-or-child-processes',
        'actual-parent-SIGKILL-encoder-PDEATHSIG-and-anonymous-FD-cleanup',
        'OS-encoder-and-native-PCM-quota-failure',
      ],
      probes,
    },
    null,
    2,
  ),
)
console.log('Native media core decoded video/audio and physical lifecycle proof passed')
