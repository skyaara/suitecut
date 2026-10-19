/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Intentionally exercises a selectable emitted SDK artifact loaded by absolute path; its runtime surface is verified by assertions. */
import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { readFileSync, readdirSync } from 'node:fs'
import { mkdir, writeFile, readFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

// Real CEF integration stress. No external browser driver or test-runner entry.
// Run Node with --expose-gc --max-old-space-size=512. Artifacts stay outside the
// repository by default; override all input paths to compare identical workloads.
const sdkPath = resolve(process.env.SUITECUT_STRESS_SDK ?? '/workspace/suitecut/dist/browser.js')
const binary = resolve(
  process.env.SUITECUT_NATIVE_EXECUTABLE ??
    '/workspace/suitecut/.suitecut/native-build/suitecut-browser',
)
const output = resolve(
  process.env.SUITECUT_STRESS_OUTPUT ??
    '/workspace/suitecut-hardening-live-proof-20261017/baseline',
)
const batches = Number(process.env.SUITECUT_STRESS_BATCHES ?? 8)
const cycles = Number(process.env.SUITECUT_STRESS_CYCLES ?? 16)
const seed = Number(process.env.SUITECUT_STRESS_SEED ?? 123456789)
assert(Number.isInteger(batches) && batches > 0 && batches <= 100)
assert(Number.isInteger(cycles) && cycles > 0 && cycles <= 100)
assert(globalThis.gc, 'Use --expose-gc for comparable post-GC measurements')
await mkdir(output, { recursive: true })
const binarySHA256 = createHash('sha256')
  .update(await readFile(binary))
  .digest('hex')
const sdkEntrySHA256 = createHash('sha256')
  .update(await readFile(sdkPath))
  .digest('hex')
const { launchBrowser } = await import(pathToFileURL(sdkPath))
let randomState = seed >>> 0
const random = () => (randomState = (Math.imul(randomState, 1664525) + 1013904223) >>> 0) / 2 ** 32
const started = performance.now()
const latency = new Map(),
  errors = [],
  memory = [],
  tracked = new Map(),
  counts = {}
let browser,
  workloadPage,
  monitor,
  watching = false,
  monitorFailure,
  frames = 0,
  totalOperations = 0
const capabilitiesUnavailable = [],
  checks = []
const record = async (name, work) => {
  const start = performance.now()
  try {
    const value = await work()
    ;(latency.get(name) ?? latency.set(name, []).get(name)).push(performance.now() - start)
    counts[name] = (counts[name] ?? 0) + 1
    totalOperations++
    return value
  } catch (error) {
    errors.push({ name, message: String(error), stack: error?.stack })
    throw error
  }
}
const processes = () => {
  const list = []
  for (const entry of readdirSync('/proc')) {
    if (!/^\d+$/.test(entry)) continue
    try {
      const value = readFileSync(`/proc/${entry}/stat`, 'utf8')
      const fields = value.slice(value.lastIndexOf(')') + 2).split(' ')
      list.push({
        pid: Number(entry),
        parent: Number(fields[1]),
        startTime: fields[19],
        state: fields[0],
      })
    } catch {
      /* Process disappeared during sampling. */
    }
  }
  const descendants = new Set([process.pid])
  let changed = true
  while (changed) {
    changed = false
    for (const item of list)
      if (descendants.has(item.parent) && !descendants.has(item.pid)) {
        descendants.add(item.pid)
        changed = true
      }
  }
  return list.filter((item) => descendants.has(item.pid) && item.pid !== process.pid)
}
const nativeSample = () => {
  const alive = processes()
  let rssKiB = 0,
    pssKiB = 0
  const processList = []
  for (const item of alive) {
    try {
      const command = readFileSync(`/proc/${item.pid}/cmdline`, 'utf8').replaceAll('\0', ' ')
      if (!command.includes('suitecut-browser') && !tracked.has(item.pid)) continue
      tracked.set(item.pid, { ...item, command })
      const rollup = readFileSync(`/proc/${item.pid}/smaps_rollup`, 'utf8')
      const rss = Number(/^Rss:\s+(\d+)/m.exec(rollup)?.[1] ?? 0)
      const pss = Number(/^Pss:\s+(\d+)/m.exec(rollup)?.[1] ?? 0)
      rssKiB += rss
      pssKiB += pss
      processList.push({ ...item, rssKiB: rss, pssKiB: pss })
    } catch {
      /* Process disappeared during sampling. */
    }
  }
  const sample = {
    elapsedMs: performance.now() - started,
    ...process.memoryUsage(),
    nativeRSSKiB: rssKiB,
    nativePSSKiB: pssKiB,
    processes: processList,
  }
  memory.push(sample)
  // RSS counts shared libcef once per process; PSS is the process-tree budget.
  if (pssKiB > 1.5 * 1024 * 1024)
    throw new Error(`Native process-tree PSS budget exceeded: ${pssKiB} KiB`)
  return sample
}
const postGC = async (batch) => {
  globalThis.gc()
  await delay(100)
  globalThis.gc()
  const sample = nativeSample()
  sample.batch = batch
  sample.nativeDiagnostics = await workloadPage.source.runAutomation('diagnostics')
  sample.rendererHeap = await workloadPage.source.sendDevToolsCommand('Runtime.getHeapUsage')
  return sample
}
const json = (response, value) => {
  response.setHeader('Content-Type', 'application/json')
  response.end(JSON.stringify(value))
}
const server = createServer((request, response) => {
  const url = new URL(request.url, 'http://127.0.0.1')
  if (url.pathname === '/api')
    return json(response, {
      ok: true,
      query: url.searchParams.get('value'),
      cookie: request.headers.cookie ?? '',
    })
  if (url.pathname === '/slow') {
    setTimeout(() => {
      if (!response.destroyed) json(response, { slow: true })
    }, 150)
    return
  }
  if (url.pathname === '/worker.js') {
    response.setHeader('Content-Type', 'text/javascript')
    response.end('globalThis.answer=41;onmessage=e=>postMessage(e.data+1)')
    return
  }
  if (url.pathname === '/frame') {
    response.end('<!doctype html><h2>Frame child</h2><input id="child" value="frame">')
    return
  }
  response.setHeader('Content-Type', 'text/html')
  response.end(
    `<!doctype html><meta charset="utf-8"><title>SDK stress</title><style>body{font:16px sans-serif}button,input,select{margin:6px}</style><h1>Native SDK stress</h1><button id="click" onclick="this.dataset.count=String(+(this.dataset.count||0)+1)">Increment</button><input id="text"><input id="check" type="checkbox"><select id="select"><option value="a">A</option><option value="b">B</option></select><button id="popup" onclick="window.open('/popup','_blank')">Popup</button><iframe src="/frame"></iframe><script>window.events=[];document.addEventListener('click',e=>events.push(e.target.id));</script>`,
  )
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const origin = `http://127.0.0.1:${server.address().port}`
const controller = new AbortController()
const deadline = setTimeout(
  () => controller.abort(new Error('Stress run exceeded 9 minute deadline')),
  540000,
)
deadline.unref()
let failure
try {
  browser = await record('browser.launch', () =>
    launchBrowser({
      executablePath: binary,
      headless: true,
      width: 640,
      height: 480,
      framesPerSecond: 5,
      signal: controller.signal,
      startupTimeoutMs: 30000,
      onDiagnostic: (text) =>
        writeFile(resolve(output, 'native.stderr.log'), text, { flag: 'a' }).catch(() => undefined),
    }),
  )
  browser.source.onFrame(() => {
    frames++
  })
  for (const [name, value] of Object.entries(browser.capabilities() ?? {}))
    if (value === false) capabilitiesUnavailable.push(name)
  watching = true
  monitor = (async () => {
    while (watching) {
      try {
        nativeSample()
      } catch (error) {
        monitorFailure = error
        controller.abort(error)
        return
      }
      await delay(750)
    }
  })()
  const steady = await record('context.new.steady', () => browser.newContext({ baseURL: origin }))
  const page = await record('page.new.steady', () => steady.newPage())
  workloadPage = page
  page.setDefaultTimeout(3000)
  page.setDefaultNavigationTimeout(10000)
  await record('page.goto.warm', () => page.goto('/'))
  // One-time broad families: each records an actual assertion and cleanup.
  await record('context.initScript', () =>
    steady.addInitScript(() => {
      globalThis.seeded = 7
    }),
  )
  await record('page.reload', () => page.reload())
  assert.equal(await record('evaluate.initScript', () => page.evaluate(() => globalThis.seeded)), 7)
  await record('permissions.grant', () => steady.grantPermissions(['geolocation'], { origin }))
  await record('permissions.clear', () => steady.clearPermissions())
  await record('cookies.add', () =>
    steady.addCookies([{ name: 'stress', value: 'yes', url: origin }]),
  )
  assert.equal((await record('cookies.read', () => steady.cookies(origin)))[0].name, 'stress')
  await record('storage.write', () =>
    page.evaluate(() => globalThis.localStorage.setItem('stress', 'kept')),
  )
  const storage = await record('storage.capture', () => steady.storageState())
  assert(storage.origins.some((item) => item.localStorage.some((value) => value.name === 'stress')))
  await record('cookies.clear', () => steady.clearCookies())
  const frame = page.frames().find((frame) => frame !== page.mainFrame())
  assert(frame, 'Expected real child frame')
  assert.equal(
    await record('frame.evaluate', () =>
      frame.evaluate(() => globalThis.document.querySelector('h2').textContent),
    ),
    'Frame child',
  )
  let dialogSeen = false
  const dialogListener = (dialog) => {
    dialogSeen = true
    dialog.accept().catch((error) => errors.push({ name: 'dialog.accept', message: String(error) }))
  }
  page.on('dialog', dialogListener)
  await record('dialog.evaluate', () =>
    page.evaluate(() => {
      globalThis.alert('stress')
      return 1
    }),
  )
  assert(dialogSeen)
  page.off('dialog', dialogListener)
  const popupPromise = page.waitForEvent('popup', { timeout: 5000 })
  await record('popup.click', () => page.locator('#popup').click())
  const popup = await record('popup.event', () => popupPromise)
  await record('popup.evaluate', () => popup.evaluate(() => globalThis.document.title))
  await record('popup.close', () => popup.close())
  const workerPromise = page.waitForEvent('worker', { timeout: 5000 })
  await record('worker.create', () =>
    page.evaluate(() => {
      globalThis.stressWorker = new Worker('/worker.js')
    }),
  )
  const worker = await record('worker.event', () => workerPromise)
  assert.equal(
    await record('worker.evaluate', () => worker.evaluate(() => globalThis.answer + 1)),
    42,
  )
  await record('worker.terminate', () =>
    page.evaluate(() => {
      globalThis.stressWorker.terminate()
      delete globalThis.stressWorker
    }),
  )
  const route = (route) =>
    route.fulfill({
      status: 200,
      contentType: 'application/json',
      body: JSON.stringify({ routed: true }),
    })
  await record('route.add', () => page.route('**/routed', route))
  assert.deepEqual(
    await record('route.fetch', () =>
      page.evaluate(() => globalThis.fetch('/routed').then((response) => response.json())),
    ),
    { routed: true },
  )
  await record('route.remove', () => page.unroute('**/routed', route))
  const http = await record('http.get', () => steady.request.get('/api?value=42'))
  assert.equal((await record('http.json', () => http.json())).query, '42')
  await record('http.dispose', () => http.dispose())
  const png = await record('screenshot', () => page.screenshot())
  assert(png.length > 100)
  await record('media.emulate', () =>
    page.emulateMedia({ colorScheme: 'dark', reducedMotion: 'reduce' }),
  )
  assert.equal(
    await record('media.read', () =>
      page.evaluate(() => globalThis.matchMedia('(prefers-color-scheme: dark)').matches),
    ),
    true,
  )
  checks.push(
    'contexts/pages/popups/frames/locators/actions/evaluation/handles/events/dialogs/routes/HTTP/storage/cookies/permissions/workers/screenshot/media',
  )
  await postGC(0)
  for (let batch = 1; batch <= batches; batch++) {
    for (let cycle = 0; cycle < cycles; cycle++) {
      const value = `${batch}:${cycle}:${Math.floor(random() * 1e9)}`
      await record('locator.fill', () => page.locator('#text').fill(value))
      assert.equal(
        await record('locator.inputValue', () => page.locator('#text').inputValue()),
        value,
      )
      await record('locator.click', () => page.locator('#click').click())
      const handle = await record('handle.create', () =>
        page.evaluateHandle((value) => ({ value, array: [1, 2, 3] }), value),
      )
      assert.equal((await record('handle.json', () => handle.jsonValue())).value, value)
      await record('handle.dispose', () => handle.dispose())
      assert.equal(
        await record('evaluate.scalar', () => page.evaluate((value) => value.length, value)),
        value.length,
      )
      const response = await record('http.churn.get', () =>
        steady.request.get(`/api?value=${encodeURIComponent(value)}`),
      )
      assert.equal((await record('http.churn.json', () => response.json())).query, value)
      await record('http.churn.dispose', () => response.dispose())
      const context = await record('context.churn.new', () =>
        browser.newContext({ baseURL: origin }),
      )
      const child = await record('page.churn.new', () => context.newPage())
      await record('page.churn.goto', () => child.goto('/'))
      assert.equal(
        await record('page.churn.evaluate', () => child.evaluate(() => globalThis.document.title)),
        'SDK stress',
      )
      await record('context.churn.close', () => context.close())
      assert.equal(context.pages().length, 0)
      assert.equal(browser.contexts().length, 2, 'Closed contexts retained in controller inventory')
    }
    await record('renderer.gc', () => page.requestGC())
    const sample = await postGC(batch)
    process.stdout.write(
      JSON.stringify({
        batch,
        totalOperations,
        heapUsed: sample.heapUsed,
        nativePSSKiB: sample.nativePSSKiB,
      }) + '\n',
    )
    if (monitorFailure)
      throw monitorFailure instanceof Error ? monitorFailure : new Error('Stress monitor failed')
  }
  // Physical closure must cancel an outstanding event waiter without timeout.
  const dying = await record('page.cancel.new', () => steady.newPage())
  const pending = dying.waitForEvent('console', { timeout: 10000 }).then(
    () => {
      throw new Error('Unexpected console event')
    },
    (error) => String(error),
  )
  await record('page.cancel.close', () => dying.close())
  const canceled = await record('page.cancel.waiter', () => pending)
  assert(/closed|abort/i.test(canceled), canceled)
  await record('context.steady.close', () => steady.close())
} catch (error) {
  failure = error
  errors.push({ name: 'run', message: String(error), stack: error?.stack })
} finally {
  if (browser) {
    try {
      await record('browser.close', () => browser.close())
      await browser.closed
    } catch (error) {
      failure ??= error
    }
  }
  watching = false
  if (monitor) await monitor
  clearTimeout(deadline)
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
}
// Inspect identity (PID + kernel start time), not PID existence alone. The
// transport's actual child close/exit receipt is required separately below.
const remaining = () =>
  [...tracked.values()].flatMap((item) => {
    try {
      const stat = readFileSync(`/proc/${item.pid}/stat`, 'utf8')
      const fields = stat.slice(stat.lastIndexOf(')') + 2).split(' ')
      return fields[19] === item.startTime
        ? [{ ...item, actualState: fields[0], actualParent: Number(fields[1]) }]
        : []
    } catch {
      return []
    }
  })
const closeDeadline = performance.now() + 10000
while (remaining().some((item) => item.actualState !== 'Z') && performance.now() < closeDeadline)
  await delay(100)
const survivingProcesses = remaining()
const pendingZombieProcesses = survivingProcesses.filter((item) => item.actualState === 'Z')
if (survivingProcesses.some((item) => item.actualState !== 'Z'))
  failure ??= new Error('Tracked running browser descendants survived shutdown')
if (pendingZombieProcesses.length && process.env.SUITECUT_STRESS_SUPERVISED !== '1')
  failure ??= new Error('Tracked zombies require an outer subreaper and actual waitpid receipts')
if (browser && !browser.source.processExit?.())
  failure ??= new Error('No actual native process exit receipt')
const percentile = (sorted, value) =>
  sorted[Math.min(sorted.length - 1, Math.floor((sorted.length - 1) * value))]
const latencySummary = Object.fromEntries(
  [...latency].map(([name, values]) => {
    values.sort((a, b) => a - b)
    return [
      name,
      {
        count: values.length,
        p50Ms: percentile(values, 0.5),
        p95Ms: percentile(values, 0.95),
        p99Ms: percentile(values, 0.99),
        maxMs: values.at(-1),
      },
    ]
  }),
)
const gcSamples = memory.filter((item) => item.batch !== undefined)
const slope = (key) => {
  const samples = gcSamples.filter((item) => item.batch >= Math.min(2, batches))
  const x = samples.map((item) => item.batch),
    y = samples.map((item) => item[key])
  const n = x.length
  const sx = x.reduce((a, b) => a + b, 0),
    sy = y.reduce((a, b) => a + b, 0)
  return n < 2
    ? null
    : (n * x.reduce((sum, v, i) => sum + v * y[i], 0) - sx * sy) /
        (n * x.reduce((sum, v) => sum + v * v, 0) - sx * sx)
}
const result = {
  status: failure
    ? 'FAIL'
    : pendingZombieProcesses.length
      ? 'PASS_PENDING_SUPERVISOR_REAP'
      : 'PASS',
  seed,
  batches,
  cycles,
  framesPerSecond: 5,
  totalOperations,
  elapsedMs: performance.now() - started,
  operationsPerSecond: totalOperations / ((performance.now() - started) / 1000),
  binary,
  binarySHA256,
  sdkPath,
  sdkEntrySHA256,
  counts,
  latency: latencySummary,
  memory: {
    steadyPostGCSamples: gcSamples,
    heapBytesPerBatchSlope: slope('heapUsed'),
    nativePSSKiBPerBatchSlope: slope('nativePSSKiB'),
    peakNativePSSKiB: Math.max(0, ...memory.map((item) => item.nativePSSKiB)),
    peakNativeRSSKiB: Math.max(0, ...memory.map((item) => item.nativeRSSKiB)),
  },
  frames,
  checks,
  unavailableCapabilities: capabilitiesUnavailable,
  deferred: [
    'custom CEF capabilities',
    'native crash/recovery injection',
    'video/audio encoding stress',
    'HAR archive replay stress',
    'PDF',
    'service/shared worker lifecycle',
  ],
  shutdown: {
    nativeExit: browser?.source.processExit?.(),
    trackedProcesses: [...tracked.values()],
    survivingRunningProcesses: survivingProcesses.filter((item) => item.actualState !== 'Z'),
    pendingZombieProcesses,
    requiresSupervisorWaitpidReceipt: pendingZombieProcesses.length > 0,
  },
  errors,
  failure: failure
    ? failure instanceof Error
      ? failure.message
      : 'Unexpected non-Error failure'
    : undefined,
}
await writeFile(resolve(output, 'result.json'), JSON.stringify(result, null, 2) + '\n')
await writeFile(resolve(output, 'memory-samples.json'), JSON.stringify(memory, null, 2) + '\n')
process.stdout.write(
  JSON.stringify({
    status: result.status,
    totalOperations,
    elapsedMs: result.elapsedMs,
    memory: {
      heapBytesPerBatchSlope: result.memory.heapBytesPerBatchSlope,
      nativePSSKiBPerBatchSlope: result.memory.nativePSSKiBPerBatchSlope,
      peakNativePSSKiB: result.memory.peakNativePSSKiB,
    },
    failure: result.failure,
    shutdown: result.shutdown.nativeExit,
  }) + '\n',
)
if (failure) process.exitCode = 1
