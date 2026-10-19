/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-argument -- Runtime public SDK and in-page observations. */
/* global document, location */
import { writeFile, mkdir } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

// Read-only anonymous site smoke check. Never bypasses TLS, signs in or submits data.
const output = resolve(process.env.SUITECUT_TWITCH_OUTPUT ?? '.suitecut/real-world-review/twitch')
await mkdir(output, { recursive: true })
const { launchBrowser } = await import(
  pathToFileURL(resolve('packages/browser-control/dist/browser.js'))
)
const proxy = process.env.HTTPS_PROXY ?? process.env.HTTP_PROXY
const results = {
  started: new Date().toISOString(),
  target: process.env.SUITECUT_TWITCH_URL ?? 'https://www.twitch.tv/alveussanctuary',
  scope:
    'Anonymous public channel smoke check; channel may be offline. Page rendering alone does not prove playback.',
  backends: [],
}
const backends = (process.env.SUITECUT_TWITCH_BACKENDS ?? 'cdp,cef').split(',')
if (backends.some((backend) => !['cdp', 'cef'].includes(backend)))
  throw new Error('Twitch backends must be cdp and/or cef')
for (const backend of backends) {
  const record = { backend, samples: [], interactions: [] }
  results.backends.push(record)
  let browser
  const controller = new AbortController()
  const timer = globalThis.setTimeout(
    () => controller.abort(new Error('Twitch probe deadline')),
    65_000,
  )
  try {
    browser = await launchBrowser(
      backend === 'cef'
        ? {
            executablePath: resolve(
              process.env.SUITECUT_NATIVE_EXECUTABLE ??
                '.suitecut/memory-deep/runtime/suitecut-browser',
            ),
            headless: true,
            width: 1280,
            height: 720,
            framesPerSecond: 30,
            signal: controller.signal,
          }
        : {
            backend,
            executablePath: resolve(
              process.env.SUITECUT_CHROME_EXECUTABLE ??
                '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
            ),
            headless: true,
            args: [
              '--no-sandbox',
              '--disable-dev-shm-usage',
              ...(proxy ? [`--proxy-server=${proxy}`] : []),
            ],
            timeoutMs: 15_000,
            viewport: { width: 1280, height: 720 },
            signal: controller.signal,
          },
    )
    record.version = browser.version()
    const context = await browser.newContext(
      backend === 'cef' && proxy ? { proxy: { server: proxy } } : {},
    )
    const page = await context.newPage()
    page.setDefaultTimeout(3000)
    try {
      await page.goto(results.target, { waitUntil: 'domcontentloaded' })
    } catch (error) {
      record.navigationError = String(error)
    }
    for (let sample = 0; sample < 4; sample++) {
      if (sample) await delay(6000)
      record.samples.push(
        await page.evaluate(() => ({
          title: document.title,
          url: location.origin + location.pathname,
          text: document.body?.innerText.slice(0, 4000),
          videos: Array.from(document.querySelectorAll('video')).map((video) => ({
            currentTime: video.currentTime,
            paused: video.paused,
            readyState: video.readyState,
            networkState: video.networkState,
            muted: video.muted,
            width: video.videoWidth,
            height: video.videoHeight,
            decodedFrames:
              video.getVideoPlaybackQuality?.().totalVideoFrames ??
              video.webkitDecodedFrameCount ??
              null,
            error: video.error ? { code: video.error.code, message: video.error.message } : null,
          })),
        })),
      )
      if (sample === 1) {
        for (const selector of [
          'button[data-a-target="consent-banner-accept"]',
          'button[data-a-target="player-play-pause-button"]',
        ]) {
          const button = page.locator(selector)
          if ((await button.count()) === 1 && (await button.isVisible())) {
            const label = await button.getAttribute('aria-label')
            if (
              selector.includes('consent') ||
              (/play/i.test(label ?? '') && !/pause/i.test(label ?? ''))
            ) {
              try {
                await button.click()
                record.interactions.push({ selector, label, clicked: true })
              } catch (error) {
                record.interactions.push({ selector, error: String(error) })
              }
            }
          }
        }
      }
    }
    record.playbackVerified = record.samples.some(
      (s, i) =>
        i > 0 &&
        s.videos.some(
          (v, j) =>
            v.currentTime > (record.samples[i - 1].videos[j]?.currentTime ?? Infinity) + 0.5 &&
            v.decodedFrames > (record.samples[i - 1].videos[j]?.decodedFrames ?? Infinity) &&
            v.width > 0,
        ),
    )
    try {
      const image = await page.screenshot(backend === 'cef' ? { timeout: 5000 } : {})
      await writeFile(resolve(output, `${backend}.png`), image)
    } catch (error) {
      record.screenshotError = String(error)
    }
  } catch (error) {
    record.error = String(error)
  } finally {
    if (browser) {
      try {
        await browser.close()
        record.closed = true
      } catch (error) {
        record.closeError = String(error)
      }
      if (backend === 'cdp') record.wireAfterClose = browser.connectionStats()
    }
    globalThis.clearTimeout(timer)
    await writeFile(resolve(output, 'results.json'), JSON.stringify(results, null, 2) + '\n')
  }
  process.stdout.write(
    JSON.stringify({
      backend,
      playbackVerified: record.playbackVerified,
      error: record.error,
      lastSample: record.samples.at(-1),
    }) + '\n',
  )
}
