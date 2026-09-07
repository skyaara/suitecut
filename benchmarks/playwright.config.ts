import { resolve } from 'node:path'

import { defineConfig } from '@playwright/test'

const manifestPath = process.env.SUITECUT_BENCHMARK_MANIFEST
if (manifestPath === undefined || manifestPath.trim() === '') {
  throw new Error('SUITECUT_BENCHMARK_MANIFEST is required')
}

export default defineConfig({
  forbidOnly: !!process.env.CI,
  testDir: '.',
  testMatch: 'public-sites.spec.ts',
  outputDir: resolve('.suitecut', 'benchmarks', 'playwright-results'),
  timeout: 180_000,
  workers: 1,
  projects: [{ name: 'chromium', use: { browserName: 'chromium' } }],
  reporter: [['line'], [resolve('dist/reporter.js'), { outputFile: manifestPath }]],
})
