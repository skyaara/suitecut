// SPDX-License-Identifier: MIT
import * as z from 'zod'

export const MAX_FRAMES_PER_SECOND = 120
/** CEF OSR capture request: its supported ceiling is an integer. */
export const CaptureFrameRateSchema = z.number().int().min(1).max(MAX_FRAMES_PER_SECOND)
/** Constant encoded output cadence; native resampling supports finite fractions. */
export const OutputFrameRateSchema = z.number().finite().min(1).max(MAX_FRAMES_PER_SECOND)

/** Converts elapsed time to output slots, snapping only floating-point roundoff at boundaries. */
export function videoFramePosition(
  elapsedMs: number,
  framesPerSecond: number,
  originMs = 0,
): number {
  const position = (elapsedMs * framesPerSecond) / 1000
  const nearest = Math.round(position)
  // Subtracting a nonzero clock origin can introduce more error than the elapsed value alone.
  const magnitude = Math.max(1, Math.abs(position), (Math.abs(originMs) * framesPerSecond) / 1000)
  return Math.abs(position - nearest) <= Number.EPSILON * magnitude * 4 ? nearest : position
}

/** Whole PCM samples for this slot; cumulative boundaries prevent drift at rates such as 59 FPS. */
export function audioSamplesForVideoFrame(index: number, framesPerSecond: number): number {
  return (
    audioSampleBoundary(index + 1, framesPerSecond) - audioSampleBoundary(index, framesPerSecond)
  )
}

/** Sample offset on the 48 kHz clock for a video slot. */
export function audioSampleBoundary(index: number, framesPerSecond: number): number {
  return Math.floor((index * 48_000) / framesPerSecond)
}
