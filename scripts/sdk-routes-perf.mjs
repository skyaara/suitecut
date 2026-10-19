// Run with --expose-gc and an emitted SDK directory argument.
import assert from 'node:assert/strict'
import console from 'node:console'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Deliberate emitted-module boundary for before/after comparison.
const { NativeRouteURLMatcherPolicy } =
  /** @type {typeof import('../src/native-route-url-matcher.js')} */ (
    await import(pathToFileURL(resolve(process.argv[2], 'native-route-url-matcher.js')))
  )
const policy = new NativeRouteURLMatcherPolicy(/^https:\/\/native\.test\/(wire|other)\/[0-9]+$/gy)
const count = 1_000_000
const urls = Array.from(
  { length: 256 },
  (_, i) => `https://native.test/${i % 3 ? 'wire' : 'missing'}/${i}`,
)
for (let i = 0; i < 100_000; ++i) policy.callback()(urls[i & 255])
const samples = []
for (let repeat = 0; repeat < 7; ++repeat) {
  globalThis.gc?.()
  let hits = 0
  const start = performance.now()
  for (let i = 0; i < count; ++i) hits += Number(policy.callback()(urls[i & 255]).matched)
  samples.push(performance.now() - start)
  assert.equal(hits, 664062)
}
policy.dispose()
console.log(
  JSON.stringify({
    count,
    samplesMs: samples,
    medianMs: [...samples].sort((a, b) => a - b)[3],
    node: process.version,
  }),
)
