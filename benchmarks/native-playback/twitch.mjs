import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { createWriteStream } from 'node:fs'
import { mkdir, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'

const directory = resolve(process.env.MEMORY_DIRECTORY)
const requestedMs = Number(process.env.SUITECUT_TWITCH_DURATION_MS ?? 120000)
assert(Number.isInteger(requestedMs) && requestedMs >= 5000 && requestedMs <= 120000)
await mkdir(directory, { recursive: true })
const result = {
  status: 'starting',
  channel: 'GOFNS',
  url: 'https://www.twitch.tv/gofns',
  requestedMs,
  backend:
    'SuiteCut CEF OnPaint + native audio callbacks + C++ libav recording; no raw frame/audio subscribers, Chrome fallback, X11 screen capture or browser extension',
  startedAt: new Date().toISOString(),
}
let engine, output, sampler
const stats = createWriteStream(join(directory, 'output-status.jsonl'))
try {
  engine = await launchEngine({
    executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
    width: 1280,
    height: 720,
    framesPerSecond: 30,
  })
  result.cefVersion = engine.source.cefVersion
  const proxy = process.env.HTTPS_PROXY ?? process.env.https_proxy
  const context = await engine.browser.newContext(
    proxy ? { proxy: { server: proxy, bypass: 'localhost,127.0.0.1' } } : {},
  )
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
  if (await fullscreen.isVisible()) await fullscreen.click()
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
  output = await engine.record(page, {
    destination: join(directory, 'recorded.mp4'),
    width: 1280,
    height: 720,
    fps: 30,
    videoCodec: 'libx264',
    audioCodec: 'aac',
  })
  sampler = spawn(
    'python3',
    [
      'benchmarks/obs-cef/profile-pipeline.py',
      join(directory, 'processes.jsonl'),
      String(process.pid),
    ],
    { stdio: 'ignore' },
  )
  result.status = 'recording'
  result.recordingStartedAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  console.log('RECORDING_STARTED', result.recordingStartedAt)
  const began = performance.now()
  let nextProgress = 30000
  while (performance.now() - began < requestedMs) {
    await Promise.race([
      delay(Math.min(1000, requestedMs - (performance.now() - began))),
      output.failure,
    ])
    const atMs = performance.now() - began
    const status = await output.status()
    assert.equal(status.error, '')
    stats.write(JSON.stringify({ atMs, output: status }) + '\n')
    if (atMs >= nextProgress) {
      console.log('PROGRESS', Math.round(atMs / 1000) + ' seconds')
      nextProgress += 30000
    }
  }
  result.elapsedMs = performance.now() - began
  result.final = await output.stop()
  assert.equal(result.final.error, '')
  assert.equal(result.final.queuedVideoBytes + result.final.queuedAudioBytes, 0)
  result.after = await state()
  assert(
    result.after.time - result.before.time > requestedMs / 1000 - 5,
    'Twitch playback did not advance for the requested duration',
  )
  await page.screenshot({ path: join(directory, 'playing-end.png') })
  result.status = 'recorded'
} catch (error) {
  result.status = 'failed'
  result.error = error.stack
  throw error
} finally {
  sampler?.kill('SIGTERM')
  await output?.stop().catch(() => {})
  await engine?.close()
  stats.end()
  result.finishedAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  console.log(JSON.stringify(result, null, 2))
}
