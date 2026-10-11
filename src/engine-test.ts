import { test as base, expect } from '@playwright/test'

import { launchEngine, type SuiteCutEngine, type SuiteCutEngineOptions } from './engine.js'

/** Runs ordinary Playwright tests against SuiteCut's embedded Chromium. */
export const test = base.extend<
  object,
  {
    suitecutEngine: SuiteCutEngine
    suitecutNativeOptions: SuiteCutEngineOptions
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
