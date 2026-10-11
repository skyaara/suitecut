import { resolve } from 'node:path'
import { defineConfig } from '@playwright/test'

export default defineConfig({
  testDir: '.',
  testMatch: 'compatibility.spec.ts',
  workers: 1,
  retries: 0,
  timeout: 20000,
  expect: { timeout: 5000 },
  outputDir: resolve(process.env.COMPAT_DIRECTORY!, 'artifacts'),
  reporter: [['list'], ['json', { outputFile: resolve(process.env.COMPAT_DIRECTORY!, 'results.json') }]],
  use: { viewport: { width: 640, height: 360 }, actionTimeout: 5000 },
})
