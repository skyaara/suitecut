// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  audioSamplesForVideoFrame,
  CaptureFrameRateSchema,
  OutputFrameRateSchema,
} from '../src/frame-rate.js'

describe('native capture ceiling and encoded output cadence', () => {
  it.each([29.97, 59.94, 119.5])(
    'accepts output cadence %s while rejecting it as a CEF integer request',
    (rate) => {
      expect(OutputFrameRateSchema.parse(rate)).toBe(rate)
      expect(() => CaptureFrameRateSchema.parse(rate)).toThrow()
    },
  )
  it.each([Number.NaN, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY, 0, 0.5, 120.001])(
    'rejects out-of-range/nonfinite rate %s in both clocks',
    (rate) => {
      expect(() => OutputFrameRateSchema.parse(rate)).toThrow()
      expect(() => CaptureFrameRateSchema.parse(rate)).toThrow()
    },
  )
  it('preserves the integer 120 FPS capture ceiling and output limit', () => {
    expect(CaptureFrameRateSchema.parse(120)).toBe(120)
    expect(OutputFrameRateSchema.parse(120)).toBe(120)
  })
  it('partitions 48kHz PCM across fractional slots without cumulative sample drift', () => {
    const samples = Array.from({ length: 300 }, (_, index) =>
      audioSamplesForVideoFrame(index, 29.97),
    )
    expect(new Set(samples)).toEqual(new Set([1601, 1602]))
    expect(samples.reduce((sum, count) => sum + count, 0)).toBe(480480)
  })
})
