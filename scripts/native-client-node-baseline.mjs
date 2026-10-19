#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-argument -- Matches the public native C++ facade against a selectable emitted Node SDK. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

const options = new Map()
for (let index = 2; index < process.argv.length; index += 2)
  options.set(process.argv[index], process.argv[index + 1])
const sdkPath = resolve(options.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
const executablePath = resolve(
  options.get('--browser') ?? '.suitecut/engine-performance-fixes/runtime/suitecut-browser',
)
const fixturePath = resolve(options.get('--fixture') ?? 'native/client/tools/fixture.html')
const output = resolve(options.get('--output') ?? '.suitecut/native-client/node-results.json')
const iterations = Number(options.get('--iterations') ?? 30)
const cycles = Number(options.get('--cycles') ?? 50)
assert.ok(Number.isInteger(iterations) && iterations > 0 && iterations <= 10000)
assert.ok(Number.isInteger(cycles) && cycles >= 0 && cycles <= 10000)
const html = await readFile(fixturePath, 'utf8')
const url =
  'data:text/html;charset=utf-8,' +
  encodeURIComponent(html).replace(
    /[!'()*]/gu,
    (value) => '%' + value.charCodeAt(0).toString(16).toUpperCase(),
  )
const sdk = await import(pathToFileURL(sdkPath).href)
const report = {
  client: 'node-sdk',
  runtime: process.version,
  iterations,
  cycles,
  passed: false,
  checks: [],
  samples: [],
  timings: {},
  scope:
    'Same CEF binary, viewport and fixture as native C++ client. Application CPU/RSS excludes the browser process tree.',
  viewport: { width: 1280, height: 720 },
  framesPerSecond: 60,
}
let browser
const started = performance.now(),
  cpuBefore = process.cpuUsage()
function check(value, message) {
  assert.ok(value, message)
  report.checks.push(message)
}
async function memory(label) {
  globalThis.gc?.()
  const smaps = await readFile('/proc/self/smaps_rollup', 'utf8')
  const stat = await readFile('/proc/self/status', 'utf8')
  const sample = {
    label,
    elapsedMs: performance.now() - started,
    ...process.memoryUsage(),
    pssKiB: Number(/^Pss:\s+(\d+)/mu.exec(smaps)?.[1] ?? 0),
    rssKiB: Number(/^Rss:\s+(\d+)/mu.exec(smaps)?.[1] ?? 0),
    peakRssKiB: Number(/^VmHWM:\s+(\d+)/mu.exec(stat)?.[1] ?? 0),
    fds: (await readdir('/proc/self/fd')).length,
  }
  report.samples.push(sample)
  return sample
}
async function measure(name, count, operation) {
  const samples = []
  const cpu = process.cpuUsage()
  for (let index = 0; index < count; index++) {
    const start = performance.now()
    await operation(index)
    samples.push(performance.now() - start)
  }
  const used = process.cpuUsage(cpu)
  const sorted = [...samples].sort((a, b) => a - b)
  report.timings[name] = {
    count,
    samplesMs: samples,
    p50Ms: sorted[Math.floor((sorted.length - 1) * 0.5)],
    p95Ms: sorted[Math.floor((sorted.length - 1) * 0.95)],
    clientCPUMs: (used.user + used.system) / 1000,
  }
}
try {
  const launch = performance.now()
  browser = await sdk.launchBrowser({
    backend: 'cef',
    executablePath,
    headless: true,
    width: 1280,
    height: 720,
    framesPerSecond: 60,
    pixelFormat: 'bgra',
  })
  report.launchToReadyMs = performance.now() - launch
  report.version = browser.version()
  console.log(
    JSON.stringify({
      event: 'ready',
      client: report.client,
      launchToReadyMs: report.launchToReadyMs,
    }),
  )
  const page = (await browser.defaultContext().pages())[0]
  await page.goto(url)
  check((await page.evaluate('40+2')) === 42, 'small evaluation')
  await page.getByRole('textbox', { name: 'Name', exact: true }).fill('Native λ 漢字')
  check(
    (await page.locator('#name').inputValue()) === 'Native λ 漢字',
    'locator input value getter',
  )
  check(
    (await page.locator('#name').getAttribute('aria-label')) === 'Name',
    'locator attribute getter',
  )
  check(
    (await page.locator('#commit').innerText()) === 'Commit transaction',
    'locator inner text getter',
  )
  await page.locator('#name').press('End')
  await page.getByRole('button', { name: 'Commit transaction', exact: true }).click()
  check(
    (await page.locator('#status').textContent()) === 'Saved Native λ 漢字',
    'form value and click outcome',
  )
  check((await page.evaluate('window.lastTrusted')) === true, 'genuine trusted pointer event')
  await page.getByRole('checkbox', { name: 'Consent', exact: true }).check()
  await page.getByRole('checkbox', { name: 'Consent', exact: true }).uncheck()
  check(
    (await page.evaluate("document.querySelector('#consent').checked")) === false,
    'checkbox uncheck',
  )
  await page.getByRole('checkbox', { name: 'Consent', exact: true }).check()
  await page.getByRole('combobox', { name: 'Region', exact: true }).selectOption('east')
  check(
    (await page.evaluate(
      "document.querySelector('#consent').checked&&document.querySelector('#region').value==='east'",
    )) === true,
    'checkbox and native select',
  )
  await page.getByRole('button', { name: 'Shadow action', exact: true }).click()
  check((await page.evaluate('window.shadowTrusted')) === true, 'shadow-root role control')
  const snapshot = await page.ariaSnapshot({ mode: 'ai' })
  const ref = /button "Commit transaction"[^\n]*\[ref=([^\]]+)\]/u.exec(snapshot)?.[1]
  assert.ok(ref, 'Observation contains an engine-issued button ref')
  await page.getByRef(ref).click()
  check(
    (await page.evaluate('window.clicks')) === 2,
    'engine-issued observation ref drives a genuine click',
  )
  const image = await page.screenshot()
  check(
    image.length > 100 &&
      image.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])),
    'actual PNG screenshot bytes',
  )
  report.screenshotBytes = image.length
  await mkdir(dirname(output), { recursive: true })
  await writeFile(resolve(dirname(output), 'screenshot.png'), image)
  await page.goto('about:blank')
  await page.goto(url + '#shift')
  await page.getByRole('button', { name: 'Moving target', exact: true }).click()
  check(
    (await page.evaluate(
      'window.race.clicks===1&&window.race.traps===0&&window.race.downs===1&&window.race.trusted===true',
    )) === true,
    'hover-shift final target revalidation',
  )
  await page.goto('about:blank')
  await page.goto(url + '#overlay')
  page.setDefaultTimeout(300)
  await assert.rejects(page.getByRole('button', { name: 'Moving target', exact: true }).click())
  check(
    (await page.evaluate(
      'window.race.downs===0&&window.race.overlayDowns===0&&window.race.traps===0',
    )) === true,
    'overlay blocks stale pointer down',
  )
  page.setDefaultTimeout(3000)
  await page.goto('about:blank')
  await page.goto(url)
  for (let index = 0; index < 5; index++) await page.evaluate('40+2')
  await memory('warm')
  await measure('evaluateSmall', iterations, async () => {
    assert.equal(await page.evaluate('40+2'), 42)
  })
  await measure('evaluateLarge', Math.max(3, Math.floor(iterations / 5)), async () => {
    const value = await page.evaluate("'x'.repeat(262144)")
    assert.equal(value.length, 262144)
    assert.equal(/[^x]/u.test(value), false)
  })
  await measure('queryCount', iterations, async () => {
    assert.equal(await page.locator('#noise .entry').count(), 3000)
  })
  await measure('locatorClick', iterations, async () => {
    await page.getByRole('button', { name: 'Commit transaction', exact: true }).click()
  })
  check(
    (await page.evaluate('window.clicks')) === iterations,
    'measured click count matches exactly',
  )
  await measure('locatorFill', iterations, async (index) => {
    await page.getByRole('textbox', { name: 'Name', exact: true }).fill(`value-${index}`)
  })
  check(
    (await page.evaluate("document.querySelector('#name').value")) === `value-${iterations - 1}`,
    'measured fill outcome',
  )
  for (let index = 0; index < cycles; index++) {
    const context = await browser.newContext()
    let isolated
    try {
      isolated = await context.newPage()
      await isolated.goto(url)
      await isolated.getByRole('textbox', { name: 'Name', exact: true }).fill(`cycle-${index}`)
      assert.equal(
        await isolated.evaluate("document.querySelector('#name').value"),
        `cycle-${index}`,
      )
    } finally {
      await isolated?.close()
      await context.close()
    }
    if ((index + 1) % 10 === 0 || index + 1 === cycles) {
      await memory(`cycle-${index + 1}`)
      console.log(JSON.stringify({ event: 'batch', client: report.client, cycles: index + 1 }))
    }
  }
  check(
    (await page.evaluate("document.querySelector('#name').value")) === `value-${iterations - 1}`,
    'context lifecycle preserves main page state',
  )
  report.passed = true
} catch (error) {
  report.error = { message: error.message, stack: error.stack }
  process.exitCode = 1
} finally {
  const close = performance.now()
  try {
    await browser?.close()
  } catch (error) {
    report.closeError = error.message
    report.passed = false
    process.exitCode = 1
  }
  report.closeMs = performance.now() - close
  report.processExit = browser?.source?.processExit?.()
  await memory('closed')
  report.totalMs = performance.now() - started
  const cpu = process.cpuUsage(cpuBefore)
  report.clientCPUMs = (cpu.user + cpu.system) / 1000
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(report, null, 2) + '\n')
  console.log(
    JSON.stringify({ event: 'done', client: report.client, passed: report.passed, output }),
  )
}
