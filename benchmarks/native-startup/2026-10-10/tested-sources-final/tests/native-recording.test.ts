import { describe, expect, it } from 'vitest'
import * as z from 'zod'

import { type NativeBrowser } from '../src/native-browser.js'
import { recordNative } from '../src/native-recording.js'

describe('native recording options', () => {
  it('requires a boolean page-audio flag', async () => {
    await expect(
      recordNative('invalid audio option', () => undefined, {
        source: {} as NativeBrowser,
        capture: { audio: 'tab' } as never,
      }),
    ).rejects.toBeInstanceOf(z.ZodError)
  })
})
