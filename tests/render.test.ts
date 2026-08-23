import { describe, expect, it } from 'vitest'
import * as z from 'zod'

import { renderSuiteCut, type SuiteCutRenderRequest } from '../src/render.js'

const baseRequest = {
  manifestPath: '/suitecut-tests/missing-manifest.json',
  outputPath: '/suitecut-tests/output.mp4',
} as const

describe('render request validation', () => {
  it('accepts the typed 4K quality profiles', async () => {
    const request = {
      ...baseRequest,
      config: {
        output: {
          width: 3840,
          height: 2160,
          framesPerSecond: 60,
          quality: 'master',
        },
      },
    } satisfies SuiteCutRenderRequest

    await expect(renderSuiteCut(request)).rejects.not.toBeInstanceOf(z.ZodError)
  })

  it.each([
    { output: { width: 3840 } },
    { output: { width: 3839, height: 2160 } },
    { output: { width: 3840, height: 2159 } },
    { output: { width: 3840, height: 2160, quality: 18 } },
    { output: { width: 3840, height: 2160, scaler: 'bilinear' } },
  ])('rejects an invalid or incomplete output configuration', async (config) => {
    await expect(renderSuiteCut({ ...baseRequest, config } as never)).rejects.toBeInstanceOf(
      z.ZodError,
    )
  })
})
