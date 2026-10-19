/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-argument -- Controlled fuzz/process-model harness selects emitted modules and deliberately malformed protocol values; production SDK validation is tested separately. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { writeFile } from 'node:fs/promises'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

const [baselinePath, candidatePath, outputPath] = process.argv.slice(2)
if (!baselinePath || !candidatePath || !outputPath || !globalThis.gc)
  throw new Error('Use node --expose-gc <script> <baseline.js> <candidate.js> <result.json>')
function packet(id, bytes = 0) {
  const json = Buffer.from(JSON.stringify({ version: 1, bytes, type: 'response', id, ok: true }))
  const length = Buffer.alloc(4)
  length.writeUInt32LE(json.length)
  return Buffer.concat([length, json])
}
function memory() {
  globalThis.gc()
  globalThis.gc()
  const { heapUsed, external, arrayBuffers, rss } = process.memoryUsage()
  return { heapUsed, external, arrayBuffers, rss }
}
const results = {}
for (const [label, path] of [
  ['baseline', baselinePath],
  ['candidate', candidatePath],
]) {
  const { NativePacketDecoder, MAX_NATIVE_RESPONSE_PAYLOAD } = await import(
    pathToFileURL(path).href
  )
  const received = []
  const decoder = new NativePacketDecoder((header) => {
    received.push(header.id)
    if (header.id === 1) decoder.push(packet(3))
  })
  let error
  try {
    decoder.push(Buffer.concat([packet(1), packet(2)]))
    decoder.end()
  } catch (failure) {
    error = { name: failure.name, message: failure.message }
  }
  decoder.dispose()
  const before = memory()
  const partial = new NativePacketDecoder(() => {
    throw new Error('Unexpected complete packet')
  })
  partial.push(Buffer.concat([packet(1, MAX_NATIVE_RESPONSE_PAYLOAD), Buffer.from([1])]))
  try {
    partial.end()
  } catch {
    /* Expected truncated-packet rejection is measured below. */
  }
  const afterTruncatedEnd = memory()
  try {
    partial.end()
  } catch {
    /* Repeated end must not retain the partial body either. */
  }
  partial.dispose()
  const afterDispose = memory()
  results[label] = {
    received,
    error,
    maxResponseBytes: MAX_NATIVE_RESPONSE_PAYLOAD,
    before,
    afterTruncatedEnd,
    afterDispose,
  }
}
assert(results.baseline.error, 'Baseline should reproduce the synchronous-receiver bug')
assert.deepEqual(results.candidate.received, [1, 2, 3])
assert.equal(results.candidate.error, undefined)
const retained = (item) => item.afterTruncatedEnd.arrayBuffers - item.before.arrayBuffers
assert(
  retained(results.baseline) >= results.baseline.maxResponseBytes,
  'Baseline must retain its incomplete response',
)
assert(
  retained(results.candidate) < 65_536,
  'Failed candidate parser retained its incomplete response',
)
await writeFile(outputPath, `${JSON.stringify(results, null, 2)}\n`)
console.log(JSON.stringify(results))
