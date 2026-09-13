import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { mkdir } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import { record } from '../dist/playwright.js'
import { resolveFfmpeg, resolveFfprobe, runProcess } from '../dist/process.js'

const ffmpeg = await resolveFfmpeg()
const ffprobe = await resolveFfprobe(ffmpeg)
const directory = resolve('.suitecut/device-scale-stream', String(Date.now()))
const outputPath = resolve(directory, 'received.flv')
const url = 'rtmp://127.0.0.1:19364/live/device-scale'
await mkdir(directory, { recursive: true })

const receiver = spawn(
  ffmpeg,
  ['-hide_banner', '-loglevel', 'error', '-listen', '1', '-i', url, '-c', 'copy', '-y', outputPath],
  { stdio: ['ignore', 'ignore', 'ignore'] },
)
const receiverClosed = once(receiver, 'close')
try {
  await delay(400)
  await record(
    'device-scale livestream',
    async ({ page, suitecut }) => {
      assert.deepEqual(
        await page.evaluate(() => [
          globalThis.innerWidth,
          globalThis.innerHeight,
          globalThis.devicePixelRatio,
        ]),
        [1920, 1080, 2],
      )
      await page.setContent(
        '<main style="width:100vw;height:100vh;border:1px solid;margin:0;box-sizing:border-box">Device-scale stream</main>',
      )
      assert.equal(await page.evaluate(() => globalThis.document.documentElement.style.zoom), '')
      assert.equal(
        await page.evaluate(() => globalThis.document.documentElement.style.transform),
        '',
      )
      await suitecut.hold(1000)
    },
    {
      capture: {
        viewport: { width: 1920, height: 1080 },
        deviceScaleFactor: 2,
        framesPerSecond: 30,
        quality: 100,
        stream: {
          url,
          size: { width: 1920, height: 1080 },
          bitrateKbps: 2000,
          adaptiveBitrate: false,
          reconnect: false,
        },
      },
      output: {
        directory: resolve(directory, 'source'),
        manifestPath: resolve(directory, 'manifest.json'),
      },
    },
  )
  await receiverClosed
  const probe = await runProcess(
    ffprobe,
    [
      '-v',
      'error',
      '-select_streams',
      'v:0',
      '-show_entries',
      'stream=width,height,r_frame_rate',
      '-of',
      'csv=p=0',
      outputPath,
    ],
    { timeoutMs: 30_000 },
  )
  assert.equal(probe.exitCode, 0)
  assert.equal(probe.stdout.trim(), '1920,1080,30/1')
  process.stdout.write(
    `Verified 3840x2160 browser rendering in a 1920x1080 local RTMP stream: ${outputPath}\n`,
  )
} finally {
  receiver.kill('SIGKILL')
}
