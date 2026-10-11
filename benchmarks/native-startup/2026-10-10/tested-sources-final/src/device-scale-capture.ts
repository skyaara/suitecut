import { setTimeout as delay } from 'node:timers/promises'

import { type Page } from 'playwright'

export interface SuiteCutCapturedFrame {
  data: Buffer
  timestamp: number
}

export interface SuiteCutFrameCapture {
  readonly firstFrame: Promise<void>
  stop(): Promise<void>
}

/** Captures device-pixel JPEGs because Chromium's screencast stream is CSS-pixel sized. */
export function startDeviceScaleFrameCapture(
  page: Page,
  framesPerSecond: number,
  quality: number,
  onFrame: (frame: SuiteCutCapturedFrame) => Promise<void> | void,
  signal: AbortSignal,
): SuiteCutFrameCapture {
  const controller = new AbortController()
  const combined = AbortSignal.any([signal, controller.signal])
  const frameDurationMs = 1_000 / framesPerSecond
  let resolveFirstFrame = (): void => undefined
  let rejectFirstFrame = (error: Error): void => {
    void error
  }
  const firstFrame = new Promise<void>((resolve, reject) => {
    resolveFirstFrame = resolve
    rejectFirstFrame = reject
  })
  void firstFrame.catch(() => undefined)

  const completion = (async (): Promise<void> => {
    let capturedFirstFrame = false
    try {
      while (!combined.aborted && !page.isClosed()) {
        const startedAt = performance.now()
        const data = await page.screenshot({
          animations: 'allow',
          caret: 'initial',
          fullPage: false,
          quality,
          scale: 'device',
          type: 'jpeg',
        })
        if (combined.aborted || page.isClosed()) break
        await onFrame({ data, timestamp: Date.now() })
        if (!capturedFirstFrame) {
          capturedFirstFrame = true
          resolveFirstFrame()
        }
        const remainingMs = frameDurationMs - (performance.now() - startedAt)
        if (remainingMs > 0) {
          await delay(remainingMs, undefined, { signal: combined })
        }
      }
    } catch (error) {
      if (combined.aborted || page.isClosed()) return
      const captureError =
        error instanceof Error
          ? error
          : new Error('SuiteCut device-scale frame capture failed with a non-Error value')
      rejectFirstFrame(captureError)
      throw captureError
    }
  })()
  void completion.catch(() => undefined)

  return {
    firstFrame,
    stop: async () => {
      controller.abort()
      await completion
    },
  }
}
