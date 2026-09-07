import { defineConfig } from '@playwright/test'

const sitePort = Number(process.env.SUITECUT_SITE_PORT ?? 4322)
if (!Number.isInteger(sitePort) || sitePort < 1 || sitePort > 65_535) {
  throw new Error('SUITECUT_SITE_PORT must be an integer from 1 through 65535')
}
const siteBaseUrl = `http://127.0.0.1:${String(sitePort)}`
const recordSuiteCut = process.env.SUITECUT_SITE_RECORD === '1'
const native4k = process.env.SUITECUT_SITE_4K === '1'
const suiteCutManifestPath =
  process.env.SUITECUT_SITE_MANIFEST_PATH ?? '.suitecut/site-demo-run.json'
const viewport = native4k ? { width: 3840, height: 2160 } : { width: 1280, height: 720 }

export default defineConfig({
  forbidOnly: !!process.env.CI,
  testDir: './tests',
  outputDir: '../test-results/site',
  fullyParallel: false,
  projects: [{ name: 'chromium', use: { browserName: 'chromium' } }],
  reporter: recordSuiteCut
    ? [['line'], ['../dist/reporter.js', { outputFile: suiteCutManifestPath }]]
    : [['line']],
  use: {
    baseURL: siteBaseUrl,
    trace: 'off',
    video: 'off',
    viewport,
  },
  webServer: {
    command: `npm run site:build && node scripts/serve-site.mjs --root site/dist --host 127.0.0.1 --port ${String(sitePort)}`,
    cwd: '..',
    reuseExistingServer: false,
    timeout: 180_000,
    url: siteBaseUrl,
  },
})
