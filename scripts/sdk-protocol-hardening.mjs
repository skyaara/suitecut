/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access -- Controlled fuzz/process-model harness selects emitted modules and deliberately malformed protocol values; production SDK validation is tested separately. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

// Run emitted code so this also measures the production parser rather than a test transform.
const [modulePath, outputPath, mode = 'all'] = process.argv.slice(2)
if (!modulePath || !outputPath || !globalThis.gc)
  throw new Error(
    'Use node --expose-gc <script> <emitted native-protocol.js> <result.json> [bench|all]',
  )
const { NativePacketDecoder } = await import(pathToFileURL(modulePath).href)
const seed = 0x51e7c0de
let random = seed
function next() {
  random ^= random << 13
  random ^= random >>> 17
  random ^= random << 5
  return random >>> 0
}
function packet(header, payload = Buffer.alloc(0)) {
  const json = Buffer.from(JSON.stringify({ version: 1, bytes: payload.length, ...header }))
  const wire = Buffer.allocUnsafe(4 + json.length + payload.length)
  wire.writeUInt32LE(json.length)
  json.copy(wire, 4)
  payload.copy(wire, 4 + json.length)
  return wire
}
function sampleMemory() {
  globalThis.gc()
  globalThis.gc()
  const { rss, heapUsed, external, arrayBuffers } = process.memoryUsage()
  return { rss, heapUsed, external, arrayBuffers }
}
function distribution(values) {
  const sorted = [...values].sort((a, b) => a - b)
  return {
    samples: sorted.length,
    minMs: sorted[0],
    p50Ms: sorted[Math.floor(sorted.length / 2)],
    p95Ms: sorted[Math.min(sorted.length - 1, Math.floor(sorted.length * 0.95))],
    maxMs: sorted.at(-1),
  }
}
const result = {
  seed,
  node: process.version,
  platform: process.platform,
  mode,
  cases: 0,
  checks: 0,
  benchmark: {},
  memory: {},
}
const started = performance.now()
const binary = Buffer.alloc(2_097_152)
for (let index = 0; index < binary.length; index++) binary[index] = index % 251
const big = packet({ type: 'response', id: 1, ok: true }, binary)
const small = packet({ type: 'response', id: 1, ok: true })
{
  let calls = 0
  let bytes = 0
  // eslint-disable-next-line @typescript-eslint/unbound-method -- Static Buffer allocator is saved and restored exactly for instrumentation.
  const alloc = Buffer.alloc
  // eslint-disable-next-line @typescript-eslint/unbound-method -- Static Buffer allocator does not use a receiver.
  const allocUnsafe = Buffer.allocUnsafe
  Buffer.alloc = function (size, ...args) {
    calls++
    bytes += size
    return alloc(size, ...args)
  }
  Buffer.allocUnsafe = function (size) {
    calls++
    bytes += size
    return allocUnsafe(size)
  }
  try {
    const decoder = new NativePacketDecoder(() => undefined)
    for (let index = 0; index < 10_000; index++) decoder.push(small)
    decoder.end()
    decoder.dispose()
  } finally {
    Buffer.alloc = alloc
    Buffer.allocUnsafe = allocUnsafe
  }
  result.bufferAllocationProbe = { packets: 10_000, calls, bytes }
}
/** @type {[string, Buffer, number, number, number][]} */
const benchmarks = [
  ['smallCoalesced', Buffer.concat(Array.from({ length: 1000 }, () => small)), 65_536, 10, 21],
  ['binary2MiB64KiB', big, 65_536, 32, 21],
  ['binary2MiB1KiB', big, 1024, 16, 21],
  ['binary2MiB1Byte', big, 1, 1, 5],
]
for (const [name, wire, fragmentSize, repetitions, samples] of benchmarks) {
  const timings = []
  const pieces = []
  for (let offset = 0; offset < wire.length; offset += fragmentSize)
    pieces.push(wire.subarray(offset, offset + fragmentSize))
  for (let sample = -2; sample < samples; sample++) {
    let delivered = 0
    const decoder = new NativePacketDecoder((_header, body) => {
      delivered++
      if (body.length) assert(body.equals(binary))
    })
    const start = performance.now()
    for (let repetition = 0; repetition < repetitions; repetition++)
      for (const part of pieces) decoder.push(part)
    decoder.end()
    const elapsed = performance.now() - start
    assert.equal(delivered, repetitions * (name === 'smallCoalesced' ? 1000 : 1))
    decoder.dispose()
    if (sample >= 0) timings.push(elapsed)
  }
  result.benchmark[name] = {
    ...distribution(timings),
    bytesPerSample: wire.length * repetitions,
    packetsPerSample: repetitions * (name === 'smallCoalesced' ? 1000 : 1),
    fragmentSize,
  }
}
if (mode !== 'bench') {
  let accepted = 0
  let rejected = 0
  let fragmentedPushes = 0
  const invalid = [
    () => Buffer.from([0, 0, 0, 0]),
    () => Buffer.from([1, 0, 0, 0]),
    () => Buffer.from([1, 0, 1, 0]),
    () => Buffer.from([255, 255, 255, 255]),
    () => Buffer.from([2, 0, 0, 0, 123, 125]),
    () => Buffer.from([2, 0, 0, 0, 255, 0]),
    () => packet({ type: 'response', id: 1, ok: true, bytes: -1 }),
    () => packet({ type: 'response', id: 1, ok: true, bytes: 192 * 1024 * 1024 + 1 }),
    () => packet({ type: 'response', id: 0, ok: true }),
    () => packet({ type: 'response', id: 1, ok: true, version: 2 }),
    () => packet({ type: 'response', id: 1, ok: true, unexpected: true }),
    () => packet({ type: 'audio', timestampMs: 1, frames: 1 }, Buffer.alloc(3)),
    () =>
      packet({ type: 'frame', timestampMs: 1, width: 1, height: 1, dropped: 0 }, Buffer.alloc(3)),
    () =>
      packet(
        { type: 'frame', timestampMs: 1, width: 1, height: 2, pixelFormat: 'i420', dropped: 0 },
        Buffer.alloc(3),
      ),
    () => packet({ type: 'navigation', url: 'https://example.test' }, Buffer.alloc(1)),
    () =>
      packet({ type: 'response', id: 1, ok: true }, Buffer.alloc(7)).subarray(0, -1 - (next() % 7)),
  ]
  result.memory.fuzzStart = sampleMemory()
  const fuzzStart = performance.now()
  for (let iteration = 0; iteration < 1_000_000; iteration++) {
    if ((next() & 3) === 0) {
      const wire = invalid[next() % invalid.length]()
      let delivered = false
      const decoder = new NativePacketDecoder(() => {
        delivered = true
      })
      let threw = false
      try {
        const split = next() % (wire.length + 1)
        decoder.push(wire.subarray(0, split))
        decoder.push(wire.subarray(split))
        decoder.end()
      } catch {
        threw = true
      }
      assert(threw && !delivered, `Invalid input accepted at ${iteration}`)
      decoder.dispose()
      rejected++
      result.checks++
    } else {
      const id = (next() % 2_000_000) + 1
      const kind = next() % 8
      let length = next() % 129
      let header
      if (kind === 0)
        header = {
          type: 'response',
          id,
          ok: Boolean(next() & 1),
          error: String.fromCharCode(next() & 65_535),
        }
      else if (kind === 1) {
        length = (1 + (next() % 32)) * 4
        header = { type: 'audio', timestampMs: next(), frames: length / 4 }
      } else if (kind === 2) {
        const width = 1 + (next() % 16)
        length = width * 4
        header = { type: 'frame', timestampMs: next(), width, height: 1, dropped: 0 }
      } else if (kind === 3) {
        length = 0
        header = { type: 'ready', timestampMs: next(), cefVersion: 'seeded-fuzz' }
      } else if (kind === 4) {
        length = 0
        header = { type: 'navigation', url: `https://example.test/${next()}` }
      } else if (kind === 5) {
        const offset = next() % 1024
        header = { type: 'networkbody', bodyId: String(id), offset, totalBytes: offset + length }
      } else if (kind === 6)
        header = {
          type: 'websocketroute',
          scope: 'context',
          contextId: 1,
          pageId: 0,
          targetId: 'worker:seeded',
          registrationId: 'route',
          routingToken: String(id),
          requestId: 'channel',
          stage: 'message',
          url: 'wss://example.test/',
          messageId: String(id),
          outgoing: Boolean(next() & 1),
          opcode: 2,
          callback: { invocation: 'fuzz', token: String(id), scope: String(id) },
        }
      else {
        const offset = next() % 1024
        header = {
          type: 'contextnetworkbody',
          contextId: 1,
          owner: 'broker:1',
          bodyId: String(id),
          offset,
          totalBytes: offset + length,
        }
      }
      const payload = Buffer.alloc(length, next() & 255)
      const wire = packet(header, payload)
      let delivered = 0
      const decoder = new NativePacketDecoder((header, body) => {
        assert.equal(
          header.type,
          [
            'response',
            'audio',
            'frame',
            'ready',
            'navigation',
            'networkbody',
            'websocketroute',
            'contextnetworkbody',
          ][kind],
        )
        assert.equal(header.bytes, length)
        assert(body.equals(payload))
        delivered++
        result.checks += 3
      })
      let cursor = 0
      while (cursor < wire.length) {
        const size = 1 + (next() % 97)
        decoder.push(wire.subarray(cursor, cursor + size))
        cursor += size
        fragmentedPushes++
      }
      decoder.end()
      assert.equal(delivered, 1)
      decoder.dispose()
      accepted++
      result.checks++
    }
    result.cases++
  }
  result.fuzz = { accepted, rejected, fragmentedPushes, elapsedMs: performance.now() - fuzzStart }
  result.memory.fuzzEnd = sampleMemory()
  const memory = []
  const decoder = new NativePacketDecoder((_header, payload) => {
    assert(payload.equals(binary))
  })
  const soakStart = performance.now()
  for (let round = 0; round < 21; round++) {
    for (let repetition = 0; repetition < 128; repetition++) {
      for (let offset = 0; offset < big.length; offset += 65_536)
        decoder.push(big.subarray(offset, offset + 65_536))
    }
    decoder.end()
    memory.push(sampleMemory())
  }
  decoder.dispose()
  result.soak = {
    packets: 21 * 128,
    payloadBytes: 21 * 128 * binary.length,
    elapsedMs: performance.now() - soakStart,
    memory,
  }
  // Exclude allocator warmup: live ArrayBuffer storage must plateau across 5 GiB of traffic.
  const retained = memory.slice(3).map((sample) => sample.arrayBuffers)
  assert(
    Math.max(...retained) - Math.min(...retained) <= 8 * 1024 * 1024,
    'Retained ArrayBuffers grew across steady-state soak',
  )
  const heap = memory.slice(3).map((sample) => sample.heapUsed)
  assert(
    Math.max(...heap) - Math.min(...heap) <= 4 * 1024 * 1024,
    'Retained JS heap grew across steady-state soak',
  )
  result.memory.afterDispose = sampleMemory()
}
result.elapsedMs = performance.now() - started
await writeFile(outputPath, `${JSON.stringify(result, null, 2)}\n`)
console.log(
  JSON.stringify({
    outputPath,
    cases: result.cases,
    checks: result.checks,
    elapsedMs: result.elapsedMs,
    benchmark: result.benchmark,
    fuzz: result.fuzz,
    soak: result.soak && { packets: result.soak.packets, payloadBytes: result.soak.payloadBytes },
  }),
)
