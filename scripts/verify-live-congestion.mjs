import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { spawn } from 'node:child_process'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import { setInterval, clearInterval } from 'node:timers'
import { setTimeout as delay } from 'node:timers/promises'

import { chromium } from 'playwright'
import { z } from 'zod'

import { createLiveAudioTransport } from '../dist/live-audio.js'
import { createLiveStreamAttempt } from '../dist/live-stream.js'
import { resolveFfmpeg, resolveFfprobe, runProcess } from '../dist/process.js'

const dir = resolve('.suitecut/congestion-verification', String(Date.now()))
await mkdir(dir, { recursive: true })
const ffmpeg = await resolveFfmpeg()
const url = 'rtmp://127.0.0.1:19387/live/congestion'
const receiver = spawn(
  ffmpeg,
  [
    '-hide_banner',
    '-loglevel',
    'error',
    '-listen',
    '1',
    '-i',
    url,
    '-c',
    'copy',
    '-y',
    `${dir}/received.flv`,
  ],
  { stdio: 'ignore' },
)
const received = once(receiver, 'close')
const browser = await chromium.launch()
const audio = await createLiveAudioTransport(
  () => undefined,
  () => undefined,
)
// JavaScript entry point requires explicit types for mutable state.
// eslint-disable-next-line jsdoc/check-tag-names
/** @type {import('../dist/live-stream.js').SuiteCutLiveStream | undefined} */
let stream
let audioTimer, updateTimer
// JavaScript entry point requires explicit types for mutable state.
// eslint-disable-next-line jsdoc/check-tag-names
/** @type {import('../dist/live-congestion.js').LiveStreamDiagnostic[]} */
const diagnostics = []
let stall = 0
let hang = false
try {
  const page = await browser.newPage({ viewport: { width: 1920, height: 1080 } })
  await page.setContent(
    '<body style="background:#3181ab;color:white"><h1>Congestion recovery test</h1></body>',
  )
  const jpeg = await page.screenshot({ type: 'jpeg' })
  const pcm = Buffer.alloc(3840)
  for (let i = 0; i < 960; i++) {
    const value = Math.round(4000 * Math.sin((2 * Math.PI * 500 * i) / 48000))
    pcm.writeInt16LE(value, i * 4)
    pcm.writeInt16LE(value, i * 4 + 2)
  }
  audioTimer = setInterval(() => audio.buffer.push(performance.now(), pcm), 20)
  stream = createLiveStreamAttempt(
    ffmpeg,
    {
      url,
      audio: 'tab',
      reconnect: false,
      bitrateKbps: 6000,
      onDiagnostic: (e) => diagnostics.push(e),
    },
    60,
    { width: 1920, height: 1080 },
    undefined,
    async (frame) => {
      if (hang) await new Promise(() => undefined)
      if (stall) {
        const ms = stall
        stall = 0
        await delay(ms)
      }
      return frame
    },
    audio,
  )
  let failure
  void stream.failure.catch((e) => {
    failure = String(e)
  })
  stream.update(jpeg)
  updateTimer = setInterval(() => stream?.update(jpeg), 17)
  await stream.ready()
  for (const ms of [1500, 6000, 11000]) {
    stall = ms
    await delay(ms + 3000)
    assert.equal(failure, undefined, `Encoder restarted or failed after ${ms}ms processing stall`)
  }
  assert(diagnostics.some((e) => e.event === 'recovering' && e.bottleneck === 'frame-processing'))
  assert(diagnostics.some((e) => e.event === 'resynchronized' && e.skippedMs >= 10_000))
  hang = true
  await Promise.race([
    stream.failure.catch(() => undefined),
    delay(25_000).then(() => {
      throw new Error('No-progress watchdog did not stop a hung processor')
    }),
  ])
  assert(diagnostics.some((e) => e.event === 'stalled' && e.bottleneck === 'frame-processing'))
  const stopAt = performance.now()
  await stream.stop().catch(() => undefined)
  assert(performance.now() - stopAt < 8000, 'Shutdown waited on a hung frame processor')
  await received
  const result = await runProcess(await resolveFfprobe(ffmpeg), [
    '-v',
    'error',
    '-show_streams',
    '-show_format',
    '-of',
    'json',
    `${dir}/received.flv`,
  ])
  const probe = z
    .object({
      streams: z.array(
        z.object({
          codec_name: z.string(),
          width: z.number().optional(),
          height: z.number().optional(),
        }),
      ),
      format: z.object({ duration: z.string() }),
    })
    .parse(JSON.parse(result.stdout))
  assert(
    probe.streams.some((s) => s.codec_name === 'h264' && s.width === 1920 && s.height === 1080),
  )
  assert(probe.streams.some((s) => s.codec_name === 'aac'))
  await writeFile(
    `${dir}/result.json`,
    JSON.stringify(
      {
        status: 'passed',
        sameEncoder: true,
        hungProcessorStopped: true,
        stallsMs: [1500, 6000, 11000],
        diagnostics,
        receivedSeconds: Number(probe.format.duration),
      },
      null,
      2,
    ),
  )
  console.log(
    `PASS: same 1080p60 encoder survived 1.5s, 6s and 11s stalls; hung processor stopped by watchdog. ${dir}`,
  )
} finally {
  clearInterval(audioTimer)
  clearInterval(updateTimer)
  await stream?.stop().catch(() => undefined)
  await audio.close()
  await browser.close()
  receiver.kill('SIGTERM')
}
