import { test as base, expect } from '@playwright/test'

import { launchEngine, type SuiteCutEngine } from './engine.js'
import { type NativeBrowserOptions } from './native-browser.js'

/** Runs ordinary Playwright tests against SuiteCut's embedded Chromium. */
export const test = base.extend<
  object,
  {
    suitecutEngine: SuiteCutEngine
    suitecutNativeOptions: NativeBrowserOptions
  }
>({
  suitecutNativeOptions: [
    { executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE ?? '' },
    { option: true, scope: 'worker' },
  ],
  suitecutEngine: [
    async ({ suitecutNativeOptions }, use) => {
      const engine = await launchEngine(suitecutNativeOptions)
      engine.source.onFailure?.((error) => {
        process.stderr.write(`CEF engine failure: ${error.message}\n`)
      })
      try {
        await use(engine)
      } finally {
        await engine.close()
      }
    },
    { scope: 'worker' },
  ],
  browser: [
    async ({ suitecutEngine }, use) => {
      await use(suitecutEngine.browser)
    },
    { scope: 'worker' },
  ],
})
export { expect }
