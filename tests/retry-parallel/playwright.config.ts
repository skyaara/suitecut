import { resolve } from 'node:path'

import { defineConfig } from '@playwright/test'

const projectRoot = resolve(import.meta.dirname, '../..')

export default defineConfig({
  forbidOnly: !!process.env.CI,
  testDir: import.meta.dirname,
  testMatch: 'retry-parallel.spec.ts',
  outputDir: resolve(projectRoot, 'test-results/retry-parallel'),
  fullyParallel: true,
  globalSetup: resolve(import.meta.dirname, 'global-setup.ts'),
  retries: 1,
  timeout: 120_000,
  workers: 2,
  projects: [
    {
      name: 'chromium-retry-parallel',
      use: { browserName: 'chromium', channel: 'chromium' },
    },
  ],
  reporter: [
    ['line'],
    [
      resolve(projectRoot, '.suitecut/retry-parallel-lib/reporter.js'),
      { outputFile: resolve(projectRoot, '.suitecut/retry-parallel.json') },
    ],
  ],
  use: {
    trace: 'off',
    video: 'off',
  },
})
