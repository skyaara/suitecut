import { defineConfig } from '@playwright/test'

export default defineConfig({
  testDir: './tests',
  outputDir: '../test-results/site',
  fullyParallel: false,
  projects: [{ name: 'chromium', use: { browserName: 'chromium' } }],
  reporter: [['line'], ['../dist/reporter.js', { outputFile: '.suitecut/site-demo-run.json' }]],
  use: {
    baseURL: 'http://127.0.0.1:4322/suitecut/',
    trace: 'off',
    video: 'off',
    viewport: { width: 1280, height: 720 },
  },
  webServer: {
    command:
      'npm run dev --workspace suitecut-docs -- --host 127.0.0.1 --port 4322 && npm exec --workspace suitecut-docs -- astro dev logs --follow',
    cwd: '..',
    reuseExistingServer: true,
    timeout: 120_000,
    url: 'http://127.0.0.1:4322/suitecut/',
  },
})
