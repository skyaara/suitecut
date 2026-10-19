// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { nativeFixtureLiteralWire } from '../src/native-fixture-values.js'

describe('raw fixture literal encoding (no native selection or callback receipt)', () => {
  it('distinguishes absent/native undefined, null, false, zero, negative zero and empty string', () => {
    expect(nativeFixtureLiteralWire(undefined)).toEqual({ kind: 'undefined' })
    for (const [value, json] of [
      [null, 'null'],
      [false, 'false'],
      [0, '0'],
      [-0, '-0'],
      ['', '""'],
    ] as const)
      expect(nativeFixtureLiteralWire(value)).toEqual({ kind: 'json', json })
  })
  it('retains exact escaped UTF16 and nested data including negative zero', () => {
    const encoded = nativeFixtureLiteralWire({
      lone: '\ud800\0\udfff',
      nested: [true, -0, { x: 'π😀' }],
    })
    expect(encoded.kind).toBe('json')
    if (encoded.kind !== 'json') throw new Error('Expected raw JSON encoding')
    expect(encoded.json).toContain('"nested":[true,-0,')
    const decoded = JSON.parse(encoded.json) as {
      lone: string
      nested: (boolean | number | object)[]
    }
    expect(decoded.lone).toBe('\ud800\0\udfff')
    expect(Object.is(decoded.nested[1], -0)).toBe(true)
  })
  it('snapshots repeated references without claiming authored object identity or mutating them', () => {
    const shared = { count: 1 },
      input = { first: shared, second: shared }
    const encoded = nativeFixtureLiteralWire(input)
    shared.count = 2
    expect(encoded).toEqual({ kind: 'json', json: '{"first":{"count":1},"second":{"count":1}}' })
    expect(input.first).toBe(shared)
    expect(Object.isFrozen(encoded)).toBe(true)
  })
  it('rejects unsupported nested undefined, functions, symbols, bigint and nonfinite numbers', () => {
    for (const input of [
      { x: undefined },
      [undefined],
      () => undefined,
      Symbol('literal'),
      1n,
      NaN,
      Infinity,
      -Infinity,
    ])
      expect(() => nativeFixtureLiteralWire(input)).toThrow()
  })
  it('rejects cycles and non-data objects without invoking accessors', () => {
    const cycle: { next?: object } = {}
    cycle.next = cycle
    let observed = 0
    const getter = Object.defineProperty({}, 'value', {
      enumerable: true,
      get: () => {
        observed++
        return 1
      },
    })
    for (const input of [
      cycle,
      getter,
      new Date(),
      new Map(),
      new Uint8Array([1]),
      Object.create({ inherited: 1 }) as object,
    ])
      expect(() => nativeFixtureLiteralWire(input)).toThrow()
    expect(observed).toBe(0)
  })
  it('rejects sparse/extended arrays and hidden/symbol properties instead of silently dropping data', () => {
    const extended = Object.assign([1], { extra: 1 }),
      hidden = Object.defineProperty({}, 'hidden', { value: 1 }),
      symbol = { [Symbol('value')]: 1 }
    for (const input of [new Array(1), extended, hidden, symbol])
      expect(() => nativeFixtureLiteralWire(input)).toThrow()
  })
  it('accepts plain null-prototype records as data', () => {
    const value = Object.assign(Object.create(null) as object, { item: 17 })
    expect(nativeFixtureLiteralWire(value)).toEqual({ kind: 'json', json: '{"item":17}' })
  })
  it('enforces native JSON byte bounds including escaping and UTF8 expansion', () => {
    expect(nativeFixtureLiteralWire('x'.repeat(8190)).kind).toBe('json')
    for (const input of ['x'.repeat(8191), '\0'.repeat(1400), '😀'.repeat(2100)])
      expect(() => nativeFixtureLiteralWire(input)).toThrow('capacity')
  })
  it('uses the same depth/node limits as native policy', () => {
    const nested = (depth: number): object => {
      let value: object = []
      while (depth-- > 0) value = [value]
      return value
    }
    expect(nativeFixtureLiteralWire(nested(16)).kind).toBe('json')
    expect(() => nativeFixtureLiteralWire(nested(17))).toThrow('capacity')
    expect(nativeFixtureLiteralWire(Array<number>(2047).fill(0)).kind).toBe('json')
    expect(() => nativeFixtureLiteralWire(Array<number>(2048).fill(0))).toThrow('capacity')
  })
})
