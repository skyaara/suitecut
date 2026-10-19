// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve, join } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

const binary = resolve(z.string().min(1).parse(process.env.SUITECUT_STREAM_CORE_TEST_EXECUTABLE))
const ffmpeg = z
  .string()
  .min(1)
  .parse(process.env.SUITECUT_FFMPEG ?? '/usr/bin/ffmpeg')
const directory = resolve(
  process.env.SUITECUT_NATIVE_STREAM_CORE_DIRECTORY ?? '.suitecut/native-stream-core-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Runs a bounded native helper or a codec inspection of genuine received bytes.
 * @param {string} program
 * @param {string[]} args
 */
function run(program, args) {
  const result = spawnSync(program, args, { timeout: 120_000, maxBuffer: 64 * 1024 * 1024 })
  if (result.error) throw result.error
  assert.equal(result.status, 0, result.stderr.toString('utf8'))
  return result.stdout
}
const metadataSchema = z.object({
  submittedFrames: z.number().int().positive(),
  encodedFrames: z.number().int().positive(),
  heldFrames: z.number().int().positive(),
  audioInputFrames: z.number().int().positive(),
  audioOutputFrames: z.number().int().positive(),
  audioGapFrames: z.number().int().nonnegative(),
  pausedAudioFrames: z.number().int().positive(),
  muxedBytes: z.number().int().positive(),
})
const probes = z.object({
  streams: z.array(
    z.object({
      codec_name: z.string(),
      codec_type: z.string(),
      r_frame_rate: z.string(),
      nb_read_frames: z.string().optional(),
      sample_rate: z.string().optional(),
      channels: z.number().optional(),
    }),
  ),
})
run(binary, [ffmpeg, directory])
const metadata = metadataSchema.parse(
  JSON.parse(await readFile(join(directory, 'native-stream-core.json'), 'utf8')),
)
const path = join(directory, 'actual-stream.webm')
const probe = probes.parse(
  JSON.parse(
    run('/usr/bin/ffprobe', [
      '-v',
      'error',
      '-count_frames',
      '-show_streams',
      '-of',
      'json',
      path,
    ]).toString('utf8'),
  ),
)
const video = probe.streams.find((stream) => stream.codec_type === 'video')
assert(video)
assert.equal(video.codec_name, 'vp9')
assert.equal(video.r_frame_rate, '120/1')
assert.equal(Number(video.nb_read_frames), metadata.submittedFrames)
assert.equal(metadata.encodedFrames, metadata.submittedFrames)
const bytes = run(ffmpeg, [
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
assert.equal(bytes.length, metadata.submittedFrames * 64 * 64 * 3)
const firstPixel = (frame) => bytes.subarray(frame * 64 * 64 * 3, frame * 64 * 64 * 3 + 3)
assert(firstPixel(0)[0] > 180 && firstPixel(0)[2] < 80)
assert(
  firstPixel(metadata.submittedFrames - 1)[2] > 180 &&
    firstPixel(metadata.submittedFrames - 1)[0] < 80,
)
const audio = probe.streams.find((stream) => stream.codec_type === 'audio')
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
assert.equal(
  pcm.length / 4,
  metadata.audioOutputFrames,
  'Native publisher lost PCM samples before Opus encoding',
)
/**
 * Measures decoded stereo PCM on its actual output timeline.
 * @param {number} start
 * @param {number} end
 */
function rms(start, end) {
  const begin = Math.round(start * 48000),
    finish = Math.min(Math.round(end * 48000), pcm.length / 4)
  assert(finish > begin)
  let squares = 0
  for (let frame = begin; frame < finish; ++frame) squares += pcm.readInt16LE(frame * 4) ** 2
  return Math.sqrt(squares / (finish - begin))
}
assert(rms(0.12, 0.24) > 2000, 'received publisher PCM lost generated real input')
assert(rms(0.34, 0.4) < 150, 'live pause did not mute its actual continuous audio timeline')
assert(rms(0.72, 0.88) > 2000, 'actual source switch lost subsequent captured PCM')
const flvMetadata = z
  .object({ frames: z.number().int().positive(), audioFrames: z.number().int().positive() })
  .parse(JSON.parse(await readFile(join(directory, 'native-stream-flv.json'), 'utf8')))
const flv = join(directory, 'actual-stream.flv')
const flvProbe = probes.parse(
  JSON.parse(
    run('/usr/bin/ffprobe', [
      '-v',
      'error',
      '-count_frames',
      '-show_streams',
      '-of',
      'json',
      flv,
    ]).toString('utf8'),
  ),
)
const flvVideo = flvProbe.streams.find((stream) => stream.codec_type === 'video')
const flvAudio = flvProbe.streams.find((stream) => stream.codec_type === 'audio')
assert(flvVideo && flvAudio)
assert.equal(flvVideo.codec_name, 'h264')
assert.equal(flvAudio.codec_name, 'aac')
assert.equal(Number(flvVideo.nb_read_frames), flvMetadata.frames)
assert.equal(flvVideo.r_frame_rate, '60/1')
assert.equal(flvAudio.sample_rate, '48000')
assert.equal(flvAudio.channels, 2)
const startDeadlineFailure = await readFile(
  join(directory, 'short-start-native-failure.txt'),
  'utf8',
)
assert(startDeadlineFailure.includes('initial encoded readiness deadline'))
const blockedFailure = await readFile(join(directory, 'blocked-native-failure.txt'), 'utf8')
assert(blockedFailure.includes('bounded pipe lag') || blockedFailure.includes('cannot keep up'))
const disconnectedFailure = await readFile(
  join(directory, 'disconnected-native-failure.txt'),
  'utf8',
)
assert(disconnectedFailure.includes('Connection reset by peer'))
await writeFile(
  join(directory, 'report.json'),
  JSON.stringify(
    {
      completed: true,
      sourceCaptureVerified: false,
      scope: 'standalone generated-input C++ network publisher',
      nativeSHA256: createHash('sha256')
        .update(await readFile(binary))
        .digest('hex'),
      metadata,
      probe,
      flvMetadata,
      flvProbe,
      blockedFailure,
      startDeadlineFailure,
      disconnectedFailure,
      checks: [
        'actualTCP-receiver-bytes-decoded-120FPS',
        'held-CFR-slots-from-generated-30FPS-input',
        'actual-source-switch-red-to-blue',
        'actual48k-stereo-Opus-generated-PCM',
        'live-pause-holds-image-and-mutes-audio',
        'native-first-frame-deadline-cancel-encoder-failure-and-codec-validation',
        'actual-network-disconnect-preserves-FFmpeg-socket-error',
        'actual-stalled-TCP-peer-bounded-native-pipe-lag-failure',
        'actual-received-H264-AAC-FLV-60FPS',
        'native-PCM-memory-overflow-fails-and-four-job-capacity-is-bounded',
        'original-start-deadline-bounds-stalled-child-pipe-and-physical-waitpid',
      ],
    },
    null,
    2,
  ),
)
console.log('Native streaming core received/decode/audio/lifecycle proof passed')
