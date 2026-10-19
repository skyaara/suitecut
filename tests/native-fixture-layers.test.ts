// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'
import * as z from 'zod'

import { NativeFixtureDeclarationSchema } from '../src/native-fixture-declarations.js'
import {
  extendNativeFixtures,
  isNativeFixtureLayers,
  nativeFixtureDeclarationLayers,
  type NativeFixtureDeclarations,
} from '../src/native-fixture-layers.js'
import { type UntrustedInput } from '../src/untrusted.js'

const setup = () => undefined
function callback(layer: NativeFixtureDeclarations | undefined, name: string) {
  const definition = layer?.[name]
  if (!definition || !('setup' in definition)) throw new Error('Expected raw callback declaration')
  return definition
}

describe('raw native fixture declaration layers', () => {
  it('allows genuine branded layers through a raw-record schema union without throwing from its failed branch', () => {
    const schema = z.union([
      z.record(z.string(), NativeFixtureDeclarationSchema),
      z.custom((value) => isNativeFixtureLayers(value as UntrustedInput)),
    ])
    const layers = extendNativeFixtures({ item: { value: 1 } }, { item: { value: 2 } })
    expect(schema.parse(layers)).toBe(layers)
    expect(schema.safeParse({ item: { setup: 17 } }).success).toBe(false)
  })
  it('preserves real callback identity and raw self/sibling dependencies without resolving', () => {
    const previous = { setup },
      replacement = { setup, dependencies: ['foo', 'bar'] }
    const result = extendNativeFixtures({ foo: previous, bar: { setup } }, { foo: replacement })
    expect(result.layers).toHaveLength(2)
    expect(callback(result.layers[0], 'foo').setup).toBe(setup)
    expect(callback(result.layers[1], 'foo').dependencies).toEqual(['foo', 'bar'])
    expect(result.layers[1]?.foo?.scope).toBeUndefined()
    expect(result.layers[1]?.foo?.automatic).toBeUndefined()
  })
  it('snapshots caller-owned objects and dependency arrays, including later extensions', () => {
    const dependencies = ['base'],
      original = { setup, dependencies },
      base = { foo: original }
    const first = extendNativeFixtures(base, {}),
      second = extendNativeFixtures(first, { foo: { setup, dependencies: ['foo'] } })
    dependencies.push('changed')
    base.foo = { setup, dependencies: [] }
    expect(callback(first.layers[0], 'foo').dependencies).toEqual(['base'])
    expect(second.layers.slice(0, 2)).toEqual(first.layers)
    expect(Object.isFrozen(second)).toBe(true)
    expect(Object.isFrozen(second.layers)).toBe(true)
    expect(Object.isFrozen(second.layers[0])).toBe(true)
    expect(Object.isFrozen(second.layers[0]?.foo)).toBe(true)
    expect(Object.isFrozen(callback(second.layers[0], 'foo').dependencies)).toBe(true)
  })
  it('keeps fixture names layers and kind ordinary and rejects forged catalog shapes', () => {
    const result = extendNativeFixtures({ layers: { setup }, kind: { setup } }, {})
    expect(Object.keys(result.layers[0] ?? {})).toEqual(['layers', 'kind'])
    expect(() => nativeFixtureDeclarationLayers({ layers: [] } as never)).toThrow()
    expect(() => nativeFixtureDeclarationLayers({ ...result })).toThrow()
  })
  it('retains explicit false/worker metadata and preserves flat compatibility', () => {
    const flat = { foo: { setup, scope: 'worker' as const, automatic: false } }
    expect(nativeFixtureDeclarationLayers(flat)).toEqual([flat])
    expect(extendNativeFixtures(flat, {}).layers[0]?.foo).toEqual(flat.foo)
  })
  it('rejects malformed raw declarations without invoking any callback', () => {
    for (const invalid of [
      { 'bad name': { setup } },
      { foo: { setup: 1 } },
      { foo: { setup, dependencies: ['bad name'] } },
      { foo: { setup, scope: 'other' } },
      { foo: { setup, unsupported: true } },
    ])
      expect(() => extendNativeFixtures(invalid as NativeFixtureDeclarations, {})).toThrow()
  })
  it('bounds total raw declarations and layer count, counting shadowed versions', () => {
    const base = Object.fromEntries(
      Array.from({ length: 256 }, (_, index) => [`f${index}`, { setup }]),
    )
    expect(extendNativeFixtures(base, {}).layers[0]).toHaveProperty('f255')
    expect(() => extendNativeFixtures(base, { f0: { setup } })).toThrow('capacity')
    let catalog = extendNativeFixtures({}, {})
    for (let index = 2; index < 32; index++) catalog = extendNativeFixtures(catalog, {})
    expect(catalog.layers).toHaveLength(32)
    expect(() => extendNativeFixtures(catalog, {})).toThrow('capacity32')
  })
  it('bounds raw dependency edges before native catalog admission', () => {
    const base = Object.fromEntries(
      Array.from({ length: 17 }, (_, index) => [
        `f${index}`,
        { setup, dependencies: Array<string>(256).fill('base') },
      ]),
    )
    expect(() => extendNativeFixtures(base, {})).toThrow('capacity')
  })
})
