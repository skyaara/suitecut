import { defineConfig } from '@playwright/test'

export default defineConfig({
  testDir: './examples',
  outputDir: './test-results',
  projects: [{ name: 'chromium', use: { browserName: 'chromium' } }],
  reporter: [
    ['line'],
    ['./dist/reporter.js', { outputFile: '.suitecut/latest-run.json' }],
  ],
  use: {
    trace: 'off',
    video: 'off',
  },
})
