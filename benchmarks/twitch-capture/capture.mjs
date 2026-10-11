import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createWriteStream } from 'node:fs'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:net'
import { join } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'
import { launchTabAudioContext } from '../../dist/tab-audio.js'
import { createRecordingSession } from '../../dist/suitecut.js'
import { rawArtifactSink } from '../../dist/recording-output.js'

const directory = process.env.MEMORY_DIRECTORY
await mkdir(directory, { recursive: true })
const result = {
  channel: 'GOFNS',
  url: 'https://www.twitch.tv/gofns',
  requestedMs: 120000,
  status: 'starting',
  backend: 'SuiteCut Playwright with tab audio; native CEF Twitch playback failed',
  startedAt: new Date().toISOString(),
}
let context, session, receiver
try {
  if (process.env.CEF_CODEC_PROBE === '1') {
    const engine = await launchEngine({
      executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
      width: 1280,
      height: 720,
      framesPerSecond: 30,
    })
    try {
      const page = engine.browser.contexts()[0].pages()[0]
      result.cefCodecSupport = await page.evaluate(() => ({
        h264aac: document
          .createElement('video')
          .canPlayType('video/mp4; codecs="avc1.42E01E, mp4a.40.2"'),
        h264: document.createElement('video').canPlayType('video/mp4; codecs="avc1.42E01E"'),
        aac: document.createElement('audio').canPlayType('audio/mp4; codecs="mp4a.40.2"'),
        vp9opus: document.createElement('video').canPlayType('video/webm; codecs="vp9, opus"'),
        av1: document.createElement('video').canPlayType('video/mp4; codecs="av01.0.05M.08"'),
      }))
      result.cefVersion = engine.source.cefVersion
      console.log('CEF codec support:', JSON.stringify(result.cefCodecSupport))
    } finally {
      await engine.close()
    }
  } else {
    result.cefCodecSupport = JSON.parse(
      await readFile(join(directory, '../capture/result.json'), 'utf8'),
    ).cefCodecSupport
  }
  const proxy = process.env.HTTPS_PROXY ?? process.env.https_proxy
  context = await launchTabAudioContext(
    { headless: false, chromiumSandbox: true },
    {
      viewport: { width: 1280, height: 720 },
      ...(proxy ? { proxy: { server: proxy, bypass: 'localhost,127.0.0.1' } } : {}),
    },
  )
  result.chromiumVersion = context.browser().version()
  const page = context.pages()[0] ?? (await context.newPage())
  page.setDefaultTimeout(15000)
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
  const playerState = () =>
    page.evaluate(() => {
      const v = document.querySelector('video')
      return {
        title: document.title,
        time: v?.currentTime,
        readyState: v?.readyState,
        paused: v?.paused,
        muted: v?.muted,
        volume: v?.volume,
        width: v?.videoWidth,
        height: v?.videoHeight,
        decodedFrames: v?.getVideoPlaybackQuality().totalVideoFrames,
        droppedFrames: v?.getVideoPlaybackQuality().droppedVideoFrames,
        text: document.body.innerText.slice(-5000),
      }
    })
  result.before = await playerState()
  await page.screenshot({ path: join(directory, 'playing-start.png') })
  const probe = createServer()
  await new Promise((ready) => probe.listen(0, '127.0.0.1', ready))
  const port = probe.address().port
  await new Promise((ready) => probe.close(ready))
  const destination = `rtmp://127.0.0.1:${port}/live/twitch`
  receiver = spawn(
    'ffmpeg',
    [
      '-v',
      'error',
      '-y',
      '-listen',
      '1',
      '-i',
      destination,
      '-c',
      'copy',
      join(directory, 'received.flv'),
    ],
    { stdio: ['ignore', 'ignore', 'pipe'] },
  )
  receiver.stderr.pipe(createWriteStream(join(directory, 'receiver.log')))
  const receiverClosed = once(receiver, 'close')
  void receiverClosed.catch(() => {})
  const portHex = port.toString(16).toUpperCase().padStart(4, '0')
  const deadline = performance.now() + 5000
  while (
    !(await readFile('/proc/net/tcp', 'utf8')).split('\n').some((line) => {
      const parts = line.trim().split(/\s+/)
      return parts[1]?.endsWith(`:${portHex}`) && parts[3] === '0A'
    })
  ) {
    if (performance.now() > deadline) throw new Error('Receiver startup timeout')
    await delay(20)
  }
  session = await createRecordingSession({
    page,
    output: rawArtifactSink(directory),
    captureOptions: {
      viewport: { width: 1280, height: 720 },
      size: { width: 1280, height: 720 },
      deviceScaleFactor: 1,
      framesPerSecond: 30,
      stream: { url: destination, audio: true, bitrateKbps: 3500, adaptiveBitrate: false },
    },
  })
  result.status = 'recording'
  result.recordingStartedAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  console.log('RECORDING_STARTED', result.recordingStartedAt)
  const start = performance.now()
  for (let i = 0; i < 4; i++) {
    await Promise.race([
      delay(Math.max(0, start + (i + 1) * 30000 - performance.now())),
      session.streamFailure,
    ])
    const state = await playerState()
    console.log(
      'PROGRESS',
      JSON.stringify({ elapsedMs: performance.now() - start, ...state, text: undefined }),
    )
    await writeFile(join(directory, `player-${i + 1}.json`), JSON.stringify(state, null, 2))
    if (state.paused || !state.width || state.readyState < 2)
      throw new Error('Twitch playback stalled during recording')
  }
  result.elapsedMs = performance.now() - start
  result.after = await playerState()
  await page.screenshot({ path: join(directory, 'playing-end.png') })
  await session.endRecording()
  result.attachment = await session.seal()
  await Promise.race([
    receiverClosed,
    delay(10000).then(() => {
      throw new Error('Receiver did not finalize')
    }),
  ])
  result.status = 'recorded'
} catch (error) {
  result.status = 'failed'
  result.error = error.stack
  throw error
} finally {
  await session?.endRecording().catch(() => {})
  await context?.close()
  if (receiver && receiver.exitCode === null) receiver.kill('SIGTERM')
  result.finishedAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  console.log(JSON.stringify(result, null, 2))
}
