import { defineConfig } from '@playwright/test'

export default defineConfig({
  forbidOnly: !!process.env.CI,
  testDir: './examples',
  outputDir: './test-results',
  projects: [
    { name: 'chromium', use: { browserName: 'chromium' } },
    { name: 'firefox', use: { browserName: 'firefox' } },
    { name: 'webkit', use: { browserName: 'webkit' } },
  ],
  reporter: [['line'], ['./dist/reporter.js', { outputFile: '.suitecut/latest-run.json' }]],
  use: {
    trace: 'off',
    video: 'off',
  },
})
