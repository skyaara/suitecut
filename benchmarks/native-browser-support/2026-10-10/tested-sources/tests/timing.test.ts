import { describe, expect, it } from 'vitest'

import { deriveSourceStartedAt, toAttemptTime, toSourceVideoTime } from '../src/timing.js'

describe('attempt clock mapping', () => {
  it('maps epoch timestamps onto the attempt clock', () => {
    expect(toAttemptTime(10_425, 10_000)).toBe(425)
  })

  it('preserves negative times for setup before the attempt origin', () => {
    expect(toAttemptTime(9_975, 10_000)).toBe(-25)
  })

  it('derives the source start from the first presented frame', () => {
    expect(deriveSourceStartedAt(10_180, 10_000)).toBe(180)
  })

  it('maps attempt time onto source-video time', () => {
    expect(toSourceVideoTime(2_180, 180)).toBe(2_000)
    expect(toSourceVideoTime(100, 180)).toBe(-80)
  })

  it.each([
    ['attempt epoch', () => toAttemptTime(Number.NaN, 10_000)],
    ['attempt origin', () => toAttemptTime(10_100, Number.POSITIVE_INFINITY)],
    ['first frame', () => deriveSourceStartedAt(Number.NaN, 10_000)],
    ['source start', () => toSourceVideoTime(100, Number.NEGATIVE_INFINITY)],
  ])('rejects a non-finite %s value', (_name, operation) => {
    expect(operation).toThrow(RangeError)
  })
})
