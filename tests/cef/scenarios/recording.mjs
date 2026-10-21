// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import { access } from 'node:fs/promises'
import { join } from 'node:path'

const tool = (binary, args) => {
  const result = spawnSync(binary, args, { timeout: 30000, maxBuffer: 16 * 1024 * 1024 })
  if (result.error) throw result.error
  assert.equal(result.status, 0, result.stderr.toString())
  return result.stdout
}

export const recording = [
  {
    id: 'recording.audio-pause-resume-cancel',
    tags: ['media', 'artifacts', 'lifecycle', 'codec'],
    timeout: 120,
    requires: ['ffmpeg', 'ffprobe'],
    async run(h) {
      const { page } = await h.page()
      await page.goto(h.fixture.origin + '/media')
      await page.locator('#audio').click()
      let active
      const run = (action, extra = {}) =>
        page.source.runAutomation('recording', {
          action,
          ...(active ? { recordingId: active.id } : {}),
          ...extra,
        })
      h.onCleanup(async () => {
        if (active) await run('cancel')
      })
      const output = join(h.output, 'recording.mp4')
      active = await run('start', {
        outputPath: output,
        ffmpegPath: h.ffmpeg,
        framesPerSecond: 30,
        width: 960,
        height: 640,
        audio: true,
      })
      await h.until(
        async () => {
          const s = await run('status')
          assert(!s.failure, s.failure)
          return s.outputFrames >= 30 && s.audioInputFrames >= 24000
        },
        'Recorder did not accept actual video/audio',
        15000,
      )
      await run('pause')
      const paused = await run('status'),
        paint = await page.evaluate('globalThis.paints')
      const handle = await page.waitForFunction(`globalThis.paints>=${paint + 20}`)
      await handle.dispose()
      const still = await run('status')
      assert(
        Math.abs(still.durationMs - paused.durationMs) < 100,
        'Recording timeline advanced while paused',
      )
      await run('resume')
      await h.until(
        async () => {
          const s = await run('status')
          assert(!s.failure, s.failure)
          return s.outputFrames >= paused.outputFrames + 30
        },
        'Recorder did not resume actual output',
        15000,
      )
      const completed = await run('stop')
      active = undefined
      assert.equal(completed.phase, 'complete')
      assert.equal(completed.failure, '')
      assert(completed.audioInputFrames > 48000)
      const streams = JSON.parse(
        tool(h.ffprobe, [
          '-v',
          'error',
          '-count_frames',
          '-show_streams',
          '-of',
          'json',
          output,
        ]).toString(),
      ).streams
      const video = streams.find((s) => s.codec_type === 'video'),
        audio = streams.find((s) => s.codec_type === 'audio')
      assert(video && audio, 'Encoded output must contain both streams')
      assert.equal(Number(video.nb_read_frames), completed.outputFrames)
      assert.equal(video.width, 960)
      assert.equal(video.height, 640)
      const pcm = tool(h.ffmpeg, [
        '-v',
        'error',
        '-i',
        output,
        '-map',
        '0:a:0',
        '-ac',
        '1',
        '-ar',
        '48000',
        '-f',
        'f32le',
        'pipe:1',
      ])
      assert(pcm.length > 48000 * 4)
      let energy = 0
      for (let i = 0; i < pcm.length; i += 4) {
        const sample = pcm.readFloatLE(i)
        energy += sample * sample
      }
      assert(Math.sqrt(energy / (pcm.length / 4)) > 0.01, 'Decoded browser audio is silent')
      const canceled = join(h.output, 'canceled.mp4')
      active = await run('start', {
        outputPath: canceled,
        ffmpegPath: h.ffmpeg,
        framesPerSecond: 30,
        width: 960,
        height: 640,
        audio: false,
      })
      await h.until(
        async () => (await run('status')).outputFrames >= 3,
        'Cancel subject did not start',
        10000,
      )
      await run('cancel')
      active = undefined
      await assert.rejects(access(canceled), /ENOENT/)
      h.observe('decoded-recording', {
        completed,
        paused,
        streams,
        audioSamples: pcm.length / 4,
        decodedAudioRms: Math.sqrt(energy / (pcm.length / 4)),
      })
      h.check(
        'Actual encoded frame count matches receipts; pause freezes media time, resume advances output, decoded audio is non-silent, cancel publishes no file',
      )
    },
  },
]
