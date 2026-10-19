import { describe, expect, it } from 'vitest'
import * as z from 'zod'

import { type NativeBrowser } from '../src/native-browser.js'
import { recordNative } from '../src/native-recording.js'

describe('native recording options', () => {
  it.each([0, 0.5, 121, Number.NaN, Number.POSITIVE_INFINITY])(
    'rejects invalid resampling FPS %s',
    async (framesPerSecond) => {
      await expect(
        recordNative('invalid FPS', () => undefined, {
          source: {} as NativeBrowser,
          capture: { framesPerSecond },
        }),
      ).rejects.toBeInstanceOf(z.ZodError)
    },
  )
  it.each([29.97, 59.94, 119.5])(
    'accepts fractional output FPS %s before requiring a live source',
    async (framesPerSecond) => {
      await expect(
        recordNative('fractional output', () => undefined, {
          source: {} as NativeBrowser,
          capture: { framesPerSecond },
        }),
      ).rejects.toThrow('Native source is not running')
    },
  )
  it('requires a boolean page-audio flag', async () => {
    await expect(
      recordNative('invalid audio option', () => undefined, {
        source: {} as NativeBrowser,
        capture: { audio: 'tab' } as never,
      }),
    ).rejects.toBeInstanceOf(z.ZodError)
  })
})
