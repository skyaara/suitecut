import { setTimeout as delay } from 'node:timers/promises'

import imageSharp from 'sharp'

import { type SuiteCutZoomEvent } from './types.js'
import {
  resolveSuiteCutZoomOptions,
  suiteCutEasingProgress,
  suiteCutZoomFrameForTarget,
} from './zoom.js'

/** Crops live JPEGs without altering application layout or recorded source frames. */
export function createLiveCamera(signal?: AbortSignal) {
  const controller = new AbortController()
  const combined = signal ? AbortSignal.any([signal, controller.signal]) : controller.signal
  let active: { event: SuiteCutZoomEvent; startedAt: number } | undefined
  let cached: { input: Buffer; key: string; output: Buffer } | undefined
  return {
    zoom: async (event: SuiteCutZoomEvent): Promise<void> => {
      combined.throwIfAborted()
      const animation = { event, startedAt: performance.now() }
      active = animation
      try {
        await delay(resolveSuiteCutZoomOptions(event.options).totalDurationMs, undefined, {
          signal: combined,
        })
      } finally {
        if (active === animation) {
          active = undefined
          cached = undefined
        }
      }
    },
    render: async (data: Buffer, pageId: string): Promise<Buffer> => {
      const animation = active
      if (animation?.event.pageId !== pageId) {
        cached = undefined
        return data
      }
      const { event } = animation
      const options = resolveSuiteCutZoomOptions(event.options)
      const elapsed = performance.now() - animation.startedAt
      if (elapsed >= options.totalDurationMs) {
        cached = undefined
        return data
      }
      let amount: number
      if (elapsed < options.enter.durationMs)
        amount = suiteCutEasingProgress(options.enter.easing, elapsed / options.enter.durationMs)
      else if (elapsed < options.enter.durationMs + options.holdMs) amount = 1
      else
        amount =
          options.exit.durationMs === 0
            ? 0
            : 1 -
              suiteCutEasingProgress(
                options.exit.easing,
                (elapsed - options.enter.durationMs - options.holdMs) / options.exit.durationMs,
              )
      if (amount <= 0) return data
      const target = suiteCutZoomFrameForTarget(options, event.rect, event.viewport)
      const scale = 1 + (target.scale - 1) * amount
      if (scale <= 1) return data
      const centerX =
        event.viewport.width / 2 + (target.centerX - event.viewport.width / 2) * amount
      const centerY =
        event.viewport.height / 2 + (target.centerY - event.viewport.height / 2) * amount
      const metadata = await imageSharp(data).metadata()
      const width = metadata.width
      const height = metadata.height
      const cropWidth = Math.max(1, Math.round(width / scale))
      const cropHeight = Math.max(1, Math.round(height / scale))
      const left = Math.max(
        0,
        Math.min(
          width - cropWidth,
          Math.round((centerX / event.viewport.width) * width - cropWidth / 2),
        ),
      )
      const top = Math.max(
        0,
        Math.min(
          height - cropHeight,
          Math.round((centerY / event.viewport.height) * height - cropHeight / 2),
        ),
      )
      const key = `${left}:${top}:${cropWidth}:${cropHeight}`
      if (cached?.input === data && cached.key === key) return cached.output
      const output = await imageSharp(data)
        .extract({ left, top, width: cropWidth, height: cropHeight })
        .resize(width, height)
        .jpeg({ quality: 95 })
        .toBuffer()
      if (!combined.aborted && active === animation) cached = { input: data, key, output }
      return output
    },
    stop: (): void => {
      controller.abort()
      active = undefined
      cached = undefined
    },
  }
}
