import assert from 'node:assert/strict'
import { Blob, File } from 'node:buffer'
import console from 'node:console'
import { readFile } from 'node:fs/promises'
import { runInThisContext } from 'node:vm'

import * as z from 'zod'

const header = await readFile(new URL('../native/browser/storage_codec.h', import.meta.url), 'utf8')
const code = header.split('R"JS(')[1]?.split(')JS"')[0]
assert(code)
const codec = z
  .object({
    encode: z.function({ input: [z.unknown()], output: z.promise(z.unknown()) }),
    decode: z.function({ input: [z.unknown()], output: z.unknown() }),
  })
  .parse(runInThisContext(`(${code})()`))
const ownProto = {}
Object.defineProperty(ownProto, '__proto__', { value: { safe: true }, enumerable: true })
const sparse = new Array(8)
sparse[4] = 'native'
Reflect.set(sparse, 'extra', 'array-property')
const buffer = new Uint8Array([0, 255, 17, 92]).buffer
const map = new Map()
const input = {
  date: new Date('2026-01-02T03:04:05Z'),
  invalidDate: new Date(NaN),
  regexp: /storage/gi,
  bigint: 928374982734987239847n,
  nan: NaN,
  negativeZero: -0,
  infinity: Infinity,
  missing: undefined,
  buffer,
  view: new Uint8Array(buffer, 1, 2),
  dataView: new DataView(buffer, 0, 3),
  map,
  set: new Set(['x', 42]),
  blob: new Blob(['native blob'], { type: 'text/plain' }),
  file: new File(['native file'], 'auth.txt', { type: 'text/plain', lastModified: 123 }),
  error: new Error('error', { cause: map }),
  exception: new DOMException('message', 'AbortError'),
  ownProto,
  sparse,
}
map.set(input, input)
Reflect.set(input, 'self', input)
const output = codec.decode(await codec.encode(input))
assert(output && typeof output === 'object')
assert.equal(Reflect.get(output, 'self'), output)
const revivedSparse = z.unknown().parse(Reflect.get(output, 'sparse'))
assert(Array.isArray(revivedSparse))
assert.equal(revivedSparse.length, 8)
assert(!Object.hasOwn(revivedSparse, 0))
assert.equal(revivedSparse[4], 'native')
assert.equal(Reflect.get(revivedSparse, 'extra'), 'array-property')
const revivedBuffer = z.unknown().parse(Reflect.get(output, 'buffer'))
const revivedView = z.unknown().parse(Reflect.get(output, 'view'))
const revivedDataView = z.unknown().parse(Reflect.get(output, 'dataView'))
assert(revivedView instanceof Uint8Array && revivedDataView instanceof DataView)
assert.equal(revivedView.buffer, revivedBuffer)
assert.equal(revivedDataView.buffer, revivedBuffer)
const revivedMap = z.unknown().parse(Reflect.get(output, 'map'))
assert(revivedMap instanceof Map)
assert.equal(revivedMap.get(output), output)
const blob = z.unknown().parse(Reflect.get(output, 'blob'))
const file = z.unknown().parse(Reflect.get(output, 'file'))
assert(blob instanceof Blob && file instanceof File)
assert.equal(await blob.text(), 'native blob')
assert.equal(await file.text(), 'native file')
assert.equal(file.name, 'auth.txt')
assert.equal(file.lastModified, 123)
assert(Object.is(Reflect.get(output, 'negativeZero'), -0))
const date = z.unknown().parse(Reflect.get(output, 'date'))
assert(date instanceof Date)
assert.equal(date.toISOString(), '2026-01-02T03:04:05.000Z')
const invalidDate = z.unknown().parse(Reflect.get(output, 'invalidDate'))
assert(invalidDate instanceof Date && Number.isNaN(invalidDate.getTime()))
const error = z.unknown().parse(Reflect.get(output, 'error'))
assert(error instanceof Error && error.cause === revivedMap)
const revivedOwnProto = z.unknown().parse(Reflect.get(output, 'ownProto'))
assert(revivedOwnProto && typeof revivedOwnProto === 'object')
assert(Object.hasOwn(revivedOwnProto, '__proto__'))
assert.equal(Object.getPrototypeOf(revivedOwnProto), Object.prototype)
assert.throws(() => codec.decode({ version: 1, root: [4, 2], nodes: [] }), /Invalid/)
await assert.rejects(codec.encode(new Blob([new Uint8Array(1048577)])), /capacity/)
await assert.rejects(codec.encode('x'.repeat(524289)), /capacity/)
let blobReads = 0
class ObservedBlob extends Blob {
  async arrayBuffer() {
    blobReads++
    return super.arrayBuffer()
  }
}
await assert.rejects(
  codec.encode([
    new ObservedBlob([new Uint8Array(220000)]),
    new ObservedBlob([new Uint8Array(220000)]),
  ]),
  /capacity/,
)
assert.equal(blobReads, 1)
assert.throws(
  () =>
    codec.decode({
      version: 1,
      root: [4, 0],
      nodes: [
        {
          type: 'Array',
          values: [
            [4, 1],
            [4, 2],
          ],
        },
        { type: 'ArrayBuffer', data: 'A'.repeat(300000) },
        { type: 'ArrayBuffer', data: 'A'.repeat(300000) },
      ],
    }),
  /capacity/,
)
for (let index = 0; index < 10000; index++) {
  const shared = { index }
  const value = { a: shared, b: shared, array: [shared], set: new Set([shared]) }
  const decoded = codec.decode(await codec.encode(value))
  assert(decoded && typeof decoded === 'object')
  assert.equal(Reflect.get(decoded, 'a'), Reflect.get(decoded, 'b'))
}
console.log(
  'Native storage codec: platform types, shared/cyclic graphs, byte limits and 10000 ownership cycles passed',
)
