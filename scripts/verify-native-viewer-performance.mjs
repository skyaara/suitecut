/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-argument -- Selectable emitted SDK and synthetic native-frame source; HTTP and JPEG output are asserted. */
/* global fetch */
// SPDX-License-Identifier: MIT
// Real JPEG/HTTP workload with a synthetic capture source; no browser or network fixture required.
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { mkdir, writeFile } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { clearInterval, setInterval } from 'node:timers'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const sdk = resolve(process.env.SUITECUT_VIEWER_SDK ?? 'packages/browser-control/dist/browser.js')
const output = resolve(
  process.env.SUITECUT_VIEWER_PROOF ?? '.suitecut/diagnostics/viewer-performance.json',
)
const { createBrowserViewer } = await import(pathToFileURL(sdk))
const listeners = new Set()
const width = 1920,
  height = 1080
const data = Buffer.alloc(width * height * 4)
for (let i = 0; i < data.length; i += 4) {
  data[i] = (i >>> 3) & 255
  data[i + 1] = (i >>> 8) & 255
  data[i + 2] = (i >>> 12) & 255
  data[i + 3] = 255
}
let current
const source = {
  width,
  height,
  viewport: { width, height },
  pixelFormat: 'bgra',
  pageId: 1,
  processSignal: new AbortController().signal,
  onFrame(listener) {
    listeners.add(listener)
    if (current) listener(current)
    return () => listeners.delete(listener)
  },
}
const page = {
  source,
  isClosed: () => false,
  context: () => ({}),
  viewportSize: () => source.viewport,
  url: () => 'https://viewer-fixture.invalid/',
}
const browser = {
  source,
  isConnected: () => true,
  contexts: () => [{ pages: () => [page] }],
}
const emit = () => {
  current = {
    data,
    pixelFormat: 'bgra',
    width,
    height,
    viewport: source.viewport,
    contentRect: { x: 0, y: 0, width, height },
    timestampMs: performance.now(),
    droppedFrames: 0,
  }
  for (const listener of listeners) listener(current)
}
emit()
const viewer = await createBrowserViewer({ browser, page, framesPerSecond: 30 })
const link = new URL(viewer.url)
const authorization = `Bearer ${link.hash.slice(1)}`
const capture = setInterval(emit, 1000 / 60)
let peakRSS = process.memoryUsage().rss
const sample = setInterval(() => {
  peakRSS = Math.max(peakRSS, process.memoryUsage().rss)
}, 10)
try {
  const started = performance.now()
  let first
  while (performance.now() - started < 5000) {
    const response = await fetch(link.origin + '/frame?revision=1&after=0&wait=1', {
      headers: { authorization },
    })
    if (response.status === 200) {
      first = Buffer.from(await response.arrayBuffer())
      break
    }
    assert.equal(response.status, 204)
    await delay(5)
  }
  assert(first?.length, 'A late viewer receives the cached native paint')
  assert.equal(first[0], 0xff)
  assert.equal(first[1], 0xd8)
  const sequenceAtDelivery = viewer.state().frameSequence
  const cpu = process.cpuUsage()
  const idleStart = performance.now()
  // Capture continues after the last viewer request, as during a background tab.
  await delay(1500)
  const idleCpu = process.cpuUsage(cpu)
  const result = {
    sdk,
    width,
    height,
    captureFramesPerSecond: 60,
    encodingFramesPerSecondLimit: 30,
    firstFrameMs: Number((idleStart - started).toFixed(2)),
    jpegBytes: first.length,
    idleWindowMs: Number((performance.now() - idleStart).toFixed(2)),
    idleEncodingJobs: viewer.state().frameSequence - sequenceAtDelivery,
    idleCPUms: Number(((idleCpu.user + idleCpu.system) / 1000).toFixed(2)),
    peakRSSBytes: peakRSS,
    finalMemory: process.memoryUsage(),
  }
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, JSON.stringify(result, null, 2) + '\n')
  process.stdout.write(JSON.stringify(result) + '\n')
} finally {
  clearInterval(capture)
  clearInterval(sample)
  await viewer.close()
  assert.equal(listeners.size, 0)
}
