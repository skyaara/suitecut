// SPDX-License-Identifier: MIT
// One browser process tree per scenario. The Python supervisor owns hard deadlines.
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { appendFileSync } from 'node:fs'
import { mkdir, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'
import { createFixture } from '../fixtures/app.mjs'
import { scenarios } from '../scenarios/index.mjs'

if (process.argv[2] === '--list') {
  process.stdout.write(
    JSON.stringify(
      scenarios.map(({ id, tags, timeout, requires = [] }) => ({ id, tags, timeout, requires })),
    ),
  )
} else {
  const options = Object.fromEntries(
    Array.from({ length: (process.argv.length - 2) / 2 }, (_, i) => [
      process.argv[2 + i * 2],
      process.argv[3 + i * 2],
    ]),
  )
  const output = resolve(options['--output']),
    seed = Number(options['--seed'])
  assert(Number.isSafeInteger(seed) && seed > 0)
  const scenario = scenarios.find((t) => t.id === options['--case'])
  assert(scenario, 'Unknown scenario')
  await mkdir(output, { recursive: true })
  const report = {
    id: scenario.id,
    seed,
    passed: false,
    started: new Date().toISOString(),
    checks: [],
    observations: [],
    errors: [],
    cleanupErrors: [],
    unhandled: [],
  }
  const save = () => writeFile(join(output, 'result.json'), JSON.stringify(report, null, 2) + '\n')
  const event = (kind, data) =>
    appendFileSync(
      join(output, 'events.jsonl'),
      JSON.stringify({ ms: performance.now(), kind, data }) + '\n',
    )
  process.on('unhandledRejection', (error) => {
    report.unhandled.push(String(error))
    event('unhandled-rejection', String(error))
  })
  const sdk = await import(pathToFileURL(resolve(options['--sdk'], 'native.js')).href)
  const fixture = await createFixture(),
    cleanups = [],
    contexts = [],
    pages = []
  let source,
    root,
    stderr = '',
    started = performance.now()
  const h = {
    seed,
    output,
    fixture,
    source,
    root,
    ffmpeg: options['--ffmpeg'],
    ffprobe: options['--ffprobe'],
    check(message) {
      report.checks.push(message)
      event('check', message)
    },
    observe(label, value) {
      report.observations.push({ label, value })
      event('observation', { label, value })
    },
    onCleanup(fn) {
      cleanups.push(fn)
    },
    async until(predicate, message, timeout = 8000) {
      const deadline = performance.now() + timeout
      for (;;) {
        if (await predicate()) return
        assert(performance.now() < deadline, message)
        await delay(20)
      }
    },
    async page(options = {}) {
      const context = await h.browser.newContext(options)
      contexts.push(context)
      const page = await context.newPage()
      page.setDefaultTimeout(8000)
      pages.push(page)
      event('page-created', { context: context.id, page: page.source.pageId })
      return { page, context }
    },
  }
  try {
    source = await sdk.launchNativeBrowser({
      executablePath: resolve(options['--engine']),
      headless: true,
      width: 960,
      height: 640,
      framesPerSecond: 60,
      pixelFormat: 'i420',
      frameTransport: 'on-demand',
      startupTimeoutMs: 30000,
      downloadsPath: join(output, 'downloads'),
      onDiagnostic: (message) => {
        stderr += message
        appendFileSync(join(output, 'native-stderr.log'), message)
        assert(stderr.length <= 2 * 1024 * 1024, 'Native stderr exceeded bound')
      },
    })
    h.source = source
    h.browser = new sdk.NativeBrowserContexts(source)
    root = h.browser.defaultContext().pages()[0]
    h.root = root
    report.cefVersion = source.cefVersion
    report.capabilities = source.backendCapabilities?.()
    assert.equal(report.capabilities?.backend, 'cef', 'Suite must exercise the actual CEF backend')
    await scenario.run(h)
    assert(report.checks.length > 0, 'Scenario did not assert an observable outcome')
  } catch (error) {
    report.errors.push({ message: String(error), stack: error?.stack })
    event('failure', String(error))
  } finally {
    // Capture first-failure state before releasing gates or closing owners.
    for (const [index, page] of pages
      .filter((p) => !p.isClosed())
      .slice(-3)
      .entries()) {
      if (report.errors.length)
        try {
          report.observations.push({
            label: 'failure-native-diagnostics',
            value: await page.source.runAutomation('diagnostics'),
          })
        } catch (error) {
          event('artifact-error', String(error))
        }
      try {
        await writeFile(join(output, `page-${index}.png`), await page.screenshot({ timeout: 1500 }))
      } catch (error) {
        event('artifact-error', String(error))
      }
      if (report.errors.length)
        try {
          await writeFile(join(output, `page-${index}.html`), await page.content())
        } catch (error) {
          event('artifact-error', String(error))
        }
    }
    for (const fn of cleanups.reverse())
      try {
        await fn()
      } catch (error) {
        report.cleanupErrors.push(String(error))
      }
    try {
      await fixture.close()
    } catch (error) {
      report.cleanupErrors.push(String(error))
    }
    for (const context of contexts.reverse())
      try {
        await context.close()
      } catch (error) {
        report.cleanupErrors.push(String(error))
      }
    if (source) {
      try {
        const counters = await root.source.runAutomation('diagnostics')
        report.finalDiagnostics = counters
        for (const key of ['navigations', 'bindingCalls', 'bindingArgumentBytes', 'networkPaused'])
          if (key in counters)
            assert.equal(
              counters[key],
              0,
              `Native ${key} did not return to zero after context teardown`,
            )
      } catch (error) {
        report.cleanupErrors.push(String(error))
      }
      try {
        await source.close()
        await source.processClosed
        report.processExit = source.processExit?.()
        assert.deepEqual(report.processExit, { code: 0, signal: null })
      } catch (error) {
        report.cleanupErrors.push(String(error))
      }
    }
    report.nativeStderrBytes = Buffer.byteLength(stderr)
    if (stderr.length)
      report.cleanupErrors.push('Native stderr was not empty; see native-stderr.log')
    report.fixtureAudit = fixture.audit
    const fixtureErrors = fixture.audit.filter((row) => row.error)
    if (fixtureErrors.length)
      report.cleanupErrors.push('Local fixture emitted an error; see fixtureAudit')
    report.durationMs = performance.now() - started
    report.passed =
      !report.errors.length && !report.cleanupErrors.length && !report.unhandled.length
    report.finished = new Date().toISOString()
    await save()
    process.stdout.write(
      JSON.stringify({
        id: report.id,
        passed: report.passed,
        checks: report.checks.length,
        errors: report.errors,
        cleanupErrors: report.cleanupErrors,
      }) + '\n',
    )
    if (!report.passed) process.exitCode = 1
  }
}
