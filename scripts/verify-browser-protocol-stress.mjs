#!/usr/bin/env node
// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Exercises a selectable emitted production SDK and records private diagnostic fields without importing source types. */
/* global document, window, setTimeout, clearTimeout, setInterval, clearInterval */
// Real Chrome stress proof using the production browser-control SDK. No videos.
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, readdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'
import { writeHeapSnapshot } from 'node:v8'

const options = new Map()
for (let index = 2; index < process.argv.length; index += 2) {
  assert.ok(
    process.argv[index]?.startsWith('--') && process.argv[index + 1],
    'Expected --option value',
  )
  options.set(process.argv[index], process.argv[index + 1])
}
const levels = {
  small: {
    batches: 3,
    cycles: 1,
    pages: 2,
    denseNodes: 20_000,
    labels: 300,
    shadows: 50,
    deadlineMs: 90_000,
  },
  medium: {
    batches: 6,
    cycles: 3,
    pages: 2,
    denseNodes: 20_000,
    labels: 500,
    shadows: 100,
    deadlineMs: 180_000,
  },
  heavy: {
    batches: 8,
    cycles: 5,
    pages: 3,
    denseNodes: 30_000,
    labels: 1000,
    shadows: 200,
    deadlineMs: 300_000,
  },
}
const level = options.get('--level') ?? 'medium'
const config = levels[level]
assert.ok(config, '--level must be small, medium or heavy')
assert.equal(typeof globalThis.gc, 'function', 'Run node --expose-gc for meaningful heap samples')
const executablePath = resolve(
  options.get('--executable') ?? '.suitecut/agent-comparison/competitor/chrome-linux64/chrome',
)
const sdkPath = resolve(options.get('--sdk') ?? 'packages/browser-control/dist/browser.js')
const output = resolve(options.get('--output') ?? `.suitecut/hard-browser-review/protocol/${level}`)
await mkdir(output, { recursive: true })
const sdk = await import(pathToFileURL(sdkPath).href)
const payload = Buffer.alloc(512 * 1024)
for (let index = 0; index < payload.length; index++) payload[index] = index % 251
const payloadHash = createHash('sha256').update(payload).digest('hex')
const upload = 'SuiteCut adversarial upload: Unicode λ 漢字\n'.repeat(4096)
const uploadPath = resolve(output, 'upload.txt')
await writeFile(uploadPath, upload)
const retainedDownloads = []
const tracked = new Map()
const metrics = new Map()
const result = {
  passed: false,
  level,
  config,
  sdkPath,
  executablePath,
  started: new Date().toISOString(),
  backend: 'cdp',
  fixture: 'Local adversarial workload; not an official AgentBench, WebArena or BrowserGym score.',
  control:
    'Public SDK APIs; raw protocol only for abort/rejection pressure. Evaluation only verifies page state.',
  measurements: {
    heap: 'Node post-GC; live browser and Node PSS/FD from /proc, sampled approximately once a second.',
    caveat:
      'Browser allocator high-water RSS/PSS alone is not proof of a leak; compare late post-warm batches and wrapper/pending counts.',
  },
  checks: {},
  samples: [],
  actionTiming: {},
  processSamples: [],
}
let browser,
  server,
  sampler,
  origin,
  crossOrigin,
  timedOut = false
let samplingTask = Promise.resolve()
let samplingPaused = false
let activeStreams = 0
let peakProcesses = { pssKiB: 0, rssKiB: 0, fds: 0, processes: 0 }

function count(key, amount = 1) {
  result.checks[key] = (result.checks[key] ?? 0) + amount
}
async function timed(name, work) {
  const start = performance.now()
  try {
    return await work()
  } finally {
    let samples = metrics.get(name)
    if (!samples) metrics.set(name, (samples = []))
    samples.push(performance.now() - start)
  }
}
async function eventually(read, expected, timeoutMs = 2500) {
  const deadline = performance.now() + timeoutMs
  let actual
  while (performance.now() < deadline) {
    try {
      actual = await read()
      if (actual === expected) return
    } catch {
      /* A navigation may briefly destroy the old execution context. */
    }
    await delay(10)
  }
  assert.equal(actual, expected, 'Browser state did not settle before the deadline')
}
function quantiles(values) {
  const sorted = [...values].sort((a, b) => a - b)
  return {
    count: sorted.length,
    p50Ms: sorted[Math.floor(sorted.length * 0.5)],
    p95Ms: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95))],
    p99Ms: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.99))],
    maxMs: sorted.at(-1),
  }
}
async function processSample(label) {
  const entries = await readdir('/proc')
  const all = []
  await Promise.all(
    entries
      .filter((value) => /^\d+$/u.test(value))
      .map(async (value) => {
        try {
          const stat = await readFile(`/proc/${value}/stat`, 'utf8')
          const fields = stat.slice(stat.lastIndexOf(')') + 2).split(' ')
          all.push({ pid: Number(value), parent: Number(fields[1]), start: fields[19] })
        } catch {
          /* A process may exit while reading /proc. */
        }
      }),
  )
  const ids = new Set([process.pid])
  for (let previous = -1; previous !== ids.size;) {
    previous = ids.size
    for (const item of all) if (ids.has(item.parent)) ids.add(item.pid)
  }
  const processes = []
  await Promise.all(
    all
      .filter((item) => ids.has(item.pid))
      .map(async (item) => {
        try {
          const [smaps, fds, command] = await Promise.all([
            readFile(`/proc/${item.pid}/smaps_rollup`, 'utf8'),
            readdir(`/proc/${item.pid}/fd`),
            readFile(`/proc/${item.pid}/cmdline`, 'utf8'),
          ])
          const entry = {
            ...item,
            identity: `${item.pid}:${item.start}`,
            fds: fds.length,
            pssKiB: Number(/^Pss:\s+(\d+)/mu.exec(smaps)?.[1] ?? 0),
            rssKiB: Number(/^Rss:\s+(\d+)/mu.exec(smaps)?.[1] ?? 0),
            kind:
              item.pid === process.pid
                ? 'harness'
                : (command
                    .split('\0')
                    .find((value) => value.startsWith('--type='))
                    ?.slice(7) ?? 'browser'),
          }
          processes.push(entry)
          if (item.pid !== process.pid) tracked.set(entry.identity, item)
        } catch {
          /* Process exit can race a sample. */
        }
      }),
  )
  const sum = processes.reduce(
    (total, item) => ({
      pssKiB: total.pssKiB + item.pssKiB,
      rssKiB: total.rssKiB + item.rssKiB,
      fds: total.fds + item.fds,
      processes: total.processes + 1,
    }),
    { pssKiB: 0, rssKiB: 0, fds: 0, processes: 0 },
  )
  for (const key of Object.keys(sum)) peakProcesses[key] = Math.max(peakProcesses[key], sum[key])
  const sample = { label, elapsedMs: performance.now() - started, ...sum, members: processes }
  if (result.processSamples.length < 400) result.processSamples.push(sample)
  return sample
}
async function heapSample(label) {
  // Procfs readFile buffers are measurement allocations, not SDK retention.
  samplingPaused = true
  await samplingTask
  globalThis.gc()
  await delay(15)
  globalThis.gc()
  const sample = {
    label,
    elapsedMs: performance.now() - started,
    ...process.memoryUsage(),
    wire: browser?.connectionStats(),
    pageWrappers: browser?.pageObjects?.size,
    contextWrappers: browser?.contextObjects?.size,
    retainedDownloads: retainedDownloads.length,
  }
  result.samples.push(sample)
  samplingPaused = false
  return sample
}

function html(requestURL) {
  const url = new URL(requestURL, origin)
  if (url.pathname === '/popup')
    return '<!doctype html><title>Workspace</title><p>Independent workspace popup</p>'
  if (url.pathname === '/frame') {
    const depth = Number(url.searchParams.get('depth') ?? 0)
    const stamp = url.searchParams.get('stamp') ?? 'initial'
    return `<!doctype html><title>Frame ${depth}</title><label>Frame value<input id="frame-value"></label>
      <button id="frame-action" onclick="document.querySelector('output').textContent=document.querySelector('input').value;window.trusted=event.isTrusted">Commit frame</button>
      <output></output><div id="stamp">${stamp}</div>
      <a id="frame-nav" href="${origin}/frame?depth=${depth}&stamp=navigated">Navigate frame</a>
      ${depth < 1 ? `<iframe id="nested" src="${origin}/frame?depth=1&stamp=${stamp}"></iframe>` : ''}`
  }
  return `<!doctype html><meta charset="utf-8"><title>Adversarial SDK workload</title>
    <style>body{font:14px sans-serif}label,button{margin:3px}#noise{display:none}iframe{display:block;width:600px;height:260px}#shadows>div{display:inline-block}</style>
    <label>Customer<input id="customer"></label><label>Budget<input type="number" id="budget"></label>
    <label>Due date<input type="date" id="date"></label><label>Region<select id="region"><option value="a">Asia</option><option value="e">Europe</option></select></label>
    <input id="upload" type="file" multiple>
    <button id="commit" onclick="window.commits=(window.commits||0)+1;window.trusted=event.isTrusted">Commit transaction</button>
    <button id="prompt" onclick="document.querySelector('#answer').textContent=prompt('Approval code','default')??'cancelled'">Approval</button><output id="answer"></output>
    <button id="popup" onclick="window.open('${origin}/popup')">Open workspace</button>
    <a id="download" href="/download">Export binary</a><a id="slow" href="/download?slow=1">Slow export</a>
    <button id="replace" onclick="document.querySelector('#outer').outerHTML='<iframe id=outer src=&quot;${crossOrigin}/frame?stamp=replaced&quot;></iframe>'">Replace frame</button>
    <iframe id="outer" src="${crossOrigin}/frame"></iframe>
    <div id="shadows">${Array.from({ length: config.shadows }, (_, i) => `<div data-shadow="${i}"></div>`).join('')}</div>
    <div id="noise">${'<div class="noise">Noise payload</div>'.repeat(config.denseNodes)}
    ${Array.from({ length: config.labels }, (_, i) => `<label>Noise label ${i}<input></label>`).join('')}</div>
    <script>for(const host of document.querySelectorAll('[data-shadow]')){const i=host.dataset.shadow;host.attachShadow({mode:'open'}).innerHTML='<button>Shadow '+i+'</button>';host.shadowRoot.querySelector('button').onclick=event=>{window.shadow=i;window.shadowTrusted=event.isTrusted}}</script>`
}
function serve(request, response) {
  if (request.url?.startsWith('/download')) {
    response.writeHead(200, {
      'content-type': 'application/octet-stream',
      'content-disposition': 'attachment; filename="stress.bin"',
      'content-length': payload.length,
      'cache-control': 'no-store',
    })
    if (!request.url.includes('slow=1')) {
      response.end(payload)
      return
    }
    activeStreams++
    let offset = 0
    const interval = setInterval(() => {
      if (offset >= payload.length) {
        clearInterval(interval)
        response.end()
        return
      }
      response.write(payload.subarray(offset, offset + 4096))
      offset += 4096
    }, 30)
    response.on('close', () => {
      clearInterval(interval)
      activeStreams--
    })
    return
  }
  response.writeHead(200, {
    'content-type': 'text/html; charset=utf-8',
    'cache-control': 'no-store',
  })
  response.end(html(request.url ?? '/'))
}

async function workflow(context, sequence, pageIndex) {
  const page = await context.newPage()
  page.setDefaultTimeout(2500)
  try {
    await timed('denseNavigation', () =>
      page.goto(`${origin}/work?seq=${sequence}&page=${pageIndex}`),
    )
    const token = `${sequence}-${pageIndex}`
    await timed('denseLabelFill', () => page.getByLabel('Customer', { exact: true }).fill(token))
    await page.getByLabel('Budget', { exact: true }).fill('12009.25')
    await page.getByLabel('Due date', { exact: true }).fill('2026-12-17')
    await page.getByLabel('Region', { exact: true }).selectOption({ label: 'Europe' })
    await timed('denseRoleClick', () =>
      page.getByRole('button', { name: `Shadow ${config.shadows - 1}`, exact: true }).click(),
    )
    await page.locator('#commit').click()
    const state = await page.evaluate(() => ({
      customer: document.querySelector('#customer').value,
      budget: document.querySelector('#budget').value,
      date: document.querySelector('#date').value,
      region: document.querySelector('#region').value,
      trusted: window.trusted,
      shadow: window.shadow,
      shadowTrusted: window.shadowTrusted,
      commits: window.commits,
      nodes: document.querySelectorAll('*').length,
    }))
    assert.deepEqual(
      { ...state, nodes: 0 },
      {
        customer: token,
        budget: '12009.25',
        date: '2026-12-17',
        region: 'e',
        trusted: true,
        shadow: String(config.shadows - 1),
        shadowTrusted: true,
        commits: 1,
        nodes: 0,
      },
    )
    assert.ok(state.nodes > config.denseNodes)
    count('denseDOMFormsAndTrustedShadow')

    await timed('oopifNestedInput', async () => {
      const outer = page.frameLocator('#outer')
      await outer.getByLabel('Frame value', { exact: true }).fill(`outer-${token}`)
      await outer.locator('#frame-action').click()
      const nested = outer.frameLocator('#nested')
      await nested.getByLabel('Frame value', { exact: true }).fill(`nested-${token}`)
      await nested.locator('#frame-action').click()
      assert.equal(await outer.locator('output').textContent(), `outer-${token}`)
      assert.equal(await nested.locator('output').textContent(), `nested-${token}`)
      assert.equal(await nested.evaluate(() => window.trusted), true)
      await outer.locator('#frame-nav').click()
      // Cross-site renderer replacement must preserve the iframe locator's document scope.
      await eventually(() => outer.locator('#stamp').textContent(), 'navigated')
      await outer.getByLabel('Frame value', { exact: true }).fill(`navigated-${token}`)
      assert.equal(await outer.locator('#stamp').textContent(), 'navigated')
    })
    const oldFrames = await page.frames()
    await page.locator('#replace').click()
    await page
      .frameLocator('#outer')
      .getByLabel('Frame value', { exact: true })
      .fill(`replacement-${token}`)
    assert.equal(await page.frameLocator('#outer').locator('#stamp').textContent(), 'replaced')
    assert.ok(
      oldFrames.some((frame) => frame.isDetached()),
      'Replacing iframe retires at least one old frame',
    )
    await page.frames()
    count('nestedOOPIFAndFrameReplacement')

    await timed('uploadAndClear', async () => {
      await page.locator('#upload').setInputFiles(uploadPath)
      assert.equal(
        await page.evaluate(() => document.querySelector('#upload').files[0].text()),
        upload,
      )
      await page.locator('#upload').setInputFiles([])
      assert.equal(await page.evaluate(() => document.querySelector('#upload').files.length), 0)
    })
    count('uploadActualBytesAndClear')

    await timed('dialogRoundTrip', async () => {
      const pending = page.waitForEvent('dialog', { timeout: 2500 })
      void pending.catch(() => undefined)
      const click = page.locator('#prompt').click()
      const dialog = await pending
      assert.equal(dialog.type(), 'prompt')
      assert.equal(dialog.defaultValue(), 'default')
      await dialog.accept(token)
      await click
      assert.equal(await page.locator('#answer').textContent(), token)
    })
    await assert.rejects(page.waitForEvent('dialog', { timeout: 3 }), /timed out/u)
    assert.equal(page.waiters.size, 0, 'Expired waiters are removed')
    count('dialogAcceptanceAndExpiredWaitCleanup')

    await timed('downloadActualBytes', async () => {
      const pending = page.waitForEvent('download', { timeout: 2500 })
      void pending.catch(() => undefined)
      await page.locator('#download').click()
      const download = await pending
      const actual = await readFile(await download.path())
      assert.equal(actual.length, payload.length)
      assert.equal(createHash('sha256').update(actual).digest('hex'), payloadHash)
      retainedDownloads.push(download)
    })
    await timed('downloadCancel', async () => {
      const pending = page.waitForEvent('download', { timeout: 2500 })
      void pending.catch(() => undefined)
      await page.locator('#slow').click()
      const download = await pending
      await download.cancel()
      assert.match(await download.failure(), /cancel/u)
      await assert.rejects(download.path(), /failed/u)
      await download.delete()
    })
    count('completedRetainedDownloadAndSlowCancellation')

    await timed('popupOpenBackgroundScreenshotClose', async () => {
      const pending = page.waitForEvent('popup', { timeout: 2500 })
      void pending.catch(() => undefined)
      await page.locator('#popup').click()
      const popup = await pending
      popup.setDefaultTimeout(2500)
      await popup.bringToFront()
      const image = await page.screenshot()
      assert.ok(image.length > 100)
      assert.equal(image.subarray(1, 4).toString(), 'PNG')
      const closedWaiter = assert.rejects(popup.waitForEvent('dialog', { timeout: 0 }), /closed/u)
      await popup.close()
      await closedWaiter
      assert.equal(popup.isClosed(), true)
    })
    count('popupLifecycleAndBackgroundScreenshot')
    // A failed public operation must not poison the page's serialized action queue.
    const previousTimeout = 25
    page.setDefaultTimeout(previousTimeout)
    await assert.rejects(
      page.locator('#definitely-absent').click(),
      /timed out|not found|matching/u,
    )
    page.setDefaultTimeout(2500)
    await page.locator('#commit').click()
    assert.equal(await page.evaluate(() => window.commits), 2)
    count('failedActionQueueRecovery')
  } finally {
    await page.close()
  }
}

async function protocolPressure() {
  const before = browser.connectionStats().pending
  const requests = []
  for (let index = 0; index < 40; index++) {
    const controller = new AbortController()
    const request = browser.sendCommand(
      'SuiteCut.NonexistentMethod',
      {},
      { signal: controller.signal, timeoutMs: 100 },
    )
    if (index % 2 === 0) controller.abort(new Error('stress cancellation'))
    requests.push(request)
  }
  const outcomes = await Promise.allSettled(requests)
  assert.ok(outcomes.every((value) => value.status === 'rejected'))
  assert.equal(browser.connectionStats().pending, before)
  count('abortedOrRejectedProtocolRequests', outcomes.length)
}

const started = performance.now()
let deadline
try {
  server = createServer((request, response) => {
    try {
      serve(request, response)
    } catch (error) {
      response.destroy(error)
    }
  })
  await new Promise((resolveListen) => server.listen(0, '0.0.0.0', resolveListen))
  const port = server.address().port
  origin = `http://127.0.0.1:${port}`
  crossOrigin = `http://localhost:${port}`
  await processSample('before-launch')
  browser = await sdk.launchBrowser({
    backend: 'cdp',
    executablePath,
    headless: true,
    args:
      process.getuid?.() === 0
        ? ['--no-sandbox', '--disable-dev-shm-usage', '--site-per-process']
        : ['--site-per-process'],
    viewport: { width: 1100, height: 900 },
    timeoutMs: 15_000,
  })
  // Bounded diagnostic ring: never retain DOM expressions, bodies, or protocol results.
  const wire = browser.driver.wire
  const request = wire.request.bind(wire)
  const ring = new Array(512)
  let traceSequence = 0
  const trace = (entry) => {
    const sequence = traceSequence++
    ring[sequence % ring.length] = { sequence, elapsedMs: performance.now() - started, ...entry }
  }
  wire.request = async (method, params = {}, requestOptions = {}) => {
    const id = wire.nextId + 1
    const begin = performance.now()
    const fields = {
      id,
      method,
      sessionId: requestOptions.sessionId,
      type: params.type,
      x: params.x,
      y: params.y,
      frameId: params.frameId,
      contextId: params.contextId,
      expressionTail:
        typeof params.expression === 'string' ? params.expression.slice(-140) : undefined,
    }
    trace({ phase: 'start', ...fields })
    try {
      const value = await request(method, params, requestOptions)
      trace({ phase: 'success', id, method, durationMs: performance.now() - begin })
      return value
    } catch (error) {
      trace({
        phase: 'error',
        ...fields,
        durationMs: performance.now() - begin,
        error: String(error),
      })
      throw error
    }
  }
  for (const method of [
    'Target.detachedFromTarget',
    'Target.attachedToTarget',
    'Page.frameNavigated',
    'Page.frameDetached',
  ])
    wire.on(method, (event, sessionId) => trace({ phase: 'event', method, sessionId, event }))
  result.recentProtocolTrace = ring
  result.version = browser.version()
  result.capabilities = browser.capabilities()
  // Retire the launcher's initial tab; keep the default context as a stable baseline.
  for (const page of await browser.pages()) await page.close()
  await browser.pages()
  await browser.contexts()
  result.baselineWrappers = {
    pages: browser.pageObjects.size,
    contexts: browser.contextObjects.size,
  }
  result.baselineWire = browser.connectionStats()
  let sampling = false
  sampler = setInterval(() => {
    if (sampling || samplingPaused) return
    sampling = true
    samplingTask = processSample('periodic')
      .catch(() => undefined)
      .finally(() => {
        sampling = false
      })
  }, 1000)
  await heapSample('launched')
  const work = async () => {
    for (let batch = 0; batch < config.batches; batch++) {
      const batchStart = performance.now()
      for (let cycle = 0; cycle < config.cycles; cycle++) {
        const context = await browser.newContext()
        try {
          await Promise.all(
            Array.from({ length: config.pages }, (_, pageIndex) =>
              workflow(context, `${batch}-${cycle}`, pageIndex),
            ),
          )
        } finally {
          await context.close()
        }
        count('isolatedContextLifecycle')
      }
      await protocolPressure()
      await browser.pages()
      await browser.contexts()
      const sample = await heapSample(`batch-${batch}`)
      sample.batchMs = performance.now() - batchStart
      assert.equal(sample.wire.pending, 0)
      assert.equal(sample.pageWrappers, result.baselineWrappers.pages)
      assert.equal(sample.contextWrappers, result.baselineWrappers.contexts)
      await processSample(`batch-${batch}`)
      console.log(
        JSON.stringify({
          batch,
          completed: batch + 1,
          total: config.batches,
          heapUsed: sample.heapUsed,
          pending: sample.wire.pending,
          listeners: sample.wire.eventListeners,
          batchMs: sample.batchMs,
        }),
      )
    }
  }
  await Promise.race([
    work(),
    new Promise((_, reject) => {
      deadline = setTimeout(() => {
        timedOut = true
        reject(new Error(`Stress deadline ${config.deadlineMs}ms exceeded`))
      }, config.deadlineMs)
    }),
  ])
  clearTimeout(deadline)
  result.postWarmGrowth = (() => {
    const samples = result.samples.filter((sample) => sample.label.startsWith('batch-'))
    const warm = samples[Math.min(1, samples.length - 1)]
    const final = samples.at(-1)
    return {
      from: warm.label,
      to: final.label,
      heapUsedBytes: final.heapUsed - warm.heapUsed,
      externalBytes: final.external - warm.external,
      rssBytes: final.rss - warm.rss,
      retainedHandleIncrease: final.retainedDownloads - warm.retainedDownloads,
      listenersIncrease: final.wire.eventListeners - warm.wire.eventListeners,
    }
  })()
  if (result.postWarmGrowth.heapUsedBytes > 8 * 1024 * 1024) {
    result.heapSnapshot = writeHeapSnapshot(resolve(output, 'growth.heapsnapshot'))
  }
  result.passed = true
} catch (error) {
  result.error = { message: error.message, stack: error.stack, timedOut }
  process.exitCode = 1
} finally {
  clearTimeout(deadline)
  clearInterval(sampler)
  await samplingTask
  result.beforeClose = browser?.connectionStats()
  try {
    await browser?.close()
  } catch (error) {
    result.closeError = error.message
    result.passed = false
    process.exitCode = 1
  }
  result.afterClose = browser?.connectionStats()
  if (browser) {
    assert.equal(browser.connectionStats().pending, 0)
    assert.equal(browser.connectionStats().eventListeners, 0)
  }
  // Retained completed handles should fail cleanly once managed storage is closed.
  const firstDownload = retainedDownloads[0]
  if (firstDownload) {
    try {
      await assert.rejects(firstDownload.path(), /closed/u)
      count('retainedHandleStorageClosed')
    } catch (error) {
      result.handleCloseError = error.message
      result.passed = false
      process.exitCode = 1
    }
  }
  retainedDownloads.length = 0
  await heapSample('after-close')
  await processSample('after-close')
  result.survivingTrackedProcesses = []
  result.awaitingSupervisorReap = []
  for (const item of tracked.values()) {
    try {
      const stat = await readFile(`/proc/${item.pid}/stat`, 'utf8')
      const fields = stat.slice(stat.lastIndexOf(')') + 2).split(' ')
      if (fields[19] === item.start) {
        if (fields[0] === 'Z' && process.env.SUITECUT_STRESS_SUPERVISED === '1')
          result.awaitingSupervisorReap.push(item)
        else result.survivingTrackedProcesses.push({ ...item, state: fields[0] })
      }
    } catch {
      /* Natural browser termination. */
    }
  }
  if (result.survivingTrackedProcesses.length) {
    result.passed = false
    process.exitCode = 1
  }
  if (server) {
    server.closeAllConnections()
    await new Promise((resolveClose) => server.close(resolveClose))
  }
  if (result.recentProtocolTrace)
    result.recentProtocolTrace = result.recentProtocolTrace
      .filter(Boolean)
      .sort((a, b) => a.sequence - b.sequence)
  result.activeDownloadStreamsAfterClose = activeStreams
  result.peakProcessTotals = peakProcesses
  result.trackedProcessIdentities = [...tracked.keys()]
  result.durationMs = performance.now() - started
  for (const [name, samples] of metrics) result.actionTiming[name] = quantiles(samples)
  await writeFile(resolve(output, 'results.json'), `${JSON.stringify(result, null, 2)}\n`)
  console.log(
    JSON.stringify({
      passed: result.passed,
      durationMs: result.durationMs,
      checks: result.checks,
      growth: result.postWarmGrowth,
      peakProcessTotals: peakProcesses,
      survivors: result.survivingTrackedProcesses.length,
      output: resolve(output, 'results.json'),
      error: result.error?.message,
    }),
  )
}
