// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Measures selectable emitted SDK revisions rather than importing source classes. */
/* global console */
// Focused, local SDK measurements; accepts emitted module directories so the
// same workload can compare an exact pre-change build with the candidate.
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { spawnSync } from 'node:child_process'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setImmediate as yieldTurn } from 'node:timers/promises'
import { pathToFileURL, fileURLToPath } from 'node:url'

const args = process.argv.slice(2)
const option = (name) => args[args.indexOf(name) + 1]
const load = (directory, name) => import(pathToFileURL(resolve(directory, `${name}.js`)).href)
if (args.includes('--worker')) {
  assert(globalThis.gc, 'Run the worker with --expose-gc')
  const directory = option('--worker')
  const { NativeNetworkBodyReceiver } = await load(directory, 'native-network-body')
  const receiver = () =>
    new NativeNetworkBodyReceiver(
      () => Promise.resolve(),
      (error) => {
        throw error
      },
    )
  const bodyTimes = []
  for (let round = 0; round < 5; round++) {
    const current = receiver()
    const bytes = Buffer.alloc(256, 7)
    const start = performance.now()
    for (let id = 1; id <= 12000; id++) {
      current.receive({ bodyId: String(id), offset: 0, totalBytes: bytes.length }, bytes)
      assert.equal(current.take(String(id))[0], 7)
      if (id % 256 === 0) await yieldTurn()
    }
    bodyTimes.push(performance.now() - start)
    assert.equal(current.archiveDiagnostics().bodies, 4096)
    current.close()
    await yieldTurn()
    globalThis.gc()
  }
  // Complete a turn holding many evicted weak payloads, then consume one more
  // body before collection. A replay bookkeeping scan must not revive all of
  // the old payloads merely to prune their weak identities.
  const memory = receiver()
  const chunk = Buffer.alloc(24 * 1024, 3)
  for (let id = 1; id <= 128; id++) {
    const totalBytes = 512 * 1024
    for (let offset = 0; offset < totalBytes; offset += chunk.length)
      memory.receive(
        { bodyId: String(id), offset, totalBytes },
        chunk.subarray(0, Math.min(chunk.length, totalBytes - offset)),
      )
    memory.take(String(id))
  }
  await yieldTurn()
  await yieldTurn()
  memory.receive({ bodyId: '129', offset: 0, totalBytes: 1 }, Buffer.from([9]))
  memory.take('129')
  globalThis.gc()
  globalThis.gc()
  const arrayBuffersDuringReplay = process.memoryUsage().arrayBuffers
  const archive = memory.archiveDiagnostics()
  memory.close()
  await yieldTurn()
  globalThis.gc()

  const { NativeNetwork } = await load(directory, 'native-network')
  const networkTimes = []
  for (let round = 0; round < 5; round++) {
    const lifetime = new AbortController()
    let receive
    const network = new NativeNetwork(undefined, undefined, {
      signal: lifetime.signal,
      defaultTimeout: () => 1000,
      command: () => Promise.resolve({}),
      body: () => Promise.resolve({}),
      subscribe: (listener) => {
        receive = listener
        return () => {
          receive = undefined
        }
      },
    })
    let requests = 0
    network.on('requestfinished', () => requests++)
    const header = 'x'.repeat(8192)
    const start = performance.now()
    for (let id = 1; id <= 1000; id++) {
      const request = {
        id: String(id),
        requestId: String(id),
        session: '',
        frameId: '',
        url: 'https://local.test/data',
        method: 'GET',
        resourceType: 'Fetch',
        headers: { large: header },
        isNavigationRequest: false,
      }
      receive({ event: 'request', sequence: id * 3, request })
      const response = {
        requestId: String(id),
        session: '',
        frameId: '',
        method: 'GET',
        url: request.url,
        status: 200,
        statusText: 'OK',
        headers: { large: header },
        mimeType: 'text/plain',
        fromDiskCache: false,
        fromServiceWorker: false,
      }
      receive({ event: 'response', sequence: id * 3 + 1, request, response })
      receive({ event: 'requestfinished', sequence: id * 3 + 2, request, response })
    }
    networkTimes.push(performance.now() - start)
    assert.equal(requests, 1000)
    assert.equal(network.metadataDiagnostics().cachedRequests, 1000)
    lifetime.abort()
    assert.equal(network.metadataDiagnostics().cachedRequestBytes, 0)
    await yieldTurn()
    globalThis.gc()
  }
  const median = (values) => values.toSorted((a, b) => a - b)[Math.floor(values.length / 2)]
  console.log(
    JSON.stringify({
      bodyTimes,
      bodyMedianMs: median(bodyTimes),
      networkTimes,
      networkMedianMs: median(networkTimes),
      arrayBuffersDuringReplay,
      archive,
    }),
  )
} else {
  const result = {}
  for (const [label, flag] of [
    ['before', '--before'],
    ['after', '--after'],
  ]) {
    assert(
      args.includes(flag),
      `Usage: node ${fileURLToPath(import.meta.url)} --before emitted-directory --after emitted-directory`,
    )
    const child = spawnSync(
      process.execPath,
      ['--expose-gc', fileURLToPath(import.meta.url), '--worker', resolve(option(flag))],
      { encoding: 'utf8', maxBuffer: 1024 * 1024, timeout: 120000 },
    )
    if (child.status !== 0)
      throw new Error(
        child.stderr.trim() ? child.stderr : (child.error?.message ?? 'Measurement failed'),
      )
    result[label] = JSON.parse(child.stdout)
  }
  result.improvement = {
    bodyThroughput: result.before.bodyMedianMs / result.after.bodyMedianMs,
    networkThroughput: result.before.networkMedianMs / result.after.networkMedianMs,
    arrayBufferBytesAvoidedDuringReplay:
      result.before.arrayBuffersDuringReplay - result.after.arrayBuffersDuringReplay,
  }
  console.log(JSON.stringify(result, null, 2))
}
