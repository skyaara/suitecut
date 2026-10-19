#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Verifies a selectable emitted SDK against real browser executables. */
/* global document, window, innerWidth, innerHeight */
// Real-browser verification for the shared browser-control entry point. No videos.
// Example: node --expose-gc scripts/verify-browser-protocol.mjs \
//   --cdp-executable /usr/bin/chromium --bidi-executable /path/to/firefox
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve, dirname } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

const values = new Map()
for (let i = 2; i < process.argv.length; i += 2) {
  const key = process.argv[i]
  const value = process.argv[i + 1]
  if (!key?.startsWith('--') || value === undefined)
    throw new Error(`Expected --option value; got ${key}`)
  values.set(key, value)
}
const sdkPath = resolve(values.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
const output = resolve(
  values.get('--output') ?? '.suitecut/browser-protocol/shared-sdk-results.json',
)
const sdk = await import(pathToFileURL(sdkPath).href)
assert.equal(typeof sdk.launchBrowser, 'function', 'Shared SDK must export launchBrowser')
const candidates = [
  { name: 'Chromium', backend: 'cdp', executablePath: values.get('--cdp-executable') },
  { name: 'Chrome', backend: 'cdp', executablePath: values.get('--chrome-executable') },
  { name: 'Firefox', backend: 'bidi', executablePath: values.get('--bidi-executable') },
].filter((candidate) => candidate.executablePath)
assert.ok(candidates.length, 'Provide --cdp-executable and/or --bidi-executable')

const fixtureHTML = `<!doctype html><html lang="en"><meta charset="utf-8"><title>SuiteCut protocol verification</title>
<style>body{font:16px sans-serif;margin:20px}label,button,select{display:block;margin:8px}#long{height:1800px}</style>
<label>Name<input id="name" autocomplete="off"></label>
<label>Quantity<input id="quantity" type="number"></label>
<label><input type="checkbox" id="consent">Consent</label>
<label for="color">Color</label><select id="color"><option value="red">Red</option><option value="green">Green</option></select>
<button id="increment" onclick="window.count=(window.count||0)+1;window.lastTrusted=event.isTrusted">Increment</button>
<button disabled id="delayed" onclick="window.readyClicked=true">Ready later</button>
<button onclick="window.keyboardActivated=true;window.keyboardTrusted=event.isTrusted">Keyboard activate</button>
<output data-testid="status">Waiting</output><div id="shadow"></div><div id="long">Long document</div>
<script>window.count=0;document.querySelector('#shadow').attachShadow({mode:'open'}).innerHTML='<button>Shadow action</button>';document.querySelector('#shadow').shadowRoot.querySelector('button').onclick=()=>window.shadowClicked=true;setTimeout(()=>document.querySelector('#delayed').disabled=false,150)</script></html>`
const server = createServer((request, response) => {
  if (request.url === '/redirect') {
    response.writeHead(302, { location: '/form' })
    response.end()
    return
  }
  response.writeHead(200, {
    'content-type': 'text/html; charset=utf-8',
    'cache-control': 'no-store',
  })
  response.end(fixtureHTML)
})
await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve))
const origin = `http://127.0.0.1:${server.address().port}`
const results = []
const quantiles = (samples) => {
  const sorted = [...samples].sort((a, b) => a - b)
  return {
    samples: sorted.length,
    p50Ms: sorted[Math.floor(sorted.length * 0.5)],
    p95Ms: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95))],
    maxMs: sorted.at(-1),
  }
}
const memory = () => {
  globalThis.gc?.()
  return process.memoryUsage()
}
async function timed(operation, count = 30) {
  for (let i = 0; i < 5; i++) await operation()
  const samples = []
  for (let i = 0; i < count; i++) {
    const start = performance.now()
    await operation()
    samples.push(performance.now() - start)
  }
  return quantiles(samples)
}
// The internal process helper only launches the fixture; all control and attachment
// assertions use the public SDK. Installed browsers remain owned by the fixture.
async function verifyAttachment(candidate) {
  const { launchProtocolProcess } = await import(
    pathToFileURL(resolve(dirname(sdkPath), 'browser-process.js')).href
  )
  const fixture = await launchProtocolProcess({
    backend: candidate.backend,
    executablePath: candidate.executablePath,
    args:
      candidate.backend === 'cdp' && process.getuid?.() === 0
        ? ['--no-sandbox', '--disable-dev-shm-usage']
        : [],
    timeoutMs: 20_000,
  })
  let attached, reconnected, terminal
  let fixtureExited = false
  void fixture.closed.then(() => {
    fixtureExited = true
  })
  try {
    attached = await sdk.connectBrowser({
      backend: candidate.backend,
      endpointURL: fixture.endpointURL,
    })
    const context = await attached.newContext()
    const page = await context.newPage()
    await page.goto(`${origin}/form`)
    await page.getByLabel('Name', { exact: true }).fill('attachment survived')
    await context.addCookies([
      { name: 'attachment-cookie', value: 'preserved', domain: '127.0.0.1', path: '/' },
    ])
    const contextId = context.id,
      pageId = page.id
    await attached.close()
    assert.equal(attached.isConnected(), false)
    assert.equal(attached.connectionStats().pending, 0)
    assert.equal(attached.connectionStats().eventListeners, 0)
    assert.equal(fixtureExited, false, 'Disconnect must not close the independently owned browser')
    const endpointURL =
      candidate.backend === 'cdp'
        ? `http://${new URL(fixture.endpointURL).host}`
        : fixture.endpointURL
    assert.equal(typeof endpointURL, 'string', 'Attachment must expose its reconnect endpoint')
    reconnected = await sdk.connectBrowser({ backend: candidate.backend, endpointURL })
    // Firefox assigns new browsing-context IDs after ending and recreating an automation session.
    const existingPage = (await reconnected.pages()).find((value) =>
      candidate.backend === 'cdp' ? value.id === pageId : value.contextId === contextId,
    )
    const existingContext = (await reconnected.contexts()).find((value) => value.id === contextId)
    assert.ok(existingPage, 'Reconnect preserves the existing page')
    assert.ok(existingContext, 'Reconnect preserves the existing isolated context')
    assert.equal(
      await existingPage.evaluate(() => document.querySelector('#name').value),
      'attachment survived',
    )
    assert.equal(
      (await existingContext.cookies()).find((cookie) => cookie.name === 'attachment-cookie')
        ?.value,
      'preserved',
    )
    await existingContext.close()
    await reconnected.close()
    assert.equal(fixtureExited, false)
    terminal = await sdk.connectBrowser({
      backend: candidate.backend,
      endpointURL: fixture.endpointURL,
      closeBrowserOnClose: true,
    })
    await terminal.close()
    await fixture.closed
    return {
      passed: true,
      optInBrowserShutdown: true,
      discovery:
        candidate.backend === 'cdp'
          ? 'WebSocket attachment and HTTP-origin reconnect'
          : 'BiDi WebSocket attachment and session reconnect',
      pageAndContextStatePreserved: true,
      pageIdentityPreserved: existingPage.id === pageId,
      cookiePreserved: true,
    }
  } finally {
    try {
      await Promise.allSettled([attached?.close(), reconnected?.close(), terminal?.close()])
    } finally {
      await fixture.close()
      await fixture.closed
    }
  }
}
try {
  for (const candidate of candidates) {
    const result = { ...candidate, sdk: sdkPath, passed: false, checks: [], memoryBefore: memory() }
    let browser
    const check = (name, operation) =>
      operation().then(() => {
        result.checks.push(name)
      })
    try {
      if (values.get('--attach-only') === 'true') {
        result.attachment = await verifyAttachment(candidate)
        result.passed = true
        continue
      }
      const launchStart = performance.now()
      browser = await sdk.launchBrowser({
        backend: candidate.backend,
        executablePath: candidate.executablePath,
        args:
          candidate.backend === 'cdp' && process.getuid?.() === 0
            ? ['--no-sandbox', '--disable-dev-shm-usage']
            : [],
        headless: true,
        viewport: { width: 913, height: 617 },
        timeoutMs: 20_000,
      })
      result.launchMs = performance.now() - launchStart
      result.version = browser.version()
      result.capabilities = browser.capabilities()
      assert.equal(result.capabilities.backend, candidate.backend)
      assert.equal(result.capabilities.nativeCapture, false)
      const first = await browser.newContext()
      const second = await browser.newContext()
      const page = await first.newPage()
      page.setDefaultTimeout(5_000)
      await check('navigation and serializable evaluation', async () => {
        await page.goto(`${origin}/redirect`, { waitUntil: 'load' })
        assert.equal(await page.title(), 'SuiteCut protocol verification')
        assert.deepEqual(await page.evaluate(() => ({ width: innerWidth, height: innerHeight })), {
          width: 913,
          height: 617,
        })
        assert.equal(await page.url(), `${origin}/form`)
        assert.deepEqual(
          await page.evaluate((arg) => ({ result: arg.a + arg.b, list: [null, true, 'é'] }), {
            a: 19,
            b: 23,
          }),
          { result: 42, list: [null, true, 'é'] },
        )
      })
      await check('labels, trusted native typing and keyboard', async () => {
        const input = page.getByLabel('Name', { exact: true })
        await input.fill('SDK é 🚀')
        assert.equal(await page.evaluate(() => document.querySelector('#name').value), 'SDK é 🚀')
        await input.press('End')
        await page.keyboard.insertText('!')
        assert.equal(await page.evaluate(() => document.querySelector('#name').value), 'SDK é 🚀!')
        await input.fill('')
        assert.equal(await page.evaluate(() => document.querySelector('#name').value), '')
        const quantity = page.getByLabel('Quantity', { exact: true })
        await quantity.fill('123')
        await quantity.fill('47')
        assert.equal(await page.evaluate(() => document.querySelector('#quantity').value), '47')
        await quantity.fill('')
        assert.equal(await page.evaluate(() => document.querySelector('#quantity').value), '')
        await page.getByRole('button', { name: 'Keyboard activate', exact: true }).press('Enter')
        assert.equal(await page.evaluate(() => window.keyboardActivated), true)
        assert.equal(await page.evaluate(() => window.keyboardTrusted), true)
      })
      await check('trusted pointer input and role locator', async () => {
        await page.getByRole('button', { name: 'Increment', exact: true }).click()
        assert.equal(await page.evaluate(() => window.count), 1)
        assert.equal(await page.evaluate(() => window.lastTrusted), true)
        assert.equal(await page.getByRole('button', { name: 'Increment', exact: true }).count(), 1)
        assert.equal(await page.locator('button').first().innerText(), 'Increment')
        assert.equal(await page.locator('button').last().innerText(), 'Shadow action')
      })
      await check('checkbox, select, readiness and shadow DOM', async () => {
        const consent = page.getByLabel('Consent', { exact: true })
        await consent.check()
        assert.equal(await consent.isChecked(), true)
        await consent.uncheck()
        assert.equal(await consent.isChecked(), false)
        await page.getByLabel('Color', { exact: true }).selectOption('green')
        assert.equal(await page.evaluate(() => document.querySelector('#color').value), 'green')
        await page.getByRole('button', { name: 'Ready later', exact: true }).click()
        assert.equal(await page.evaluate(() => window.readyClicked), true)
        await page.getByRole('button', { name: 'Shadow action', exact: true }).click()
        assert.equal(await page.evaluate(() => window.shadowClicked), true)
      })
      await check('dynamic viewport sizes and real PNG screenshots', async () => {
        for (const size of [
          { width: 384, height: 640 },
          { width: 1024, height: 768 },
          { width: 800, height: 600 },
        ]) {
          await page.setViewportSize(size)
          assert.deepEqual(
            await page.evaluate(() => ({ width: innerWidth, height: innerHeight })),
            size,
          )
        }
        const png = await page.screenshot({ type: 'png' })
        assert.equal(png.subarray(0, 8).toString('hex'), '89504e470d0a1a0a')
        assert.equal(png.readUInt32BE(16), 800)
        assert.equal(png.readUInt32BE(20), 600)
        const full = await page.screenshot({ type: 'png', fullPage: true })
        assert.ok(full.readUInt32BE(20) >= 1800)
        result.screenshots = {
          viewportBytes: png.length,
          fullPageBytes: full.length,
          fullPageHeight: full.readUInt32BE(20),
        }
      })
      await check('isolated browser-context cookies', async () => {
        await first.addCookies([
          { name: 'suitecut-isolation', value: 'first-only', domain: '127.0.0.1', path: '/' },
        ])
        assert.equal(
          (await first.cookies()).find((cookie) => cookie.name === 'suitecut-isolation')?.value,
          'first-only',
        )
        assert.equal(
          (await second.cookies()).find((cookie) => cookie.name === 'suitecut-isolation'),
          undefined,
        )
        const secondPage = await second.newPage()
        await secondPage.goto(`${origin}/form`)
        assert.equal(
          await secondPage.evaluate(() => document.cookie.includes('suitecut-isolation')),
          false,
        )
        await secondPage.close()
        assert.equal(secondPage.isClosed(), true)
      })
      result.evaluateLatency = await timed(
        async () => assert.equal(await page.evaluate(() => 6 * 7), 42),
        50,
      )
      const counter = page.getByRole('button', { name: 'Increment', exact: true })
      result.clickLatency = await timed(() => counter.click())
      assert.equal(await page.evaluate(() => window.count), 36)
      await check('bounded concurrent commands', async () => {
        const values = await Promise.all(
          Array.from({ length: 16 }, (_, i) => page.evaluate((x) => x * 2, i)),
        )
        assert.deepEqual(
          values,
          Array.from({ length: 16 }, (_, i) => i * 2),
        )
      })
      await check('page and context lifecycle churn', async () => {
        await page.close()
        assert.equal(page.isClosed(), true)
        await assert.rejects(() => page.evaluate(() => 1))
        await first.close()
        await second.close()
        const baselineContexts = (await browser.contexts()).length
        const baselinePages = (await browser.pages()).length
        result.lifecycleSamples = []
        for (let batch = 0; batch < 3; batch++) {
          for (let i = 0; i < 8; i++) {
            const context = await browser.newContext()
            const childPage = await context.newPage()
            await childPage.goto(`${origin}/form`)
            assert.equal(
              await childPage.evaluate(() => document.title),
              'SuiteCut protocol verification',
            )
            await context.close()
            assert.equal(childPage.isClosed(), true)
          }
          assert.equal((await browser.contexts()).length, baselineContexts)
          assert.equal((await browser.pages()).length, baselinePages)
          assert.equal(browser.connectionStats().pending, 0)
          result.lifecycleSamples.push(memory())
        }
      })
      result.connectionBeforeClose = browser.connectionStats()
      assert.equal(result.connectionBeforeClose.pending, 0)
      result.passed = true
    } catch (error) {
      result.error = error instanceof Error ? error.stack : String(error)
      process.exitCode = 1
    } finally {
      if (browser) {
        const closeStart = performance.now()
        try {
          await browser.close()
          await browser.closed
          await browser.close()
          assert.equal(browser.isConnected(), false)
          const stats = browser.connectionStats()
          assert.equal(stats.pending, 0)
          assert.equal(stats.eventListeners, 0)
          result.connectionAfterClose = stats
          result.cleanup = { passed: true, closeMs: performance.now() - closeStart }
        } catch (error) {
          result.cleanup = { passed: false, error: String(error) }
          result.passed = false
          process.exitCode = 1
        }
      }
      if (result.passed && !result.attachment) {
        try {
          result.attachment = await verifyAttachment(candidate)
        } catch (error) {
          result.attachment = {
            passed: false,
            error: error instanceof Error ? error.stack : String(error),
          }
          result.passed = false
          process.exitCode = 1
        }
      }
      result.memoryAfter = memory()
      result.sdkHeapDeltaBytes = result.memoryAfter.heapUsed - result.memoryBefore.heapUsed
      results.push(result)
      console.log(JSON.stringify(result))
    }
  }
} finally {
  await new Promise((resolve) => server.close(resolve))
  await mkdir(dirname(output), { recursive: true })
  await writeFile(
    output,
    JSON.stringify(
      {
        generatedAt: new Date().toISOString(),
        node: process.version,
        scope:
          'Shared SDK real-browser smoke, warm persistent-connection timings, bounded lifecycle churn; no recording. Latencies are local protocol/SDK measurements, not cross-machine browser rankings. Node heap samples exclude browser processes and are diagnostic, not leak proof.',
        results,
      },
      null,
      2,
    ),
  )
}
