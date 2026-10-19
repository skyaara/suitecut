/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Exercises a selectable built SDK against the real native engine. */
/* global fetch, console */
import assert from 'node:assert/strict'
import { execFileSync } from 'node:child_process'
import { once } from 'node:events'
import { readFileSync, readdirSync } from 'node:fs'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const output = resolve(process.env.SUITECUT_PERF_OUTPUT ?? '.suitecut/performance-review')
const sdk = resolve(process.env.SUITECUT_PERF_SDK ?? 'dist/browser.js')
const executablePath = resolve(
  process.env.SUITECUT_NATIVE_EXECUTABLE ?? '.suitecut/native-build/suitecut-browser',
)
const { launchBrowser, createBrowserViewer } = await import(pathToFileURL(sdk))
const ticksPerSecond = Number(execFileSync('getconf', ['CLK_TCK'], { encoding: 'utf8' }))
assert(globalThis.gc, 'Use --expose-gc for post-GC memory samples')
await mkdir(output, { recursive: true })
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(
    `<!doctype html><style>body{margin:0;background:#e3ebe8}#patch{width:64px;height:64px;background:#36945b;position:absolute;top:100px;left:100px}</style><div id="patch"></div><script>let n=0;function animate(){document.querySelector('#patch').style.backgroundColor=(++n%2)?'#36945b':'#54a1bf';requestAnimationFrame(animate)};if(location.pathname!='/static')animate()</script>`,
  )
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const url = `http://127.0.0.1:${server.address().port}`
function processTree() {
  const all = []
  for (const value of readdirSync('/proc')) {
    if (!/^\d+$/u.test(value)) continue
    try {
      const stat = readFileSync(`/proc/${value}/stat`, 'utf8'),
        fields = stat.slice(stat.lastIndexOf(')') + 2).split(' ')
      all.push({
        pid: Number(value),
        parent: Number(fields[1]),
        start: fields[19],
        ticks: Number(fields[11]) + Number(fields[12]),
      })
    } catch {
      /* A process may finish between enumeration and sampling. */
    }
  }
  const selected = new Set([process.pid])
  let changed = true
  while (changed) {
    changed = false
    for (const item of all)
      if (selected.has(item.parent) && !selected.has(item.pid)) {
        selected.add(item.pid)
        changed = true
      }
  }
  return all.filter((item) => selected.has(item.pid))
}
function memory() {
  let nativePSSKiB = 0
  for (const item of processTree()) {
    if (item.pid === process.pid) continue
    try {
      nativePSSKiB += Number(
        /^Pss:\s+(\d+)/mu.exec(readFileSync(`/proc/${item.pid}/smaps_rollup`, 'utf8'))?.[1] ?? 0,
      )
    } catch {
      /* Process exited. */
    }
  }
  return { ...process.memoryUsage(), nativePSSKiB }
}
function cpu(before, after, elapsedMs) {
  let nodeMs = 0,
    nativeMs = 0
  const start = new Map(before.map((item) => [`${item.pid}:${item.start}`, item.ticks]))
  for (const item of after) {
    const first = start.get(`${item.pid}:${item.start}`)
    if (first === undefined) continue
    const ms = ((item.ticks - first) * 1000) / ticksPerSecond
    if (item.pid === process.pid) nodeMs += ms
    else nativeMs += ms
  }
  return {
    nodeMs,
    nativeMs,
    nodeCorePercent: (100 * nodeMs) / elapsedMs,
    nativeCorePercent: (100 * nativeMs) / elapsedMs,
  }
}
const results = []
let browser, viewer, off
try {
  for (const pixelFormat of process.env.SUITECUT_PERF_CASE === 'viewer' ? [] : ['bgra', 'i420']) {
    browser = await launchBrowser({
      executablePath,
      headless: true,
      width: 3840,
      height: 2160,
      pixelFormat,
      framesPerSecond: 30,
    })
    const page = await browser.newPage()
    await page.goto(url + '/animated')
    let frames = 0,
      bytes = 0
    off = page.source.onFrame((frame) => {
      frames++
      bytes += frame.data.length
    })
    await delay(600)
    const samples = []
    for (let repeat = 0; repeat < 3; repeat++) {
      globalThis.gc()
      const startMemory = memory(),
        before = processTree(),
        started = performance.now(),
        firstFrame = frames,
        firstByte = bytes
      await delay(2000)
      const elapsedMs = performance.now() - started,
        after = processTree()
      globalThis.gc()
      samples.push({
        elapsedMs,
        frames: frames - firstFrame,
        bytes: bytes - firstByte,
        deliveredFPS: ((frames - firstFrame) * 1000) / elapsedMs,
        cpu: cpu(before, after, elapsedMs),
        startMemory,
        endMemory: memory(),
      })
      assert(frames > firstFrame, 'No frames arrived under dirty-rectangle animation')
    }
    results.push({ name: '4K-small-dirty-paint', pixelFormat, samples })
    off()
    off = undefined
    await browser.close()
    assert.equal(browser.source.processExit().code, 0)
    browser = undefined
  }
  browser = await launchBrowser({
    executablePath,
    headless: true,
    width: 1280,
    height: 720,
    framesPerSecond: 60,
  })
  const page = await browser.newPage()
  await page.goto(url + '/animated')
  viewer = await createBrowserViewer({ browser, page, framesPerSecond: 30 })
  const link = new URL(viewer.url),
    headers = { authorization: 'Bearer ' + link.hash.slice(1) }
  let sequence = 0,
    delivered = 0
  const before = processTree(),
    started = performance.now()
  for (let request = 0; request < 15; request++) {
    const current = viewer.state(),
      response = await fetch(
        `${link.origin}/frame?after=${sequence}&revision=${current.revision}&wait=1`,
        { headers },
      )
    if (response.status === 200) {
      assert((await response.arrayBuffer()).byteLength > 0)
      sequence = Number(response.headers.get('X-Frame-Sequence'))
      delivered++
    } else assert.equal(response.status, 204)
    await delay(200)
  }
  const elapsedMs = performance.now() - started,
    after = processTree(),
    activeSequence = viewer.state().frameSequence
  const idleStarted = performance.now(),
    idleBefore = processTree()
  await delay(1500)
  const idleElapsedMs = performance.now() - idleStarted,
    idleAfter = processTree(),
    idleSequence = viewer.state().frameSequence
  results.push({
    name: 'viewer-five-requests-per-second',
    delivered,
    encoded: activeSequence,
    elapsedMs,
    cpu: cpu(before, after, elapsedMs),
    idle: {
      elapsedMs: idleElapsedMs,
      encoded: idleSequence - activeSequence,
      cpu: cpu(idleBefore, idleAfter, idleElapsedMs),
    },
    memory: memory(),
  })
  assert(delivered > 0)
} catch (error) {
  process.exitCode = 1
  results.push({ error: String(error), stack: error.stack })
} finally {
  off?.()
  await viewer?.close()
  await browser?.close()
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  const report = {
    status: process.exitCode ? 'FAIL' : 'PASS',
    sdk,
    executablePath,
    results,
    scope:
      'Three two-second real 4K dirty-paint samples per pixel format plus a bounded low-demand viewer workload. CPU is process-tree ticks; native memory is PSS, not shared-library-inflated RSS. Shared cloud host; scoped observations, not a whole-engine speedup.',
  }
  await writeFile(output + '/result.json', JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
}
