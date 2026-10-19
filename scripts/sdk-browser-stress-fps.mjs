/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access -- Intentionally exercises a selectable emitted SDK artifact loaded by absolute path; its runtime surface is verified by assertions. */
import assert from 'node:assert/strict'
import { once } from 'node:events'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

const output = resolve(
  process.env.SUITECUT_STRESS_OUTPUT ?? '/workspace/suitecut-hardening-live-proof-20261017/fps',
)
await mkdir(output, { recursive: true })
const { launchBrowser } = await import(
  pathToFileURL(process.env.SUITECUT_STRESS_SDK ?? '/workspace/suitecut/dist/browser.js')
)
const server = createServer((q, r) =>
  r.end(
    '<!doctype html><button onclick="this.dataset.count=String(+(this.dataset.count||0)+1)">Click</button>',
  ),
)
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const origin = `http://127.0.0.1:${server.address().port}`
const results = []
try {
  for (const fps of [5, 60, 120]) {
    let browser
    try {
      browser = await launchBrowser({
        executablePath:
          process.env.SUITECUT_NATIVE_EXECUTABLE ??
          '/workspace/suitecut/.suitecut/native-build/suitecut-browser',
        headless: true,
        width: 640,
        height: 480,
        framesPerSecond: fps,
      })
      const page = await browser.newPage({ baseURL: origin })
      page.setDefaultTimeout(5000)
      await page.goto('/')
      for (let i = 0; i < 3; i++) await page.locator('button').click()
      const samples = []
      for (let i = 0; i < 20; i++) {
        const started = performance.now()
        await page.locator('button').click()
        samples.push(performance.now() - started)
      }
      assert.equal(await page.locator('button').getAttribute('data-count'), '23')
      samples.sort((a, b) => a - b)
      const result = {
        fps,
        operations: 20,
        p50Ms: samples[9],
        p95Ms: samples[18],
        p99Ms: samples[19],
        samples,
      }
      results.push(result)
      await page.close()
      await browser.close()
      await browser.closed
      result.nativeExit = browser.source.processExit()
      process.stdout.write(JSON.stringify(result) + '\n')
    } finally {
      if (browser) await browser.close()
    }
  }
} finally {
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
}
await writeFile(
  resolve(output, 'result.json'),
  JSON.stringify(
    {
      status: 'PASS',
      results,
      scope:
        'One warmed page per separately launched native process; unchanged real RAF actionability checks; contended shared cloud host, not a standalone throughput benchmark.',
    },
    null,
    2,
  ) + '\n',
)
