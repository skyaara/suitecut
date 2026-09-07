import { describe, expect, it } from 'vitest'
import * as z from 'zod'

import { renderSuiteCut, type SuiteCutRenderRequest } from '../src/render.js'

const baseRequest = {
  manifestPath: '/suitecut-tests/missing-manifest.json',
  outputPath: '/suitecut-tests/output.mp4',
} as const

describe('render request schema validation', () => {
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

    await expect(renderSuiteCut(request)).rejects.toMatchObject({ code: 'ENOENT' })
  })

  it('accepts explicit containers, encoders, pixel formats, and color ranges', async () => {
    const request = {
      ...baseRequest,
      config: {
        output: {
          container: 'mov',
          videoCodec: 'prores_ks',
          audioCodec: 'pcm_s24le',
          pixelFormat: 'yuv422p10le',
          colorRange: 'limited',
        },
      },
    } satisfies SuiteCutRenderRequest

    await expect(renderSuiteCut(request)).rejects.toMatchObject({ code: 'ENOENT' })
  })

  it('rejects an aborted render before reading its manifest', async () => {
    const controller = new AbortController()
    controller.abort()

    await expect(
      renderSuiteCut({ ...baseRequest, signal: controller.signal }),
    ).rejects.toMatchObject({ name: 'AbortError' })
  })

  it.each([
    { output: { width: 3840 } },
    { output: { width: 3839, height: 2160 } },
    { output: { width: 3840, height: 2159 } },
    { output: { width: 3840, height: 2160, quality: 18 } },
    { output: { width: 3840, height: 2160, scaler: 'bilinear' } },
    { output: { container: 'mov', format: 'mp4' } },
    { output: { videoCodec: '-vn' } },
    { output: { audioCodec: 'libopus -y' } },
    { output: { colorRange: 'broadcast' } },
    { signal: 'not-an-abort-signal' },
  ])('rejects an invalid or incomplete output configuration', async (config) => {
    const request = 'signal' in config ? { ...baseRequest, ...config } : { ...baseRequest, config }
    await expect(renderSuiteCut(request as never)).rejects.toBeInstanceOf(z.ZodError)
  })
})
