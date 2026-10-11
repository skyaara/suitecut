import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { mkdir, mkdtemp, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import { setTimeout as delay } from 'node:timers/promises'

import { chromium } from 'playwright'

import { connectBrowser } from '../../dist/index.js'
import { connectBrowser as engineConnect } from '../../dist/engine.js'
import { connectBrowser as playwrightConnect } from '../../dist/playwright.js'

const directory = resolve(process.env.SUITECUT_CONNECT_DIRECTORY ?? '.suitecut/external-browser')
await mkdir(directory, { recursive: true })
assert.equal(connectBrowser, engineConnect)
assert.equal(connectBrowser, playwrightConnect)
const results = []
const check = async (name, action) => {
  const started = performance.now()
  try {
    const details = await action()
    results.push({ name, passed: true, durationMs: performance.now() - started, details })
  } catch (error) {
    results.push({ name, passed: false, durationMs: performance.now() - started, error: String(error) })
    throw error
  }
}
const html = `<!doctype html><meta charset="utf-8"><title>External SuiteCut test</title>
<style>body{font:20px sans-serif;padding:24px}#space{height:2400px}</style>
<h1>External browser</h1><label>Name <input id="name"></label>
<button id="save" disabled>Save name</button><output id="result"></output>
<a href="/popup" target="_blank">Open tab</a>
<iframe title="Child frame" src="/frame"></iframe><div id="space"></div><p id="end">End of page</p>
<script>setTimeout(()=>document.querySelector('#save').disabled=false,500);
document.querySelector('#save').onclick=()=>document.querySelector('#result').textContent=document.querySelector('#name').value;</script>`
const server = createServer((req, res) => {
  res.setHeader('Content-Type', 'text/html; charset=utf-8')
  res.end(req.url === '/frame' ? '<button onclick="this.textContent=\'Frame clicked\'">Frame action</button>' :
    req.url === '/popup' ? '<h1>Popup page</h1>' : html)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const origin = `http://127.0.0.1:${server.address().port}`
let child
let browser
let browserServer
let chromeLog = ''
let failure
const memory = []

async function interactions(label, context, page) {
  page.setDefaultTimeout(10000)
  await check(`${label}: navigation`, async () => {
    await page.goto(origin)
    assert.equal(await page.title(), 'External SuiteCut test')
  })
  await check(`${label}: typing and auto-waited click`, async () => {
    await page.getByLabel('Name').fill('SuiteCut external')
    await page.getByLabel('Name').press('End')
    await page.getByRole('button', { name: 'Save name' }).click()
    assert.equal(await page.locator('#result').textContent(), 'SuiteCut external')
  })
  await check(`${label}: iframe`, async () => {
    const frame = page.frameLocator('iframe')
    await frame.getByRole('button', { name: 'Frame action' }).click()
    assert.equal(await frame.getByRole('button').textContent(), 'Frame clicked')
  })
  await check(`${label}: popup and tab lifecycle`, async () => {
    const popupPromise = page.waitForEvent('popup')
    await page.getByRole('link', { name: 'Open tab' }).click()
    const popup = await popupPromise
    await popup.waitForLoadState()
    assert.equal(await popup.locator('h1').textContent(), 'Popup page')
    await popup.close()
  })
  await check(`${label}: wheel and locator scroll`, async () => {
    await page.mouse.move(700, 400)
    await page.mouse.wheel(0, 800)
    await page.waitForFunction(() => window.scrollY > 100)
    await page.locator('#end').scrollIntoViewIfNeeded()
    assert(await page.locator('#end').isVisible())
    return { scrollY: await page.evaluate(() => window.scrollY) }
  })
  await check(`${label}: JavaScript, storage and screenshot`, async () => {
    assert.equal(await page.evaluate(() => 6 * 7), 42)
    await page.evaluate(() => localStorage.setItem('suitecut-external', 'retained'))
    await page.screenshot({ path: resolve(directory, `${label}.png`) })
  })
  await check(`${label}: network interception`, async () => {
    await context.route('**/api/test', (route) => route.fulfill({ json: { attached: true } }))
    const response = await page.evaluate(async () => (await fetch('/api/test')).json())
    assert.deepEqual(response, { attached: true })
    await context.unroute('**/api/test')
  })
}

try {
  await check('connection timeout: unresponsive HTTP discovery endpoint', async () => {
    const stalled = createServer(() => {})
    stalled.listen(0, '127.0.0.1')
    await once(stalled, 'listening')
    try {
      await assert.rejects(connectBrowser({
        endpoint: `http://127.0.0.1:${stalled.address().port}`,
        timeoutMs: 250,
      }), /Timeout/)
    } finally {
      stalled.closeAllConnections()
      await new Promise((done) => stalled.close(done))
    }
  })
  const profile = await mkdtemp(resolve(directory, 'chrome-profile-'))
  child = spawn(chromium.executablePath(), [
    '--headless=new', '--remote-debugging-address=127.0.0.1', '--remote-debugging-port=0',
    `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check', origin,
  ], { stdio: ['ignore', 'ignore', 'pipe'] })
  child.stderr.on('data', (data) => { chromeLog += data.toString() })
  child.on('error', (error) => { failure = error })
  let port
  const deadline = performance.now() + 30000
  while (performance.now() < deadline) {
    if (failure) throw failure
    if (child.exitCode !== null) throw new Error(`External Chromium exited: ${child.exitCode}`)
    try {
      port = (await readFile(resolve(profile, 'DevToolsActivePort'), 'utf8')).split('\n')[0]
      if (Number(port) > 0) break
    } catch { /* Chromium writes this file after initialization. */ }
    await delay(100)
  }
  assert(Number(port) > 0, 'External Chromium must expose its private CDP port')
  const endpoint = `http://127.0.0.1:${port}`
  browser = await connectBrowser({ endpoint, timeoutMs: 10000 })
  const version = browser.version()
  await check('cdp-http: existing host tab', async () => {
    const context = browser.contexts()[0]
    assert(context)
    const page = context.pages().find((page) => page.url().startsWith(origin))
    assert(page, 'Must attach to the tab opened by external Chromium')
    await interactions('cdp-http', context, page)
  })
  await check('cdp-http: isolated context', async () => {
    const context = await browser.newContext()
    const page = await context.newPage()
    await page.goto(origin)
    assert.equal(await page.evaluate(() => localStorage.getItem('suitecut-external')), null)
    await context.close()
  })
  await browser.close()
  browser = undefined
  await check('cdp: disconnect preserves host and existing tab state', async () => {
    assert.equal(child.exitCode, null)
    browser = await connectBrowser({ endpoint })
    const page = browser.contexts()[0].pages().find((page) => page.url().startsWith(origin))
    assert(page)
    assert.equal(await page.getByLabel('Name').inputValue(), 'SuiteCut external')
    assert.equal(await page.evaluate(() => localStorage.getItem('suitecut-external')), 'retained')
    await browser.close()
    browser = undefined
  })
  const discovery = await (await fetch(`${endpoint}/json/version`)).json()
  browser = await connectBrowser({ endpoint: discovery.webSocketDebuggerUrl })
  await interactions('cdp-ws', browser.contexts()[0], browser.contexts()[0].pages()[0])
  await browser.close()
  browser = undefined
  await check('cdp: 25 attach/disconnect cycles without accumulating tabs', async () => {
    for (let index = 0; index < 25; index++) {
      browser = await connectBrowser({ endpoint })
      assert.equal(browser.contexts()[0].pages().length, 1)
      await browser.contexts()[0].pages()[0].evaluate(() => 42)
      await browser.close()
      browser = undefined
      if (index % 5 === 0) {
        globalThis.gc?.()
        memory.push({ cycle: index + 1, ...process.memoryUsage() })
      }
    }
    return { cycles: 25, controllerMemory: memory, scope: 'Lifecycle smoke test, not proof of no memory leaks' }
  })
  browserServer = await chromium.launchServer({ chromiumSandbox: true, headless: true })
  browser = await connectBrowser({ protocol: 'playwright', endpoint: browserServer.wsEndpoint() })
  const context = await browser.newContext()
  const page = await context.newPage()
  await interactions('playwright-ws', context, page)
  await browser.close()
  browser = undefined
  await check('playwright: disconnect preserves server and reconnect works', async () => {
    assert.equal(browserServer.process().exitCode, null)
    browser = await connectBrowser({ protocol: 'playwright', endpoint: browserServer.wsEndpoint() })
    assert.equal(browser.contexts().length, 0)
    const context = await browser.newContext()
    const page = await context.newPage()
    await page.goto(origin)
    assert.equal(await page.title(), 'External SuiteCut test')
    await browser.close()
    browser = undefined
  })
  await writeFile(resolve(directory, 'browser-version.json'), JSON.stringify({ version, sandboxDisabled: false }, null, 2))
} catch (error) {
  failure = error
} finally {
  await browser?.close().catch(() => {})
  await browserServer?.close().catch(() => {})
  if (child?.exitCode === null) {
    const exited = once(child, 'exit')
    child.kill('SIGTERM')
    const killer = setTimeout(() => child.kill('SIGKILL'), 5000)
    await exited
    clearTimeout(killer)
  }
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
  await writeFile(resolve(directory, 'chromium.stderr.log'), chromeLog)
  await writeFile(resolve(directory, 'results.json'), JSON.stringify({
    savedAt: new Date().toISOString(), passed: !failure,
    scope: 'Ordinary external browser automation; no native A/V capture or RTC claims',
    results, failure: failure ? String(failure) : null,
  }, null, 2))
}
console.log(JSON.stringify({ passed: !failure, checks: results.length, directory }))
if (failure) throw failure
