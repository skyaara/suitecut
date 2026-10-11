import { resolve } from 'node:path'

import { defineConfig } from '@playwright/test'

const directory = process.env.COMPAT_DIRECTORY
if (!directory) throw new Error('Set COMPAT_DIRECTORY to an evidence directory')

export default defineConfig({
  testDir: '.',
  testMatch: 'compatibility.spec.ts',
  testIgnore: ['**/2026-*/**'],
  workers: 1,
  retries: 0,
  timeout: 20000,
  expect: { timeout: 5000 },
  outputDir: resolve(directory, 'artifacts'),
  reporter: [['list'], ['json', { outputFile: resolve(directory, 'results.json') }]],
  use: { viewport: { width: 640, height: 360 }, actionTimeout: 5000 },
})
