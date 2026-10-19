import { describe, expect, it } from 'vitest'

import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

describe('native browser value boundary', () => {
  it('preserves isolated UTF-16 units and metadata through ASCII-safe JSON', () => {
    const codec = createNativeValueCodec()
    const text = `a\ud800b\udfff\ud83d\ude80`
    const error = new Error(text)
    error.name = text
    error.stack = text
    const value = { [text]: text, pattern: new RegExp(text), error }
    const serialized = JSON.stringify(codec.encode(value, 'argument'))
    expect(serialized).not.toMatch(/\\u[dD][89aAbBcCdDeEfF][0-9a-fA-F]{2}/u)
    const decoded = codec.decode(JSON.parse(serialized) as UntrustedInput)
    expect(decoded).toEqual(value)
    const metadata = decoded as { pattern: RegExp; error: Error }
    expect(metadata.pattern.source).toBe(value.pattern.source)
    expect(metadata.error.name).toBe(text)
    expect(metadata.error.message).toBe(text)
    expect(metadata.error.stack).toBe(text)
    expect(codec.encode('plain \ud83d\ude80')).toEqual(['primitive', 'plain \ud83d\ude80'])
  })

  it('rejects malformed UTF-16 units and retains legacy error metadata', () => {
    const codec = createNativeValueCodec()
    for (const value of [
      ['utf16', 'd80'],
      ['utf16', 'zzzz'],
      ['utf16', 0],
      ['utf16', 'd800', 'extra'],
    ])
      expect(() => codec.decode(value)).toThrow('Invalid native serialized value')
    const error = codec.decode(['error', 'Error', 'actual native rejection', ''])
    expect(error).toBeInstanceOf(Error)
    if (!(error instanceof Error)) throw new Error('Decoded native error is missing')
    expect(error.name).toBe('Error')
    expect(error.message).toBe('actual native rejection')
    expect(error.stack).toBe('')
  })
  it('preserves special numbers, undefined fields and typed bytes across JSON transport', () => {
    const codec = createNativeValueCodec()
    const value = {
      nan: NaN,
      negativeZero: -0,
      infinity: Infinity,
      none: undefined,
      large: 12345678901234567890n,
      bytes: new Float64Array([1.25, -3.5]),
      date: new Date('2026-10-13T00:00:00Z'),
      pattern: /hello/im,
    }
    const result = codec.decode(
      JSON.parse(JSON.stringify(codec.encode(value, 'argument'))) as UntrustedInput,
    )
    expect(result).toEqual(value)
    expect(Object.hasOwn(result as object, 'none')).toBe(true)
  })

  it('preserves circular and shared references without invoking user toJSON', () => {
    const codec = createNativeValueCodec()
    const item = {
      name: 'shared',
      toJSON: () => {
        throw new Error('toJSON must not run')
      },
    }
    const value: { item: typeof item; again: typeof item; self?: object } = { item, again: item }
    value.self = value
    const encoded = JSON.parse(JSON.stringify(codec.encode(value))) as UntrustedInput
    const result = codec.decode(encoded) as { item: object; again: object; self: object }
    expect(result.item).toBe(result.again)
    expect(result.self).toBe(result)
    expect(result.item).toEqual({ name: 'shared', toJSON: {} })
    const spoof = { [Symbol.toStringTag]: 'Performance', ...item }
    expect(codec.decode(codec.encode(spoof))).toEqual({ name: 'shared', toJSON: {} })
  })

  it('rejects malicious references, invalid typed-array names and deeply nested data', () => {
    const codec = createNativeValueCodec()
    expect(() => codec.decode(['reference', 7])).toThrow('Invalid native')
    expect(() => codec.decode(['bytes', 'constructor', [1]])).toThrow('Invalid native')
    expect(() => codec.decode(['bytes', 'Uint8Array', [256]])).toThrow('Invalid native')
    const deep: Record<string, object> = {}
    let cursor = deep
    for (let i = 0; i < 600; i++) {
      const child: Record<string, object> = {}
      cursor.child = child
      cursor = child
    }
    expect(() => codec.encode(deep)).toThrow('reference chain is too long')
    expect(() => codec.encode({ callback: () => 1 }, 'argument')).toThrow('unexpected value')
  })

  it('creates dangerous property names as own data without prototype pollution', () => {
    const result = createNativeValueCodec().decode([
      'object',
      1,
      [['__proto__', ['object', 2, [['polluted', ['primitive', true]]]]]],
    ]) as object
    expect(Object.getPrototypeOf(result)).toBe(Object.prototype)
    expect(Object.hasOwn(result, '__proto__')).toBe(true)
    expect(Object.hasOwn(Object.prototype, 'polluted')).toBe(false)
  })

  it('transfers nested handle references only through an explicit handle table', () => {
    const codec = createNativeValueCodec()
    const handle = { remote: true }
    const value: { nested: object[]; repeated: object; self?: object } = {
      nested: [handle],
      repeated: handle,
    }
    value.self = value
    const encoded = codec.encode(value, 'argument', (item) => (item === handle ? 0 : undefined))
    const remote = { browserObject: true }
    const decoded = codec.decode(encoded, [remote]) as typeof value
    expect(decoded.nested[0]).toBe(remote)
    expect(decoded.repeated).toBe(remote)
    expect(decoded.self).toBe(decoded)
    expect(() => codec.decode(encoded)).toThrow('Invalid native')
    for (const index of [-1, 0.5, Infinity, '0', 1])
      expect(() => codec.decode(['handle', index], [remote])).toThrow('Invalid native')
  })

  it('reports the exact path to rejected argument functions', () => {
    const callback = () => 42
    expect(() => createNativeValueCodec().encode({ rows: [{ callback }] }, 'argument')).toThrow(
      `unexpected value at position "rows[0].callback": ${String(callback)}`,
    )
  })
})
