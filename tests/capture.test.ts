// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { createCaptureScaleFilter } from '../src/capture.js'

describe('native capture scaling', () => {
  it('uses Lanczos without padding for matching aspect ratios', () => {
    expect(
      createCaptureScaleFilter({ width: 3840, height: 2160 }, { width: 1920, height: 1080 }),
    ).toBe('scale=1920:1080:flags=lanczos+accurate_rnd+full_chroma_int,setsar=1')
  })
})
