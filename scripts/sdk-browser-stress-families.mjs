/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return -- Intentionally exercises a selectable emitted SDK artifact loaded by absolute path; its runtime surface is verified by assertions. */
import assert from 'node:assert/strict'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'
import { pathToFileURL } from 'node:url'

const sdk = resolve(process.env.SUITECUT_STRESS_SDK ?? '/workspace/suitecut/dist/browser.js')
const output = resolve(
  process.env.SUITECUT_STRESS_OUTPUT ??
    '/workspace/suitecut-hardening-live-proof-20261017/families',
)
await mkdir(output, { recursive: true })
const { launchBrowser } = await import(pathToFileURL(sdk))
const server = createServer((request, response) => {
  if (request.url.startsWith('/api')) {
    response.setHeader('Content-Type', 'application/json')
    response.end('{"value":42}')
    return
  }
  response.setHeader('Content-Type', 'text/html')
  response.end(
    '<!doctype html><title>Families</title><style>h1{color:red}</style><h1>Stress families</h1><input type="checkbox" id="check"><select id="select"><option value="a">A</option><option value="b">B</option></select><script>function fixtureFunction(x){return x+1}</script>',
  )
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const origin = `http://127.0.0.1:${server.address().port}`
const results = [],
  controller = new AbortController()
const timer = setTimeout(() => controller.abort(new Error('Family stress deadline')), 240000)
let browser
const family = async (name, work) => {
  const start = performance.now()
  try {
    const evidence = await work()
    results.push({ name, status: 'PASS', elapsedMs: performance.now() - start, evidence })
  } catch (error) {
    results.push({
      name,
      status: 'FAIL',
      elapsedMs: performance.now() - start,
      error: String(error),
      stack: error?.stack,
    })
  }
  process.stdout.write(JSON.stringify(results.at(-1)) + '\n')
}
try {
  browser = await launchBrowser({
    executablePath:
      process.env.SUITECUT_NATIVE_EXECUTABLE ??
      '/workspace/suitecut/.suitecut/native-build/suitecut-browser',
    headless: true,
    width: 640,
    height: 480,
    framesPerSecond: 1,
    signal: controller.signal,
  })
  const context = await browser.newContext({ baseURL: origin })
  const page = await context.newPage()
  page.setDefaultTimeout(5000)
  await page.goto('/')
  await family('forms-checkbox-select', async () => {
    await page.locator('#check').check()
    assert.equal(await page.locator('#check').isChecked(), true)
    await page.locator('#check').uncheck()
    assert.equal(await page.locator('#check').isChecked(), false)
    assert.deepEqual(await page.locator('#select').selectOption('b'), ['b'])
    return { checkedAndUnchecked: true, selected: 'b' }
  })
  await family('pdf', async () => {
    const pdf = await page.pdf()
    assert.equal(pdf.subarray(0, 4).toString(), '%PDF')
    return { bytes: pdf.length }
  })
  await family('js-current-document-coverage', async () => {
    await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
    assert.equal(await page.evaluate('fixtureFunction(41)'), 42)
    const entries = await page.coverage.stopJSCoverage()
    assert(
      entries.some((entry) =>
        entry.functions.some((fn) => fn.ranges.some((range) => range.count > 0)),
      ),
    )
    return { entries: entries.length }
  })
  await family('css-current-document-coverage', async () => {
    await page.coverage.startCSSCoverage()
    assert.equal(
      await page.evaluate(
        () => globalThis.getComputedStyle(globalThis.document.querySelector('h1')).color,
      ),
      'rgb(255, 0, 0)',
    )
    const entries = await page.coverage.stopCSSCoverage()
    assert(entries.length > 0)
    return { entries: entries.length }
  })
  await family('tracing', async () => {
    const path = resolve(output, 'trace.zip')
    await context.tracing.start({ screenshots: false, snapshots: true })
    await page.locator('h1').textContent()
    await context.tracing.stop({ path })
    const data = await readFile(path)
    assert(data.length > 100)
    return { bytes: data.length }
  })
  await family('har-record-replay', async () => {
    const path = resolve(output, 'record.har')
    const recording = await browser.newContext({
      baseURL: origin,
      recordHAR: { path, content: 'embed' },
    })
    try {
      const tab = await recording.newPage()
      await tab.goto('/')
      assert.equal(
        await tab.evaluate(() =>
          globalThis
            .fetch('/api')
            .then((r) => r.json())
            .then((j) => j.value),
        ),
        42,
      )
    } finally {
      await recording.close()
    }
    const har = JSON.parse(await readFile(path, 'utf8'))
    assert(har.log.entries.some((entry) => entry.request.url.endsWith('/api')))
    const replay = await browser.newContext({ baseURL: origin })
    try {
      await replay.routeFromHAR(path, { notFound: 'abort' })
      const tab = await replay.newPage()
      await tab.goto('/')
      assert.equal(
        await tab.evaluate(() =>
          globalThis
            .fetch('/api')
            .then((r) => r.json())
            .then((j) => j.value),
        ),
        42,
      )
    } finally {
      await replay.close()
    }
    return { entries: har.log.entries.length }
  })
  await family('renderer-crash-recovery', async () => {
    const crash = await context.newPage()
    await crash.goto('/')
    try {
      const event = crash.waitForEvent('crash', { timeout: 5000 })
      const command = crash.source.sendDevToolsCommand('Page.crash').catch((error) => String(error))
      assert.equal(await event, crash)
      await command
      assert.equal(crash.source.state, 'failed')
      const response = await crash.reload()
      assert.equal(response.status(), 200)
      assert.equal(await crash.title(), 'Families')
      return { actualCrash: true, recovered: true }
    } finally {
      await crash.close()
    }
  })
  await context.close()
} finally {
  if (browser) {
    await browser.close()
    await browser.closed
  }
  clearTimeout(timer)
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
}
const result = {
  status: results.some((result) => result.status === 'FAIL') ? 'FAIL' : 'PASS',
  results,
  shutdown: browser?.source.processExit?.(),
}
assert(result.shutdown, 'Expected actual native child exit receipt')
await writeFile(resolve(output, 'result.json'), JSON.stringify(result, null, 2) + '\n')
if (result.status === 'FAIL') process.exitCode = 1
