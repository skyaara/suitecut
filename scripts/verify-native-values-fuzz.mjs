// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

import * as z from 'zod'

const codecURL = process.env.SUITECUT_VALUE_FUZZ_MODULE
  ? pathToFileURL(resolve(process.env.SUITECUT_VALUE_FUZZ_MODULE))
  : new URL('../dist/native-values.js', import.meta.url)
// eslint-disable-next-line @typescript-eslint/no-unsafe-assignment -- Explicitly selected emitted codec is exercised by the property assertions below.
const { createNativeValueCodec } = /** @type {typeof import('../dist/native-values.js')} */ (
  await import(codecURL.href)
)
const directory = resolve(process.env.SUITECUT_VALUE_FUZZ_OUTPUT ?? '.suitecut/value-fuzz')

const collect = globalThis.gc
assert(typeof collect === 'function', 'Run with node --expose-gc')
const iterations = z.coerce
  .number()
  .int()
  .min(1)
  .max(10000000)
  .parse(process.argv[2] ?? 100000)
const codec = createNativeValueCodec()
const codecBytes = await readFile(codecURL)
const codecSHA256 = createHash('sha256').update(codecBytes).digest('hex')
let seed = 0x5c17c0de
const random = () => {
  seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0
  return seed
}
const resultSchema = z.object({
  shared: z.object({ value: z.number() }),
  again: z.object({ value: z.number() }),
  special: z.array(z.union([z.number(), z.nan(), z.literal(Infinity), z.literal(-Infinity)])),
  bytes: z.instanceof(Uint32Array),
  date: z.date(),
  big: z.bigint(),
  self: z.object(),
  text: z.string(),
  textKeys: z.record(z.string(), z.string()),
  error: z.instanceof(Error),
  pattern: z.instanceof(RegExp),
})
collect()
const before = process.memoryUsage().heapUsed
let malformedRejected = 0
for (let index = 0; index < iterations; index++) {
  const number = random()
  const shared = { value: number, missing: undefined }
  const text = String.fromCharCode(
    random() & 0xffff,
    0xd800 | (random() & 0x3ff),
    random() & 0xffff,
    0xdc00 | (random() & 0x3ff),
  )
  const error = new Error(text)
  error.name = text
  error.stack = text
  const value = {
    shared,
    again: shared,
    special: [NaN, -0, Infinity, -Infinity],
    bytes: new Uint32Array([number]),
    date: new Date(number),
    big: BigInt(number),
    self: {},
    text,
    textKeys: { [text]: text },
    error,
    pattern: new RegExp(`\ud800${number}\udfff`),
  }
  value.self = value
  const decoded = codec.decode(
    z.json().parse(JSON.parse(JSON.stringify(codec.encode(value, 'argument')))),
  )
  const result = resultSchema.parse(decoded)
  // Check graph identity before schema validation creates domain copies.
  assert(
    decoded &&
      typeof decoded === 'object' &&
      'self' in decoded &&
      'shared' in decoded &&
      'again' in decoded,
  )
  assert.equal(decoded.self, decoded)
  assert.equal(decoded.shared, decoded.again)
  assert.equal(result.shared.value, number)
  assert(Object.is(result.special[1], -0))
  assert.deepEqual(result.bytes, value.bytes)
  assert.equal(result.big, value.big)
  assert.equal(result.text, text)
  assert.equal(result.textKeys[text], text)
  assert.equal(result.error.name, text)
  assert.equal(result.error.message, text)
  assert.equal(result.error.stack, text)
  assert.equal(result.pattern.source, value.pattern.source)
  assert.throws(() => codec.decode(['utf16', 'd800ZZZZ']), /Invalid native/u)
  assert.throws(() => codec.decode(['reference', 1 + random()]), /Invalid native/)
  const handle = { native: number }
  const handles = codec.encode({ handle, repeated: [handle] }, 'argument', (item) =>
    item === handle ? 0 : undefined,
  )
  const remote = { live: number }
  const resolved = z
    .object({
      handle: z.object({ live: z.number() }),
      repeated: z.array(z.object({ live: z.number() })),
    })
    .parse(codec.decode(handles, [remote]))
  assert.equal(resolved.handle.live, number)
  assert.equal(resolved.repeated[0].live, number)
  assert.throws(() => codec.decode(['handle', 1 + random()], [remote]), /Invalid native/)
  malformedRejected++
}
await setImmediate()
collect()
const heapDelta = process.memoryUsage().heapUsed - before
assert(heapDelta < 8 * 1024 * 1024, 'Value codec state retained after collection')
const report = {
  codecSHA256,
  iterations,
  seed: '0x5c17c0de',
  malformedRejected,
  heapDelta,
  handleReferenceCases: iterations,
  invalidHandleReferencesRejected: iterations,
  exactUTF16StringAndMetadataCases: iterations,
  scope:
    'typed/circular browser value encoding, nested handle tables, exact UTF16 units/keys/Error/RegExp metadata, malformed reference and UTF16 decoding',
  note: 'Deterministic property fuzzing, not whole-engine leak freedom',
}
await mkdir(directory, { recursive: true })
assert.equal(
  createHash('sha256')
    .update(await readFile(codecURL))
    .digest('hex'),
  codecSHA256,
  'Codec bytes changed during proof',
)
await writeFile(resolve(directory, 'current-code-native-values.js'), codecBytes)
await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
console.log(JSON.stringify(report, null, 2))
