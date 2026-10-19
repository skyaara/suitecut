/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return -- Intentionally exercises a selectable emitted SDK artifact loaded by absolute path; its runtime surface is verified by assertions. */
import assert from 'node:assert/strict'
import { once } from 'node:events'
import { readFileSync, readdirSync } from 'node:fs'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'
import { pathToFileURL } from 'node:url'

const output = resolve(
  process.env.SUITECUT_STRESS_OUTPUT ??
    '/workspace/suitecut-hardening-live-proof-20261017/retention',
)
await mkdir(output, { recursive: true })
const { launchBrowser } = await import(
  pathToFileURL(process.env.SUITECUT_STRESS_SDK ?? '/workspace/suitecut/dist/browser.js')
)
const server = createServer((q, r) =>
  r.end(
    '<!doctype html><title>Retention</title><input id="text"><button id="click" onclick="this.dataset.value=String(+(this.dataset.value||0)+1)">Click</button>',
  ),
)
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const origin = `http://127.0.0.1:${server.address().port}`
const results = [],
  timer = new AbortController()
const deadline = setTimeout(() => timer.abort(new Error('Retention deadline')), 240000)
let browser
const processMemory = () =>
  readdirSync('/proc')
    .filter((s) => /^\d+$/.test(s))
    .flatMap((pid) => {
      try {
        const command = readFileSync(`/proc/${pid}/cmdline`, 'utf8')
        if (!command.includes('suitecut-browser') || !command.includes('--type=renderer')) return []
        return [
          {
            pid: Number(pid),
            pssKiB: Number(
              /^Pss:\s+(\d+)/m.exec(readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8'))?.[1] ?? 0,
            ),
          },
        ]
      } catch {
        return []
      }
    })
try {
  browser = await launchBrowser({
    executablePath:
      process.env.SUITECUT_NATIVE_EXECUTABLE ??
      '/workspace/suitecut/.suitecut/native-build/suitecut-browser',
    headless: true,
    width: 640,
    height: 480,
    framesPerSecond: 5,
    signal: timer.signal,
  })
  for (const phase of ['evaluate', 'handle', 'fill', 'click']) {
    const context = await browser.newContext({ baseURL: origin })
    const page = await context.newPage()
    await page.goto('/')
    page.setDefaultTimeout(5000)
    const measure = async (batch) => {
      await page.requestGC()
      globalThis.gc()
      return {
        batch,
        heap: await page.source.sendDevToolsCommand('Runtime.getHeapUsage'),
        renderers: processMemory(),
        diagnostics: await page.source.runAutomation('diagnostics'),
      }
    }
    const samples = [await measure(0)]
    let totalMs = 0
    for (let batch = 1; batch <= 3; batch++) {
      const started = performance.now()
      for (let i = 0; i < 64; i++) {
        if (phase === 'evaluate') assert.equal(await page.evaluate((x) => x + 1, i), i + 1)
        if (phase === 'handle') {
          const handle = await page.evaluateHandle((x) => ({ x }), i)
          assert.equal((await handle.jsonValue()).x, i)
          await handle.dispose()
        }
        if (phase === 'fill') await page.locator('#text').fill(String(i))
        if (phase === 'click') await page.locator('#click').click()
      }
      totalMs += performance.now() - started
      samples.push(await measure(batch))
      process.stdout.write(
        JSON.stringify({
          phase,
          batch,
          totalMs,
          heap: samples.at(-1).heap,
          renderers: samples.at(-1).renderers,
        }) + '\n',
      )
    }
    results.push({ phase, operations: 192, totalMs, samples })
    await context.close()
  }
} finally {
  if (browser) {
    await browser.close()
    await browser.closed
  }
  clearTimeout(deadline)
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
}
await writeFile(
  resolve(output, 'result.json'),
  JSON.stringify({ results, nativeExit: browser?.source.processExit?.() }, null, 2) + '\n',
)
