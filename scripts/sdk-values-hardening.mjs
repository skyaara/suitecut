// SPDX-License-Identifier: MIT
/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Standalone fuzz harness loads arbitrary compiled revisions and deliberately constructs malformed inputs. */
import assert from 'node:assert/strict'
import console from 'node:console'
// Deterministic, standalone SDK boundary fuzzing. Run after compiling the three
// selected modules with `node --expose-gc --max-old-space-size=512`.
import { createHash } from 'node:crypto'
import { readFile, writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'

const directory = process.argv[2]
assert(directory, 'Supply a compiled module directory')
const count = Number(process.argv[3] ?? 120000)
assert(Number.isInteger(count) && count >= 100000 && count <= 1000000)
assert.equal(typeof globalThis.gc, 'function', 'Run with --expose-gc')
const load = (name) => import(pathToFileURL(`${directory}/${name}.js`).href)
const { createNativeValueCodec } = await load('native-values')
const { nativeURLGlobMatcher, resolveNativeURLGlob } = await load('native-url-glob')
const { encodeNativeStringFields } = await load('native-string-wire')
const codec = createNativeValueCodec()
let seed = 0x5c17c0de
const random = () => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0)
const text = () =>
  String.fromCharCode(...Array.from({ length: 1 + (random() % 24) }, () => random() & 65535))
const views = [
  Int8Array,
  Uint8Array,
  Uint8ClampedArray,
  Int16Array,
  Uint16Array,
  Int32Array,
  Uint32Array,
  Float32Array,
  Float64Array,
  BigInt64Array,
  BigUint64Array,
  DataView,
]
const memory = () => {
  globalThis.gc()
  globalThis.gc()
  const { heapUsed, external, arrayBuffers, rss } = process.memoryUsage()
  return { heapUsed, external, arrayBuffers, rss }
}
const marks = [memory()]
let graphMilliseconds = 0
let stringMilliseconds = 0
let malformedRejected = 0
for (let batch = 0; batch < 12; batch++) {
  const start = performance.now()
  const begin = Math.floor((count * batch) / 12)
  const end = Math.floor((count * (batch + 1)) / 12)
  for (let i = begin; i < end; i++) {
    const word = text()
    const shared = { [word]: word, number: random(), missing: undefined }
    const bytes = new Uint8Array(64)
    for (let j = 0; j < bytes.length; j++) bytes[j] = random() & 255
    const View = views[i % views.length]
    const typed = new View(bytes.buffer, 8, View === DataView ? 24 : 24 / View.BYTES_PER_ELEMENT)
    const error = new Error(word)
    error.name = word
    error.stack = word
    const value = {
      shared,
      again: shared,
      self: null,
      list: [NaN, -0, Infinity, -Infinity, undefined, true],
      typed,
      big: BigInt(random()) << 96n,
      date: new Date(random()),
      pattern: new RegExp(word.replace(/[.*+?^${}()|[\]\\]/gu, '\\$&'), 'u'),
      error,
    }
    value.self = value
    const decoded = codec.decode(JSON.parse(JSON.stringify(codec.encode(value, 'argument'))))
    assert.equal(decoded.self, decoded)
    assert.equal(decoded.shared, decoded.again)
    assert.deepEqual(decoded, value)
    const handle = { index: i }
    const encoded = codec.encode([handle, { handle }], 'argument', (item) =>
      item === handle ? 0 : undefined,
    )
    const result = codec.decode(encoded, [shared])
    assert.equal(result[0], shared)
    assert.equal(result[1].handle, shared)
    const bad = [
      ['utf16', `${(random() & 65535).toString(16).padStart(4, '0')}X`],
      ['bytes', 'Uint8Array', [256 + random()]],
      ['reference', 1 + random()],
      ['handle', 1 + random()],
      ['object', 0, []],
      ['object', 1, [['a', ['object', 1, []]]]],
    ][i % 6]
    assert.throws(() => codec.decode(bad), /Invalid native/)
    malformedRejected++
  }
  graphMilliseconds += performance.now() - start
  const stringStart = performance.now()
  for (let i = begin; i < end; i++) {
    const word = `x\ud800x${text()}x\udfffx`
    const args = { selector: word, self: null }
    args.self = args
    const input = {
      selector: word,
      query: [{ engine: 'filter', has: [{ engine: 'text', text: word }], name: { text: word } }],
      element: { selector: word },
      masks: [{ selector: word }],
      args,
    }
    const wire = encodeNativeStringFields('query', input)
    assert.equal(JSON.parse(wire.selectorJSON), word)
    assert.equal(JSON.parse(wire.query[0].has[0].textJSON), word)
    assert.equal(wire.args, args)
    assert.equal(input.selector, word)
    assert.throws(
      () => encodeNativeStringFields('query', { selector: word, selectorJSON: '"bad"' }),
      /Ambiguous/,
    )
  }
  stringMilliseconds += performance.now() - stringStart
  marks.push(memory())
}
// Mutate real tags and field positions rather than only repeating fixed invalid
// examples. Accepted mutations must still round-trip as a stable wire value.
const mutationStart = performance.now()
let mutatedAccepted = 0
let mutatedRejected = 0
const tags = [
  'null',
  'undefined',
  'primitive',
  'utf16',
  'bytes',
  'handle',
  'reference',
  'object',
  'array',
  'number',
  'bigint',
  'date',
  'regexp',
  'url',
  'error',
  'unknown',
]
const atoms = [null, false, true, 0, -1, 1.5, '', 'NaN', '0010', 'd800', '__proto__', [], {}]
for (let i = 0; i < count * 4; i++) {
  const candidate = [
    tags[(random() >>> 8) % tags.length],
    atoms[(random() >>> 8) % atoms.length],
    atoms[(random() >>> 8) % atoms.length],
    atoms[(random() >>> 8) % atoms.length],
  ]
  if (random() & 1) candidate.length = random() % 5
  let decoded
  try {
    decoded = codec.decode(candidate)
  } catch (error) {
    assert(error instanceof Error)
    mutatedRejected++
    continue
  }
  if (!(decoded instanceof Date && Number.isNaN(decoded.getTime())))
    assert.deepEqual(codec.decode(codec.encode(decoded)), decoded)
  mutatedAccepted++
}
const mutationMilliseconds = performance.now() - mutationStart
// Independent regular-expression oracle is used only for small random patterns;
// production uses bounded DP rather than a backtracking regular expression.
const oracle = (pattern) => {
  let source = '^'
  for (let i = 0; i < pattern.length; i++) {
    let c = pattern[i]
    if (c === '\\' && i + 1 < pattern.length) c = pattern[++i]
    else if (c === '*') {
      const deep = pattern[i + 1] === '*'
      while (pattern[i + 1] === '*') i++
      source += deep ? '[\\s\\S]*' : '[^/]*'
      continue
    }
    source += c.replace(/[.*+?^${}()|[\]\\]/gu, '\\$&')
  }
  // The native matcher operates on UTF-16 code units, including lone surrogates.
  return new RegExp(`${source}$`)
}
const globStart = performance.now()
for (let i = 0; i < count; i++) {
  const alphabet = 'abc/*\\?.\ud800\udfff'
  let pattern = '',
    url = ''
  for (let j = 0, n = random() % 24; j < n; j++) pattern += alphabet[random() % alphabet.length]
  for (let j = 0, n = random() % 32; j < n; j++) url += alphabet[random() % alphabet.length]
  let positive = ''
  for (let j = 0; j < pattern.length; j++) {
    if (pattern[j] === '\\' && j + 1 < pattern.length) positive += pattern[++j]
    else if (pattern[j] === '*') {
      const deep = pattern[j + 1] === '*'
      while (pattern[j + 1] === '*') j++
      positive += deep ? 'a/b' : 'ab'
    } else positive += pattern[j]
  }
  assert.equal(oracle(pattern).test(positive), true)
  assert.equal(
    nativeURLGlobMatcher(pattern)(positive),
    true,
    `positive seed=${seed} pattern=${JSON.stringify(pattern)}`,
  )
  assert.equal(
    nativeURLGlobMatcher(pattern)(url),
    oracle(pattern).test(url),
    `seed=${seed} pattern=${JSON.stringify(pattern)} url=${JSON.stringify(url)}`,
  )
}
const globMilliseconds = performance.now() - globStart
// Large, bounded operations exercise allocation and explicit overflow failures.
const adversarialStart = performance.now()
for (let i = 0; i < 8; i++) {
  const word = `${'x'.repeat(256 * 1024)}\ud800`
  assert.equal(codec.decode(codec.encode(word)), word)
  assert.equal(codec.decode(['utf16', '0061'.repeat(256 * 1024)]).length, 256 * 1024)
  assert.deepEqual(codec.decode(codec.encode(new Uint8Array(100000))), new Uint8Array(100000))
}
let deep = {}
for (let i = 0; i < 103; i++) deep = { deep }
assert.throws(() => codec.encode(deep), /reference chain/)
assert.throws(() => codec.decode(['bytes', 'Uint8Array', Array(100001).fill(0)]), /Invalid native/)
assert.throws(() => codec.encode(Array(100001).fill(0)), /node limit/)
assert.throws(() => encodeNativeStringFields('query', { query: Array(100001).fill(null) }), /limit/)
assert.throws(() => nativeURLGlobMatcher('{a,b}'.repeat(10)), /capacity/)
assert.throws(() => nativeURLGlobMatcher('*a'.repeat(3000))('a'.repeat(8192)), /work capacity/)
assert.equal(
  resolveNativeURLGlob('./*.{html,svg}', 'https://a.test/root/'),
  'https://a.test/root/*.{html,svg}',
)
const adversarialMilliseconds = performance.now() - adversarialStart
const benchmark = (name, action, iterations) => {
  for (let i = 0; i < Math.min(iterations, 1000); i++) action()
  const start = performance.now()
  for (let i = 0; i < iterations; i++) action()
  return { name, iterations, milliseconds: performance.now() - start }
}
const performanceCases = []
for (const pattern of [
  'https://example.test/assets/*.js',
  '**/*.js',
  'https://example.test/{assets,scripts}/*.js',
]) {
  let total = 0
  for (let batch = 0; batch < 40; batch++) {
    const match = nativeURLGlobMatcher(pattern)
    total += benchmark(
      pattern,
      () => assert(match('https://example.test/assets/component.js')),
      1000,
    ).milliseconds
  }
  performanceCases.push({ name: pattern, iterations: 40000, milliseconds: total })
}
const finalMemory = memory()
const plateau = marks.slice(4)
const spread = (field) =>
  Math.max(...plateau.map((m) => m[field])) - Math.min(...plateau.map((m) => m[field]))
assert(spread('heapUsed') < 12 * 1024 * 1024, 'Post-GC value/string heap does not plateau')
assert(spread('arrayBuffers') < 4 * 1024 * 1024, 'Post-GC typed buffers do not plateau')
const report = {
  mutationCases: count * 4,
  mutatedAccepted,
  mutatedRejected,
  mutationMilliseconds,
  seed: '0x5c17c0de',
  richGraphCases: count,
  typedViewKinds: views.length,
  handleCases: count,
  malformedRejected,
  selectedStringWireCases: count,
  globOracleCases: count * 2,
  graphMilliseconds,
  stringMilliseconds,
  globMilliseconds,
  adversarialMilliseconds,
  performanceCases,
  postGCBatches: marks,
  plateauHeapSpread: spread('heapUsed'),
  plateauArrayBufferSpread: spread('arrayBuffers'),
  finalMemory,
  sha256: {},
}
for (const name of ['native-values', 'native-string-wire', 'native-url-glob'])
  report.sha256[name] = createHash('sha256')
    .update(await readFile(`${directory}/${name}.js`))
    .digest('hex')
if (process.argv[4]) await writeFile(process.argv[4], JSON.stringify(report, null, 2) + '\n')
console.log(JSON.stringify(report, null, 2))
