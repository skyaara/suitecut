import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { execFile } from 'node:child_process'
import { promisify } from 'node:util'

import { z } from 'zod'

const execute = promisify(execFile)
const probeSchema = z.object({
  streams: z.array(
    z.object({
      codec_type: z.string(),
      codec_name: z.string(),
      width: z.number().optional(),
      height: z.number().optional(),
      avg_frame_rate: z.string().optional(),
      nb_read_frames: z.string().optional(),
      sample_rate: z.string().optional(),
      channels: z.number().optional(),
      duration: z.string().optional(),
    }),
  ),
  format: z.object({ duration: z.string() }),
})

/** Verify encoded cadence and actual decoded audio, not just stream presence. */
export async function verifyMedia({ videoPath, ffmpeg, ffprobe, signal }) {
  const probe = await execute(
    ffprobe,
    ['-v', 'error', '-count_frames', '-show_streams', '-show_format', '-of', 'json', videoPath],
    { timeout: 120_000, maxBuffer: 1024 * 1024, signal },
  )
  const media = probeSchema.parse(JSON.parse(probe.stdout))
  const video = media.streams.find((stream) => stream.codec_type === 'video')
  const audio = media.streams.find((stream) => stream.codec_type === 'audio')
  assert(video && audio, 'Recording must contain video and page audio')
  assert.equal(video.width, 1920)
  assert.equal(video.height, 1080)
  assert.equal(video.avg_frame_rate, '60/1')
  const frames = z.coerce.number().int().positive().parse(video.nb_read_frames)
  const duration = z.coerce.number().min(18).max(180).parse(media.format.duration)
  assert(Math.abs(frames / 60 - duration) < 0.15, 'Frame count and duration disagree')
  const decoded = await execute(
    ffmpeg,
    [
      '-v',
      'error',
      '-threads',
      '1',
      '-i',
      videoPath,
      '-vn',
      '-ac',
      '1',
      '-ar',
      '16000',
      '-t',
      '180',
      '-f',
      's16le',
      'pipe:1',
    ],
    { encoding: 'buffer', timeout: 120_000, maxBuffer: 8 * 1024 * 1024, signal },
  )
  const pcm = Buffer.from(decoded.stdout)
  assert(pcm.length > 16000 * 2 * 18, 'Decoded audio is unexpectedly short')
  let squares = 0,
    peak = 0,
    audibleWindows = 0
  const windowSamples = 1600
  for (let offset = 0; offset + windowSamples * 2 <= pcm.length; offset += windowSamples * 2) {
    let windowSquares = 0
    for (let sample = 0; sample < windowSamples; sample++) {
      const value = pcm.readInt16LE(offset + sample * 2) / 32768
      windowSquares += value * value
      peak = Math.max(peak, Math.abs(value))
    }
    squares += windowSquares
    if (Math.sqrt(windowSquares / windowSamples) > 0.001) audibleWindows++
  }
  const rms = Math.sqrt(squares / (pcm.length / 2))
  const audibleSeconds = audibleWindows / 10
  assert(peak > 0.01 && rms > 0.001, 'Audio stream exists but is silent')
  assert(audibleSeconds >= 8, `Expected sustained real app audio; heard ${audibleSeconds}s`)
  return {
    passed: true,
    width: video.width,
    height: video.height,
    encodedFps: 60,
    decodedVideoFrames: frames,
    durationSeconds: duration,
    videoCodec: video.codec_name,
    audioCodec: audio.codec_name,
    audioSampleRate: audio.sample_rate,
    audioChannels: audio.channels,
    decodedAudioRms: rms,
    decodedAudioPeak: peak,
    audibleSeconds,
    cadenceScope:
      'Encoded 60 FPS and decoded frame count; this does not prove 60 distinct source paints per second.',
  }
}
