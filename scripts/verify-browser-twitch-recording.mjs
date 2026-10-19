// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-argument -- Records a public anonymous live page through selectable emitted SDKs and inspects actual media state. */
/* global document, location, MediaSource */
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve, join } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const output = resolve(process.env.SUITECUT_TWITCH_OUTPUT ?? '.suitecut/twitch-recording')
await mkdir(output, { recursive: true })
const sdkPath = resolve('packages/browser-control/dist/browser.js')
const { launchBrowser } = await import(pathToFileURL(sdkPath))
const proxy = process.env.HTTPS_PROXY ?? process.env.HTTP_PROXY
const target = process.env.SUITECUT_TWITCH_URL ?? 'https://www.twitch.tv/alveussanctuary'
const nativeExecutable = resolve('.suitecut/engine-performance-fixes/runtime/suitecut-browser')
const chromeExecutable = resolve('.suitecut/agent-comparison/competitor/chrome-linux64/chrome')
const result = {
  started: new Date().toISOString(),
  target,
  scope:
    'Anonymous public live channel, normal TLS/proxy, no sign-in/chat or media-URL extraction. CEF records real native paint/audio into VP9/Opus WebM. CDP control records viewport PNG samples at3FPS without audio.',
  backends: [],
}
const inspect = (cmd, args) => {
  const child = spawnSync(cmd, args, {
    encoding: 'utf8',
    timeout: 45000,
    maxBuffer: 4 * 1024 * 1024,
  })
  assert.equal(child.status, 0, child.stderr)
  return child.stdout
}
const snapshot = () => ({
  title: document.title,
  url: location.origin + location.pathname,
  text: document.body?.innerText.slice(0, 12000),
  video: Array.from(document.querySelectorAll('video')).map((video) => ({
    currentTime: video.currentTime,
    paused: video.paused,
    readyState: video.readyState,
    networkState: video.networkState,
    muted: video.muted,
    volume: video.volume,
    width: video.videoWidth,
    height: video.videoHeight,
    decodedFrames:
      video.getVideoPlaybackQuality?.().totalVideoFrames ?? video.webkitDecodedFrameCount ?? 0,
    droppedFrames: video.getVideoPlaybackQuality?.().droppedVideoFrames ?? null,
    audioDecodedBytes: video.webkitAudioDecodedByteCount ?? null,
    error: video.error ? { code: video.error.code, message: video.error.message } : null,
    h264: video.canPlayType('video/mp4; codecs="avc1.42E01E"'),
    aac: video.canPlayType('audio/mp4; codecs="mp4a.40.2"'),
    mseH264AAC:
      typeof MediaSource !== 'undefined' &&
      MediaSource.isTypeSupported('video/mp4; codecs="avc1.42E01E,mp4a.40.2"'),
  })),
})
for (const backend of ['cef', 'cdp']) {
  const directory = join(output, backend)
  await mkdir(directory, { recursive: true })
  const rec = { backend, samples: [], interactions: [], errors: [] }
  result.backends.push(rec)
  let browser,
    page,
    recording,
    recordingId,
    finalized = false
  const controller = new AbortController()
  const timer = globalThis.setTimeout(
    () => controller.abort(new Error('Twitch recording deadline')),
    100000,
  )
  try {
    const executablePath = backend === 'cef' ? nativeExecutable : chromeExecutable
    rec.executableSHA256 = createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex')
    browser = await launchBrowser(
      backend === 'cef'
        ? {
            executablePath,
            width: 1280,
            height: 720,
            framesPerSecond: 30,
            headless: true,
            signal: controller.signal,
          }
        : {
            backend,
            executablePath,
            viewport: { width: 1280, height: 720 },
            headless: true,
            args: [
              ...(process.getuid?.() === 0 ? ['--no-sandbox'] : []),
              ...(proxy ? [`--proxy-server=${proxy}`] : []),
            ],
            signal: controller.signal,
          },
    )
    rec.version = browser.version()
    const context = await browser.newContext(
      backend === 'cef' && proxy ? { proxy: { server: proxy } } : {},
    )
    page = await context.newPage()
    page.setDefaultTimeout(5000)
    try {
      await page.goto(target, { waitUntil: 'domcontentloaded', timeout: 25000 })
    } catch (error) {
      rec.errors.push({ stage: 'navigate', message: String(error) })
    }
    // A bounded startup observation; no client token, direct HLS URL or unsupported playback flag is supplied.
    for (let attempt = 0; attempt < 5; attempt++) {
      await delay(2000)
      const state = await page.evaluate(snapshot)
      rec.samples.push({ at: performance.now(), ...state })
      for (const selector of [
        'button[data-a-target="consent-banner-accept"]',
        'button[data-a-target="player-play-pause-button"]',
      ]) {
        const button = page.locator(selector)
        try {
          if ((await button.count()) === 1 && (await button.isVisible())) {
            const label = await button.getAttribute('aria-label')
            if (
              selector.includes('consent') ||
              (/play/iu.test(label ?? '') && !/pause/iu.test(label ?? ''))
            ) {
              await button.click()
              rec.interactions.push({ selector, label })
            }
          }
        } catch (error) {
          rec.errors.push({ stage: 'control', message: String(error) })
        }
      }
      if (
        state.video.some((v) => v.decodedFrames > 0 && v.currentTime > 0.1) ||
        /Error #4000/u.test(state.text ?? '')
      )
        break
    }
    await writeFile(
      join(directory, 'before.png'),
      await page.screenshot(backend === 'cef' ? { timeout: 10000 } : {}),
    )
    const before = await page.evaluate(snapshot)
    rec.recordingBefore = before
    const recordStarted = performance.now()
    if (backend === 'cef') {
      const first = await page.source.runRecording({
        action: 'start',
        ffmpegPath: '/usr/bin/ffmpeg',
        outputPath: join(directory, 'twitch-native.webm'),
        width: 1280,
        height: 720,
        framesPerSecond: 30,
        audio: true,
        timeoutMs: 30000,
      })
      recordingId = first.id
      recording = first
      rec.recordingStart = first
      for (let i = 0; i < 6; i++) {
        await delay(2000)
        rec.samples.push({ at: performance.now(), ...(await page.evaluate(snapshot)) })
      }
      rec.recordingStop = await page.source.runRecording({
        action: 'stop',
        recordingId,
        timeoutMs: 60000,
      })
      finalized = true
      rec.recordingAfter = await page.evaluate(snapshot)
      rec.videoPath = join(directory, 'twitch-native.webm')
    } else {
      const frameDirectory = join(directory, 'frames')
      await mkdir(frameDirectory, { recursive: true })
      for (let i = 0; i < 36; i++) {
        await writeFile(
          join(frameDirectory, `frame-${String(i).padStart(4, '0')}.png`),
          await page.screenshot(),
        )
        if (i % 6 === 0)
          rec.samples.push({ at: performance.now(), ...(await page.evaluate(snapshot)) })
        const remaining = recordStarted + (i + 1) * (1000 / 3) - performance.now()
        if (remaining > 0) await delay(remaining)
      }
      rec.recordingAfter = await page.evaluate(snapshot)
      rec.videoPath = join(directory, 'twitch-chrome-silent.webm')
      inspect('/usr/bin/ffmpeg', [
        '-v',
        'error',
        '-framerate',
        '3',
        '-i',
        join(frameDirectory, 'frame-%04d.png'),
        '-an',
        '-c:v',
        'libvpx-vp9',
        '-deadline',
        'realtime',
        '-cpu-used',
        '5',
        '-threads',
        '2',
        '-pix_fmt',
        'yuv420p',
        '-y',
        rec.videoPath,
      ])
    }
    rec.recordingWallMs = performance.now() - recordStarted
    const after = rec.recordingAfter ?? (await page.evaluate(snapshot))
    rec.recordingAfter = after
    rec.playbackVerified = after.video.some(
      (v, i) =>
        v.width > 0 &&
        !v.error &&
        v.currentTime > (before.video[i]?.currentTime ?? Infinity) + 1 &&
        v.decodedFrames > (before.video[i]?.decodedFrames ?? Infinity) + 10,
    )
    rec.liveIndicatorVisible = /\bLIVE\b/u.test(after.text ?? '')
    rec.recordingInspection = JSON.parse(
      inspect('/usr/bin/ffprobe', [
        '-v',
        'error',
        '-count_frames',
        '-show_entries',
        'format=duration,size:stream=codec_type,codec_name,width,height,nb_read_frames,r_frame_rate,sample_rate,channels',
        '-of',
        'json',
        rec.videoPath,
      ]),
    )
    rec.recordingSHA256 = createHash('sha256')
      .update(await readFile(rec.videoPath))
      .digest('hex')
    inspect('/usr/bin/ffmpeg', [
      '-v',
      'error',
      '-ss',
      '5',
      '-i',
      rec.videoPath,
      '-frames:v',
      '1',
      '-y',
      join(directory, 'recording-frame.png'),
    ])
    try {
      await writeFile(
        join(directory, 'after.png'),
        await page.screenshot(backend === 'cef' ? { timeout: 10000 } : {}),
      )
    } catch (error) {
      rec.errors.push({ stage: 'post-recording-screenshot', message: String(error) })
    }
  } catch (error) {
    rec.errors.push({ stage: 'probe', message: String(error), stack: error.stack })
  } finally {
    if (recording && !finalized)
      try {
        await page.source.runRecording({ action: 'stop', recordingId, timeoutMs: 15000 })
      } catch {
        /* Browser close owns remaining encoder retirement. */
      }
    try {
      await browser?.close()
      rec.closed = true
    } catch (error) {
      rec.errors.push({ stage: 'close', message: String(error) })
    }
    globalThis.clearTimeout(timer)
    await writeFile(join(output, 'results.json'), JSON.stringify(result, null, 2) + '\n')
    console.log(
      JSON.stringify({
        backend,
        playbackVerified: rec.playbackVerified,
        video: rec.videoPath,
        errors: rec.errors,
      }),
    )
  }
}
