/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- The selectable emitted SDK is asserted against a real native runtime. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { readFileSync, readdirSync } from 'node:fs'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

// Fixed workload; no videos, external browser driver, or private fixture state.
// An outer proof-supervisor provides actual waitpid receipts for descendants.
const output = resolve(
  process.env.SUITECUT_STRESS_OUTPUT ?? '.suitecut/hard-browser-review/native/bounded',
)
const executable = resolve(
  process.env.SUITECUT_NATIVE_EXECUTABLE ?? '.suitecut/memory-deep/runtime/suitecut-browser',
)
const sdk = resolve(process.env.SUITECUT_STRESS_SDK ?? 'dist/browser.js')
const cycles = Number(process.env.SUITECUT_STRESS_CYCLES ?? 48)
const warmupCycles = Number(process.env.SUITECUT_STRESS_WARMUP_CYCLES ?? 16)
const durationMs = Number(process.env.SUITECUT_STRESS_DURATION_MS ?? 150000)
const framesPerSecond = Number(process.env.SUITECUT_STRESS_FPS ?? 60)
const idleDurationMs = Number(process.env.SUITECUT_STRESS_IDLE_MS ?? 0)
assert(Number.isInteger(cycles) && cycles >= 4 && cycles <= 128)
assert(Number.isInteger(warmupCycles) && warmupCycles >= 0 && warmupCycles <= 128)
assert(Number.isFinite(durationMs) && durationMs >= 1000 && durationMs <= 160000)
assert(Number.isFinite(framesPerSecond) && framesPerSecond > 0 && framesPerSecond <= 120)
assert(Number.isFinite(idleDurationMs) && idleDurationMs >= 0 && idleDurationMs <= 90000)
assert(globalThis.gc, 'Run with --expose-gc for post-GC evidence')
await mkdir(output, { recursive: true })
const digest = (bytes) => createHash('sha256').update(bytes).digest('hex')
const binarySHA256 = digest(await readFile(executable))
const sdkEntrySHA256 = digest(await readFile(sdk))
const { launchBrowser } = await import(pathToFileURL(sdk))
const payload = Buffer.alloc(1048576, 173)
for (let i = 0; i < payload.length; i += 4096) payload[i] = (i / 4096) % 256
const payloadSHA256 = digest(payload)
const dense = Array.from(
  { length: 768 },
  (_, i) =>
    `<section data-row="${i}"><div><span><b>Row ${i}</b><i>amount ${i * 7}</i></span><span class="value">${i % 19}</span></div></section>`,
).join('')
const pageHTML = `<!doctype html><meta charset="utf-8"><title>Native endurance</title><style>body{font:14px sans-serif}input,select,button,a{margin:4px}section{height:16px}</style><input id="text"><input id="check" type="checkbox"><select id="choice"><option value="a">Alpha</option><option value="b">Beta</option></select><button id="increment" onclick="this.dataset.count=String(+(this.dataset.count||0)+1)">Increment</button><button id="popup" onclick="window.open('/popup')">Popup</button><a id="download" href="/download">Download</a><iframe src="/frame"></iframe><main>${dense}</main>`
const requests = { pages: 0, frames: 0, popups: 0, downloads: 0, bytes: 0 }
const server = createServer((request, response) => {
  const url = new URL(request.url ?? '/', 'http://127.0.0.1')
  if (url.pathname === '/bytes' || url.pathname === '/download') {
    response.setHeader('Content-Type', 'application/octet-stream')
    response.setHeader('Content-Length', payload.length)
    if (url.pathname === '/download') {
      requests.downloads++
      response.setHeader('Content-Disposition', 'attachment; filename="endurance.bin"')
    } else requests.bytes++
    response.end(payload)
    return
  }
  response.setHeader('Content-Type', 'text/html')
  if (url.pathname === '/frame') {
    requests.frames++
    response.end(
      '<!doctype html><title>Child</title><input id="frame-input"><h2>Real child frame</h2>',
    )
    return
  }
  if (url.pathname === '/popup') {
    requests.popups++
    response.end('<!doctype html><title>Real popup</title><h1>Popup loaded</h1>')
    return
  }
  requests.pages++
  response.end(pageHTML)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const origin = `http://127.0.0.1:${server.address().port}`
const started = performance.now()
const latency = new Map(),
  samples = [],
  retained = [],
  tracked = new Map(),
  errors = []
let browser,
  stable,
  failure,
  completedCycles = 0,
  retainedClosedPublicObjects,
  heldNodeMemory,
  releasedNodeMemory
const controller = new AbortController()
const deadline = setTimeout(
  () => controller.abort(new Error('Native endurance exceeded 180 second runtime budget')),
  180000,
)
const record = async (name, operation) => {
  const begin = performance.now()
  try {
    const value = await operation()
    if (!latency.has(name)) latency.set(name, [])
    latency.get(name).push(performance.now() - begin)
    return value
  } catch (error) {
    errors.push({ name, message: String(error), stack: error?.stack })
    throw error
  }
}
const identities = () => {
  const all = readdirSync('/proc')
    .filter((entry) => /^\d+$/.test(entry))
    .flatMap((entry) => {
      try {
        const stat = readFileSync(`/proc/${entry}/stat`, 'utf8')
        const fields = stat.slice(stat.lastIndexOf(')') + 2).split(' ')
        return [
          { pid: Number(entry), parent: Number(fields[1]), start: fields[19], state: fields[0] },
        ]
      } catch {
        return []
      }
    })
  const owned = new Set([process.pid])
  for (let changing = true; changing;) {
    changing = false
    for (const item of all)
      if (owned.has(item.parent) && !owned.has(item.pid)) {
        owned.add(item.pid)
        changing = true
      }
  }
  return all.filter((item) => owned.has(item.pid) && item.pid !== process.pid)
}
const sample = async (cycle) => {
  await stable.requestGC()
  globalThis.gc()
  await delay(50)
  globalThis.gc()
  const children = []
  for (const item of identities()) {
    try {
      const rollup = readFileSync(`/proc/${item.pid}/smaps_rollup`, 'utf8')
      const command = readFileSync(`/proc/${item.pid}/cmdline`, 'utf8').replaceAll('\0', ' ')
      tracked.set(`${item.pid}:${item.start}`, { ...item, command })
      children.push({
        ...item,
        pssKiB: Number(/^Pss:\s+(\d+)/m.exec(rollup)?.[1] ?? 0),
        rssKiB: Number(/^Rss:\s+(\d+)/m.exec(rollup)?.[1] ?? 0),
      })
    } catch {
      /* The process physically retired during sampling. */
    }
  }
  const diagnostics = await stable.source.runAutomation('diagnostics')
  const rendererHeap = await stable.source.sendDevToolsCommand('Runtime.getHeapUsage')
  // Unknown-size /proc reads allocate temporary backing stores; collect those
  // after sampling so they are not mistaken for retained SDK buffers.
  await delay(0)
  globalThis.gc()
  const entry = {
    cycle,
    elapsedMs: performance.now() - started,
    node: process.memoryUsage(),
    children,
    nativePSSKiB: children.reduce((sum, item) => sum + item.pssKiB, 0),
    nativeRSSKiB: children.reduce((sum, item) => sum + item.rssKiB, 0),
    rendererHeap,
    diagnostics,
  }
  samples.push(entry)
  await writeFile(resolve(output, 'memory-samples.jsonl'), JSON.stringify(entry) + '\n', {
    flag: 'a',
  })
  assert(entry.nativePSSKiB < 1.5 * 1024 * 1024, 'Native process tree exceeded 1.5 GiB PSS budget')
  process.stdout.write(
    JSON.stringify({
      cycle,
      elapsedMs: entry.elapsedMs,
      nodeHeap: entry.node.heapUsed,
      nativePSSKiB: entry.nativePSSKiB,
      nativeAllocated: diagnostics.nativeGlibcAllocatedBytes,
      nativeFree: diagnostics.nativeGlibcFreeBytes,
      sourcesLive: diagnostics.nativeSourcesLive,
      sourceRegistry: diagnostics.nativeSourceRegistryEntries,
    }) + '\n',
  )
}
try {
  browser = await record('browser.launch', () =>
    launchBrowser({
      executablePath: executable,
      headless: true,
      width: 960,
      height: 720,
      framesPerSecond,
      signal: controller.signal,
      startupTimeoutMs: 30000,
      onDiagnostic: (message) =>
        writeFile(resolve(output, 'native.stderr.log'), message, { flag: 'a' }).catch(
          () => undefined,
        ),
    }),
  )
  const steady = await browser.newContext({ baseURL: origin })
  stable = await steady.newPage()
  stable.setDefaultTimeout(5000)
  await stable.goto('/')
  assert((await stable.evaluate(() => globalThis.document.querySelectorAll('*').length)) > 3500)
  await sample(0)
  if (idleDurationMs > 0) {
    const until = performance.now() + idleDurationMs
    while (performance.now() < until) {
      await delay(Math.max(0, Math.min(5000, until - performance.now())))
      await sample(0)
    }
  } else
    for (let cycle = 1; cycle <= cycles; cycle++) {
      // Leave room for GC samples and graceful shutdown before the hard abort.
      if (performance.now() - started >= durationMs) break
      const context = await record('context.new', () => browser.newContext({ baseURL: origin }))
      const page = await record('page.new', () => context.newPage())
      page.setDefaultTimeout(5000)
      await record('page.goto.dense', () => page.goto('/'))
      const value = `cycle-${cycle}-✓`
      await record('form.fill', () => page.locator('#text').fill(value))
      assert.equal(await record('form.read', () => page.locator('#text').inputValue()), value)
      await record('form.check', () => page.locator('#check').check())
      assert.equal(await page.locator('#check').isChecked(), true)
      assert.deepEqual(
        await record('form.select', () => page.locator('#choice').selectOption('b')),
        ['b'],
      )
      await record('form.click', () => page.locator('#increment').click())
      assert.equal(await page.locator('#increment').getAttribute('data-count'), '1')
      const frame = page.frames().find((item) => item !== page.mainFrame())
      assert(frame, 'Expected the actual child frame')
      await record('frame.fill', () => frame.locator('#frame-input').fill(value))
      assert.equal(await frame.locator('#frame-input').inputValue(), value)
      const handle = await record('handle.new', () =>
        page.evaluateHandle((v) => ({ v, array: Array.from({ length: 128 }, (_, i) => i) }), value),
      )
      assert.equal((await handle.jsonValue()).v, value)
      await record('handle.dispose', () => handle.dispose())
      const response = await record('http.1MiB', () => context.request.get('/bytes'))
      assert.equal(digest(await response.body()), payloadSHA256)
      await record('http.dispose', () => response.dispose())
      if (cycle % 4 === 0) {
        const pending = page.waitForEvent('popup', { timeout: 5000 })
        void pending.catch(() => undefined)
        await record('popup.click', () => page.locator('#popup').click())
        const popup = await pending
        await popup.waitForLoadState('load')
        assert.equal(await record('popup.title', () => popup.title()), 'Real popup')
        await record('popup.close', () => popup.close())
        retained.push(popup)
      }
      if (cycle % 6 === 0) {
        const pending = page.waitForEvent('download', { timeout: 5000 })
        void pending.catch(() => undefined)
        await record('download.click', () => page.locator('#download').click())
        const download = await pending
        const path = await record('download.path', () => download.path())
        assert(path)
        assert.equal(digest(await readFile(path)), payloadSHA256)
        assert.equal(download.suggestedFilename(), 'endurance.bin')
        await record('download.delete', () => download.delete())
        retained.push(download)
      }
      const closedWait = page.waitForEvent('console', { timeout: 10000 }).then(
        () => {
          throw new Error('Unexpected console event')
        },
        (error) => String(error),
      )
      await record('context.close', () => context.close())
      assert.match(await closedWait, /closed|abort/i)
      assert.equal(context.pages().length, 0)
      assert.equal(browser.contexts().length, 2)
      retained.push(context, page, frame, handle, response)
      completedCycles = cycle
      if (cycle % 4 === 0 || cycle === cycles) await sample(cycle)
    }
  // Retaining the public closed objects must not retain live native Sources.
  await sample(completedCycles + 1)
  assert.equal(
    samples.at(-1).diagnostics.nativeSourcesLive,
    samples[0].diagnostics.nativeSourcesLive,
  )
  assert.equal(
    samples.at(-1).diagnostics.nativeSourceRegistryEntries,
    samples[0].diagnostics.nativeSourceRegistryEntries,
  )
  assert.equal(samples.at(-1).diagnostics.nativeTasksReturnedHeld, 0)
  for (const key of [
    'activeDownloads',
    'retainedDownloadFiles',
    'downloadDiskBytes',
    'downloadReservedBytes',
    'downloadWaits',
    'downloadCopies',
  ])
    assert.equal(samples.at(-1).diagnostics[key], 0, `Native download ownership remained: ${key}`)
  for (const key of [
    'httpActive',
    'httpResponses',
    'httpRetainedBodyBytes',
    'httpRetainedBodyCapacity',
    'httpActiveBodyBytes',
    'httpUploads',
    'httpUploadBytes',
    'httpTransfers',
    'httpContexts',
  ])
    assert.equal(samples.at(-1).diagnostics[key], 0, `Native HTTP ownership remained: ${key}`)
  await steady.close()
} catch (error) {
  failure = error
  errors.push({ name: 'endurance', message: String(error), stack: error?.stack })
} finally {
  retainedClosedPublicObjects = retained.length
  globalThis.gc()
  heldNodeMemory = process.memoryUsage()
  retained.length = 0
  await delay(25)
  globalThis.gc()
  releasedNodeMemory = process.memoryUsage()
  if (browser) {
    try {
      await record('browser.close', () => browser.close())
      await browser.closed
    } catch (error) {
      failure ??= error
    }
  }
  clearTimeout(deadline)
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
}
const remaining = [...tracked.values()].flatMap((item) => {
  try {
    const stat = readFileSync(`/proc/${item.pid}/stat`, 'utf8')
    const fields = stat.slice(stat.lastIndexOf(')') + 2).split(' ')
    return fields[19] === item.start ? [{ ...item, actualState: fields[0] }] : []
  } catch {
    return []
  }
})
const running = remaining.filter((item) => item.actualState !== 'Z')
if (running.length) failure ??= new Error('Tracked native processes remained running after closure')
if (browser && !browser.source.processExit?.())
  failure ??= new Error('Missing actual native process exit receipt')
if (remaining.length && process.env.SUITECUT_STRESS_SUPERVISED !== '1')
  failure ??= new Error('Outer supervisor required to reap descendant zombies')
const quantile = (values, q) => values[Math.floor((values.length - 1) * q)]
const summary = Object.fromEntries(
  [...latency].map(([name, values]) => {
    values.sort((a, b) => a - b)
    return [
      name,
      {
        count: values.length,
        p50Ms: quantile(values, 0.5),
        p95Ms: quantile(values, 0.95),
        p99Ms: quantile(values, 0.99),
        maxMs: values.at(-1),
      },
    ]
  }),
)
const measured = samples.filter(
  (item) => item.cycle >= warmupCycles && item.cycle <= completedCycles,
)
const regression = (read) => {
  const n = measured.length,
    sx = measured.reduce((sum, item) => sum + item.cycle, 0),
    sy = measured.reduce((sum, item) => sum + read(item), 0)
  return n < 2
    ? null
    : (n * measured.reduce((sum, item) => sum + item.cycle * read(item), 0) - sx * sy) /
        (n * measured.reduce((sum, item) => sum + item.cycle ** 2, 0) - sx ** 2)
}
const result = {
  status: failure
    ? 'FAIL'
    : idleDurationMs > 0
      ? 'PASS_IDLE_CONTROL'
      : completedCycles < cycles
        ? 'PASS_BOUNDED'
        : 'PASS',
  scope:
    'Fixed native CEF heavy control/endurance workload; no claim of official agent-benchmark ranking or leak freedom.',
  mode: idleDurationMs > 0 ? 'idle-memory-control' : 'context-churn',
  cycles: idleDurationMs > 0 ? 0 : cycles,
  completedCycles,
  durationMs,
  idleDurationMs,
  elapsedMs: performance.now() - started,
  input: {
    executable,
    binarySHA256,
    sdk,
    sdkEntrySHA256,
    payloadBytes: payload.length,
    payloadSHA256,
    denseElementLowerBound: 3500,
    framesPerSecond,
    videos: false,
  },
  requests,
  retainedClosedPublicObjects,
  latency: summary,
  memory: {
    postWarmupStartCycle: warmupCycles,
    heldNodeMemory,
    releasedNodeMemory,
    nodeHeapBytesPerCycle: regression((item) => item.node.heapUsed),
    nodeExternalBytesPerCycle: regression((item) => item.node.external),
    nativePSSKiBPerCycle: regression((item) => item.nativePSSKiB),
    nativeAllocatedBytesPerCycle: regression((item) => item.diagnostics.nativeGlibcAllocatedBytes),
    nativeFreeArenaBytesPerCycle: regression((item) => item.diagnostics.nativeGlibcFreeBytes),
    rendererHeapBytesPerCycle: regression((item) => item.rendererHeap.usedSize),
    samples,
  },
  shutdown: {
    nativeExit: browser?.source.processExit?.(),
    trackedIdentities: [...tracked.values()],
    survivors: running,
    awaitingSupervisorReap: remaining.filter((item) => item.actualState === 'Z'),
  },
  errors,
  failure: failure
    ? failure instanceof Error
      ? failure.message
      : 'Unexpected non-Error failure'
    : undefined,
}
await writeFile(resolve(output, 'result.json'), JSON.stringify(result, null, 2) + '\n')
process.stdout.write(
  JSON.stringify({
    status: result.status,
    completedCycles,
    elapsedMs: result.elapsedMs,
    requests,
    memory: Object.fromEntries(Object.entries(result.memory).filter(([key]) => key !== 'samples')),
    failure: result.failure,
  }) + '\n',
)
if (failure) process.exitCode = 1
