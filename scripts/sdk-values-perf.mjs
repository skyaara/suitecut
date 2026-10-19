// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return -- Standalone fuzz harness loads arbitrary compiled revisions and deliberately constructs malformed inputs. */
import assert from 'node:assert/strict'
import console from 'node:console'
import { writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

const modules = await Promise.all(
  process.argv.slice(2, 4).map(async (directory) => ({
    codec: (
      await import(pathToFileURL(`${directory}/native-values.js`).href)
    ).createNativeValueCodec(),
    factory: (await import(pathToFileURL(`${directory}/native-values.js`).href))
      .createNativeValueCodec,
    glob: (await import(pathToFileURL(`${directory}/native-url-glob.js`).href))
      .nativeURLGlobMatcher,
  })),
)
assert.equal(modules.length, 2)
const text = `${'a'.repeat(64 * 1024)}\ud800`
const hex = modules[0].codec.encode(text)
const definitions = [
  {
    name: 'fresh-codec-plain-small',
    iterations: 200000,
    make: (m) => () => m.factory().encode({ value: 'ordinary string' }),
  },
  {
    name: 'fresh-codec-utf16-small',
    iterations: 200000,
    make: (m) => () => m.factory().encode('x\ud800x'),
  },
  { name: 'utf16-encode-64k', iterations: 48, make: (m) => () => m.codec.encode(text) },
  { name: 'utf16-decode-64k', iterations: 48, make: (m) => () => m.codec.decode(hex) },
  {
    name: 'glob-origin-prefix-extension-suffix',
    iterations: 10000,
    make: (m) => {
      const f = m.glob('https://example.test/assets/*.js')
      return () => f('https://example.test/assets/component.js')
    },
  },
  {
    name: 'glob-deep-star-extension-suffix',
    iterations: 10000,
    make: (m) => {
      const f = m.glob('**/*.js')
      return () => f('https://example.test/assets/component.js')
    },
  },
  {
    name: 'glob-alternative-origin-path',
    iterations: 10000,
    make: (m) => {
      const f = m.glob('https://example.test/{assets,scripts}/*.js')
      return () => f('https://example.test/assets/component.js')
    },
  },
]
const reports = []
for (const definition of definitions) {
  const samples = [[], []]
  for (let round = 0; round < 7; round++)
    for (const index of round % 2 ? [1, 0] : [0, 1]) {
      const run = definition.make(modules[index])
      for (let i = 0; i < Math.min(500, definition.iterations); i++) run()
      globalThis.gc?.()
      const start = performance.now()
      for (let i = 0; i < definition.iterations; i++) run()
      samples[index].push(performance.now() - start)
    }
  const median = samples.map((s) => s.slice().sort((a, b) => a - b)[3])
  reports.push({
    name: definition.name,
    iterationsPerSample: definition.iterations,
    rounds: 7,
    baselineMilliseconds: samples[0],
    candidateMilliseconds: samples[1],
    baselineMedian: median[0],
    candidateMedian: median[1],
    speedup: median[0] / median[1],
  })
}
if (process.argv[4]) await writeFile(process.argv[4], JSON.stringify(reports, null, 2) + '\n')
console.log(JSON.stringify(reports, null, 2))
