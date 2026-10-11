import { resolve } from 'node:path'

import { defineConfig } from '@playwright/test'

import { type NativeBrowserOptions } from '../../src/native-browser.js'

export default defineConfig<object, { suitecutNativeOptions: NativeBrowserOptions }>({
  testDir: '.',
  testMatch: 'engine.e2e.spec.ts',
  testIgnore: ['**/tested-sources*/**'],
  workers: 1,
  timeout: 30000,
  outputDir: process.env.SUITECUT_ENGINE_E2E_DIRECTORY
    ? resolve(process.env.SUITECUT_ENGINE_E2E_DIRECTORY, 'playwright-artifacts')
    : '2026-10-09/playwright-artifacts',
  reporter: [
    ['list'],
    [
      'json',
      {
        outputFile: process.env.SUITECUT_ENGINE_E2E_DIRECTORY
          ? resolve(process.env.SUITECUT_ENGINE_E2E_DIRECTORY, 'playwright-results.json')
          : '2026-10-09/playwright-results.json',
      },
    ],
  ],
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
