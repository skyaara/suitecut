import { defineConfig } from '@playwright/test'

import { type NativeBrowserOptions } from '../../src/native-browser.js'

export default defineConfig<object, { suitecutNativeOptions: NativeBrowserOptions }>({
  testDir: '.',
  testMatch: 'engine.e2e.spec.ts',
  workers: 1,
  timeout: 30000,
  outputDir: '2026-10-09/playwright-artifacts',
  reporter: [['list'], ['json', { outputFile: '2026-10-09/playwright-results.json' }]],
  use: {
    suitecutNativeOptions: {
      executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE ?? '',
      width: 640,
      height: 360,
      framesPerSecond: 30,
    },
    viewport: { width: 640, height: 360 },
  },
})
