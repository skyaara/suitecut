import { chromium } from 'playwright'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createWriteStream } from 'node:fs'
import { mkdir, writeFile } from 'node:fs/promises'
import { join } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'

const directory = process.env.MEMORY_DIRECTORY
await mkdir(directory, { recursive: true })
const result = {
  status: 'starting',
  channel: 'GOFNS',
  url: 'https://www.twitch.tv/gofns',
  requestedSeconds: 120,
  backend:
    'Direct X11 + PulseAudio capture of Playwright Chrome; SuiteCut CEF and legacy adapter attempts failed',
  startedAt: new Date().toISOString(),
}
let browser, encoder
try {
  const proxy = process.env.HTTPS_PROXY ?? process.env.https_proxy
  browser = await chromium.launch({
    channel: 'chromium',
    headless: false,
    chromiumSandbox: true,
    ignoreDefaultArgs: ['--mute-audio'],
    args: [
      '--autoplay-policy=no-user-gesture-required',
      '--window-position=0,0',
      '--window-size=1280,720',
    ],
    ...(proxy ? { proxy: { server: proxy, bypass: 'localhost,127.0.0.1' } } : {}),
  })
  result.browserVersion = browser.version()
  const context = await browser.newContext({ viewport: { width: 1280, height: 720 } })
  const page = await context.newPage()
  await page.goto(result.url, { waitUntil: 'domcontentloaded', timeout: 60000 })
  const consent = page.getByRole('button', { name: /accept/i }).first()
  if (await consent.isVisible().catch(() => false)) await consent.click()
  const video = page.locator('video').first()
  await video.waitFor({ timeout: 30000 })
  await video.evaluate((v) => {
    v.muted = false
    v.volume = 0.8
    return v.play().catch(() => {})
  })
  await page.waitForFunction(
    () => {
      const v = document.querySelector('video')
      return v && !v.paused && v.readyState >= 2 && v.videoWidth > 0 && v.currentTime > 1
    },
    undefined,
    { timeout: 90000 },
  )
  const fullscreen = page.locator('[data-a-target="player-fullscreen-button"]:visible').first()
  await fullscreen.click()
  await page.mouse.move(1, 1)
  await delay(4000)
  const state = () =>
    page.evaluate(() => {
      const v = document.querySelector('video')
      return {
        title: document.title,
        time: v.currentTime,
        paused: v.paused,
        readyState: v.readyState,
        muted: v.muted,
        volume: v.volume,
        width: v.videoWidth,
        height: v.videoHeight,
        decodedFrames: v.getVideoPlaybackQuality().totalVideoFrames,
        droppedFrames: v.getVideoPlaybackQuality().droppedVideoFrames,
      }
    })
  result.before = await state()
  await page.screenshot({ path: join(directory, 'playing-start.png') })
  const file = join(directory, 'recorded.mp4')
  encoder = spawn(
    'ffmpeg',
    [
      '-hide_banner',
      '-y',
      '-thread_queue_size',
      '256',
      '-f',
      'x11grab',
      '-framerate',
      '30',
      '-video_size',
      '1280x720',
      '-i',
      process.env.DISPLAY + '+0,0',
      '-thread_queue_size',
      '256',
      '-f',
      'pulse',
      '-i',
      'memory.monitor',
      '-t',
      '120',
      '-map',
      '0:v:0',
      '-map',
      '1:a:0',
      '-vf',
      'fps=30',
      '-af',
      'aresample=async=1:first_pts=0',
      '-c:v',
      'libx264',
      '-preset',
      'veryfast',
      '-crf',
      '21',
      '-threads',
      '4',
      '-pix_fmt',
      'yuv420p',
      '-c:a',
      'aac',
      '-b:a',
      '160k',
      '-movflags',
      '+faststart',
      file,
    ],
    { stdio: ['ignore', 'ignore', 'pipe'] },
  )
  encoder.stderr.pipe(createWriteStream(join(directory, 'encoder.log')))
  const completed = once(encoder, 'close')
  const error = once(encoder, 'error').then(([error]) => {
    throw error
  })
  void error.catch(() => {})
  result.status = 'recording'
  result.recordingStartedAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  console.log('RECORDING_STARTED', result.recordingStartedAt)
  const began = performance.now()
  const timer = setInterval(() => {
    console.log('PROGRESS', Math.round((performance.now() - began) / 1000) + ' seconds')
  }, 30000)
  let code
  try {
    ;[code] = await Promise.race([
      completed,
      error,
      delay(150000).then(() => {
        throw new Error('Recording timeout')
      }),
    ])
  } finally {
    clearInterval(timer)
  }
  if (code !== 0) throw new Error('Display encoder exited with code ' + code)
  result.elapsedMs = performance.now() - began
  result.after = await state()
  if (result.after.time - result.before.time < 115)
    throw new Error('Twitch playback did not advance for the full recording')
  await page.screenshot({ path: join(directory, 'playing-end.png') })
  result.file = file
  result.status = 'recorded'
} catch (error) {
  result.status = 'failed'
  result.error = error.stack
  throw error
} finally {
  if (encoder && encoder.exitCode === null) {
    encoder.kill('SIGTERM')
    await Promise.race([once(encoder, 'close'), delay(3000)])
    if (encoder.exitCode === null) encoder.kill('SIGKILL')
  }
  await browser?.close()
  result.finishedAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  console.log(JSON.stringify(result, null, 2))
}
