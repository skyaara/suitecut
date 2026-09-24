import { record } from 'suitecut'

const streamUrl = process.env.SUITECUT_STREAM_URL
if (!streamUrl) {
  throw new Error('Set SUITECUT_STREAM_URL to your full RTMP or RTMPS publish URL.')
}

const statusUrl = process.env.SUITECUT_STATUS_URL ?? 'https://status.openai.com/'
const headed = process.env.SUITECUT_HEADED === '1'
const controller = new AbortController()
const stop = (): void => controller.abort()
process.once('SIGINT', stop)
process.once('SIGTERM', stop)

try {
  await record(
    'OpenAI Status livestream',
    async ({ page, suitecut, signal }) => {
      await page.goto(statusUrl, { waitUntil: 'domcontentloaded' })
      await suitecut.hold(2_000)

      const systemStatus = page.getByRole('heading', { name: 'System status' })
      if ((await systemStatus.count()) > 0) {
        await suitecut.scrollTo(systemStatus, { behavior: 'smooth', settleMs: 800 })
        await suitecut.highlight(systemStatus, { durationMs: 1_200 })
      }

      await new Promise<void>((resolve) => {
        if (signal?.aborted === true) resolve()
        else signal?.addEventListener('abort', () => resolve(), { once: true })
      })
    },
    {
      launch: { headless: !headed },
      capture: {
        viewport: { width: 1920, height: 1080 },
        framesPerSecond: 30,
        stream: {
          url: streamUrl,
          audio: true,
          bitrateKbps: 4500,
          onDiagnostic: (event) => console.log(event.event, event.bottleneck),
          reconnect: { initialDelayMs: 1_000, maxDelayMs: 30_000, maxAttempts: 0 },
        },
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
