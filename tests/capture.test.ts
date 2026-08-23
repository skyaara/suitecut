import { describe, expect, it } from 'vitest'

import { resolveCaptureSize } from '../src/capture.js'

describe('capture sizing', () => {
  it('uses the full viewport and keeps encoder dimensions even', () => {
    expect(resolveCaptureSize({}, { width: 1601, height: 901 })).toEqual({
      width: 1600,
      height: 900,
    })
  })

  it('allows a smaller physical source', () => {
    expect(
      resolveCaptureSize({ size: { width: 1280, height: 720 } }, { width: 1600, height: 900 }),
    ).toEqual({ width: 1280, height: 720 })
  })

  it('rejects a source size larger than the Playwright viewport', () => {
    expect(() =>
      resolveCaptureSize({ size: { width: 3840, height: 2160 } }, { width: 1600, height: 900 }),
    ).toThrowError(/request 4K from the renderer/u)
  })
})
