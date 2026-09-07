import { describe, expect, it } from 'vitest'

import { resolveCaptureLayout } from '../src/capture.js'

describe('capture sizing', () => {
  it('uses the full viewport and keeps encoder dimensions even', () => {
    expect(resolveCaptureLayout({}, { width: 1601, height: 901 }).captureSize).toEqual({
      width: 1600,
      height: 900,
    })
  })

  it('allows a smaller physical source', () => {
    expect(
      resolveCaptureLayout({ size: { width: 1280, height: 720 } }, { width: 1600, height: 900 })
        .captureSize,
    ).toEqual({ width: 1280, height: 720 })
  })

  it('uses an HD composition on a proportional 4K browser surface', () => {
    expect(
      resolveCaptureLayout(
        {
          viewport: { width: 1920, height: 1080 },
          size: { width: 3840, height: 2160 },
        },
        { width: 1600, height: 900 },
      ),
    ).toEqual({
      captureSize: { width: 3840, height: 2160 },
      layoutScale: 2,
      layoutViewport: { width: 1920, height: 1080 },
      surfaceViewport: { width: 3840, height: 2160 },
    })
  })

  it('rejects a larger source with a different aspect ratio', () => {
    expect(() =>
      resolveCaptureLayout(
        {
          viewport: { width: 1920, height: 1080 },
          size: { width: 3840, height: 2000 },
        },
        { width: 1600, height: 900 },
      ),
    ).toThrowError(/same aspect ratio/u)
  })
})
