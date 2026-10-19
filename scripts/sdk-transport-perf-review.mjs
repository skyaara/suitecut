// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return -- Compares separately compiled SDK revisions through actual pipe delivery. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import childProcess from 'node:child_process'
import { mkdir, writeFile } from 'node:fs/promises'
import { syncBuiltinESMExports } from 'node:module'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { PassThrough } from 'node:stream'
import { setImmediate } from 'node:timers'
import { pathToFileURL } from 'node:url'

const [beforeDirectory, afterDirectory, reportPath] = process.argv.slice(2)
assert(
  beforeDirectory && afterDirectory && reportPath,
  'Usage: node --expose-gc scripts/sdk-transport-perf-review.mjs BEFORE_DIST AFTER_DIST REPORT.json',
)
const originalSpawn = childProcess.spawn
let latestChild
childProcess.spawn = () => {
  const child = new childProcess.ChildProcess()
  child.stdin = new PassThrough()
  child.stdout = new PassThrough()
  child.stderr = new PassThrough()
  child.kill = () => {
    globalThis.queueMicrotask(() => child.emit('close', 1))
    return true
  }
  child.stdin.once('finish', () => globalThis.queueMicrotask(() => child.emit('close', 0)))
  latestChild = child
  return child
}
syncBuiltinESMExports()
const revisions = await Promise.all(
  [beforeDirectory, afterDirectory].map(async (directory) => ({
    browser: await import(pathToFileURL(resolve(directory, 'native-browser.js')).href),
    protocol: await import(pathToFileURL(resolve(directory, 'native-protocol.js')).href),
  })),
)
function packet(header, payload = Buffer.alloc(0)) {
  const json = Buffer.from(JSON.stringify({ version: 1, bytes: payload.length, ...header }))
  const prefix = Buffer.allocUnsafe(4)
  prefix.writeUInt32LE(json.length)
  return Buffer.concat([prefix, json, payload])
}
const control = Buffer.concat(
  Array.from({ length: 64 }, (_, index) => packet({ type: 'response', id: index + 1, ok: true })),
)
const worker = packet(
  { type: 'contextworker' },
  Buffer.from(
    JSON.stringify({
      contextId: 1,
      workerEvent: {
        event: 'updated',
        sequence: 1,
        worker: { id: 'worker-1', url: 'https://example.test/worker.js', type: 'worker' },
      },
    }),
  ),
)
const workerPredicate = packet(
  { type: 'contextworkerpredicate' },
  Buffer.from(
    JSON.stringify({
      contextId: 1,
      id: 1,
      generation: 1,
      token: 1,
      sequence: 1,
      clientToken: 'client-1',
      event: 'console',
      value: {
        type: 'log',
        text: 'message',
        location: {
          url: 'https://example.test/worker.js',
          lineNumber: 0,
          columnNumber: 0,
        },
      },
    }),
  ),
)
const frame = packet(
  { type: 'frame', pixelFormat: 'bgra', width: 1920, height: 1080, timestampMs: 1, dropped: 0 },
  Buffer.alloc(1920 * 1080 * 4, 42),
)
const frameChunks = []
for (let offset = 0; offset < frame.length; offset += 65536)
  frameChunks.push(frame.subarray(offset, offset + 65536))
const cases = [
  {
    name: 'control-64-coalesced',
    iterations: 1000,
    packetsPerIteration: 64,
    make: (revision) => {
      let delivered = 0
      const decoder = new revision.protocol.NativePacketDecoder(() => {
        delivered++
      })
      return {
        run: () => decoder.push(control),
        finish: (expected) => {
          decoder.end()
          assert.equal(delivered, expected * 64)
          decoder.dispose()
        },
      }
    },
  },
  {
    name: 'bgra-1080p-64k-pipe-reads',
    iterations: 12,
    packetsPerIteration: 1,
    make: (revision) => {
      let delivered = 0
      const decoder = new revision.protocol.NativePacketDecoder((header, data) => {
        assert.equal(header.type, 'frame')
        assert.equal(data[0], 42)
        assert.equal(data.length, 1920 * 1080 * 4)
        delivered++
      })
      return {
        run: () => {
          for (const chunk of frameChunks) decoder.push(chunk)
        },
        finish: (expected) => {
          decoder.end()
          assert.equal(delivered, expected)
          decoder.dispose()
        },
      }
    },
  },
  ...[
    ['context-worker-message', worker, 'onContextWorkerMessage'],
    ['context-worker-console-predicate', workerPredicate, 'onContextWorkerPredicate'],
  ].map(([name, bytes, subscribe]) => ({
    name,
    iterations: 2000,
    packetsPerIteration: 1,
    make: async (revision) => {
      const launching = revision.browser.launchNativeBrowser({
        executablePath: '/controlled/perf-browser',
        width: 2,
        height: 2,
      })
      while (!latestChild) await new Promise(setImmediate)
      const child = latestChild
      latestChild = undefined
      child.stdout.write(packet({ type: 'ready', timestampMs: 1, cefVersion: 'controlled' }))
      const browser = await launching
      let delivered = 0
      browser[subscribe](() => {
        delivered++
      })
      return {
        run: () => {
          child.stdout.write(bytes)
        },
        finish: async (expected) => {
          assert.equal(delivered, expected)
          assert.equal(browser.state, 'running')
          await browser.close()
        },
      }
    },
  })),
]
const reports = []
try {
  for (const definition of cases) {
    const samples = [[], []]
    for (let round = 0; round < 7; round++) {
      for (const index of round % 2 ? [1, 0] : [0, 1]) {
        const instance = await definition.make(revisions[index])
        const warmup = Math.min(100, definition.iterations)
        for (let i = 0; i < warmup; i++) instance.run()
        globalThis.gc?.()
        const memoryBefore = process.memoryUsage()
        let peakHeap = memoryBefore.heapUsed
        const start = performance.now()
        for (let i = 0; i < definition.iterations; i++) {
          instance.run()
          if ((i & 63) === 0) peakHeap = Math.max(peakHeap, process.memoryUsage().heapUsed)
        }
        const milliseconds = performance.now() - start
        await Promise.resolve(instance.finish(warmup + definition.iterations))
        globalThis.gc?.()
        const memoryAfter = process.memoryUsage()
        samples[index].push({
          milliseconds,
          packetsPerSecond:
            (definition.iterations * definition.packetsPerIteration * 1000) / milliseconds,
          sampledHeapGrowthBytes: peakHeap - memoryBefore.heapUsed,
          retainedHeapDeltaBytes: memoryAfter.heapUsed - memoryBefore.heapUsed,
        })
      }
    }
    const median = (values) => [...values].sort((a, b) => a - b)[Math.floor(values.length / 2)]
    const beforeMs = median(samples[0].map((sample) => sample.milliseconds))
    const afterMs = median(samples[1].map((sample) => sample.milliseconds))
    reports.push({
      name: definition.name,
      iterations: definition.iterations,
      medianBeforeMs: beforeMs,
      medianAfterMs: afterMs,
      speedup: beforeMs / afterMs,
      samples,
    })
    process.stdout.write(
      `${String(definition.name)}: ${(beforeMs / afterMs).toFixed(2)}x; ${beforeMs.toFixed(2)} -> ${afterMs.toFixed(2)} ms\n`,
    )
  }
} finally {
  childProcess.spawn = originalSpawn
  syncBuiltinESMExports()
}
await mkdir(resolve(reportPath, '..'), { recursive: true })
await writeFile(
  reportPath,
  JSON.stringify(
    {
      node: process.version,
      beforeDirectory,
      afterDirectory,
      note: 'Controlled SDK stdout-pipe microbenchmarks; not end-to-end browser latency. Alternating paired samples. Payload data is owned in both revisions. Heap growth is sampled, not a native peak RSS claim.',
      reports,
    },
    null,
    2,
  ) + '\n',
)
