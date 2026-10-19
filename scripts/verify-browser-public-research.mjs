/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-argument -- Public SDK runtime smoke check with in-page observations. */
/* global document */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const output = resolve('.suitecut/real-world-review/public-research')
await mkdir(output, { recursive: true })
const { launchBrowser } = await import(
  pathToFileURL(resolve('packages/browser-control/dist/browser.js'))
)
const proxy = process.env.HTTPS_PROXY ?? process.env.HTTP_PROXY
const report = {
  started: new Date().toISOString(),
  scope:
    'Read-only public Wikipedia search and article research through native browser input. No account or external state modification.',
  backends: [],
}
for (const backend of ['cdp', 'cef']) {
  const record = { backend, passed: false }
  report.backends.push(record)
  let browser
  try {
    browser = await launchBrowser(
      backend === 'cef'
        ? {
            executablePath: resolve('.suitecut/memory-deep/runtime/suitecut-browser'),
            headless: true,
            width: 1280,
            height: 720,
          }
        : {
            backend,
            executablePath: resolve('.suitecut/agent-comparison/competitor/chrome-linux64/chrome'),
            headless: true,
            args: [
              '--no-sandbox',
              '--disable-dev-shm-usage',
              ...(proxy ? [`--proxy-server=${proxy}`] : []),
            ],
            timeoutMs: 15_000,
            viewport: { width: 1280, height: 720 },
          },
    )
    record.version = browser.version()
    const context = await browser.newContext(
      backend === 'cef' && proxy ? { proxy: { server: proxy } } : {},
    )
    const page = await context.newPage()
    page.setDefaultTimeout(5000)
    await page.goto('https://en.wikipedia.org/wiki/Main_Page', { waitUntil: 'domcontentloaded' })
    const search = page.locator('input[name="search"]').first()
    await search.fill('Media Source Extensions')
    await search.press('Enter')
    const deadline = Date.now() + 15_000
    let state
    while (Date.now() < deadline) {
      try {
        state = await page.evaluate(() => ({
          title: document.title,
          heading: document.querySelector('#firstHeading')?.textContent,
          intro: document.querySelector('.mw-parser-output p:not(.mw-empty-elt)')?.textContent,
          sources: document.querySelectorAll('.reference').length,
        }))
        if (state.heading === 'Media Source Extensions') break
      } catch {
        /* Submission can briefly replace the document. */
      }
      await delay(200)
    }
    assert.equal(state?.heading, 'Media Source Extensions')
    assert.match(state?.intro ?? '', /JavaScript|stream|media|video/i)
    assert.ok(state.sources > 0, 'Article must include actual citation links')
    record.article = state
    record.passed = true
  } catch (error) {
    record.error = String(error)
  } finally {
    if (browser) {
      try {
        await browser.close()
        record.closed = true
      } catch (error) {
        record.closeError = String(error)
      }
      if (backend === 'cdp') record.afterClose = browser.connectionStats()
    }
    await writeFile(resolve(output, 'results.json'), JSON.stringify(report, null, 2) + '\n')
  }
  process.stdout.write(JSON.stringify(record) + '\n')
}
if (report.backends.some((record) => !record.passed)) process.exitCode = 1
