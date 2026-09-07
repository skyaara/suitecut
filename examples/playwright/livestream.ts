import { record } from 'suitecut'

const streamUrl = process.env.SUITECUT_STREAM_URL
const websiteUrl = process.env.SUITECUT_WEBSITE_URL
if (!streamUrl || !websiteUrl) {
  throw new Error(
    'Set SUITECUT_STREAM_URL to your full RTMP/RTMPS publish URL and SUITECUT_WEBSITE_URL to the website to show.',
  )
}

const controller = new AbortController()
const stop = (): void => controller.abort()
process.once('SIGINT', stop)
process.once('SIGTERM', stop)

try {
  await record(
    'website livestream',
    async ({ page, signal }) => {
      await page.goto(websiteUrl)
      // Add Playwright interactions here, or use the visible browser manually.
      await new Promise<void>((resolve) => {
        if (signal?.aborted === true) resolve()
        else signal?.addEventListener('abort', () => resolve(), { once: true })
      })
    },
    {
      launch: { headless: false },
      capture: {
        viewport: { width: 1280, height: 720 },
        framesPerSecond: 30,
        stream: { url: streamUrl, bitrateKbps: 4500 },
      },
      signal: controller.signal,
    },
  )
} catch (error) {
  if (!controller.signal.aborted) throw error
} finally {
  process.off('SIGINT', stop)
  process.off('SIGTERM', stop)
}
