// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { fileURLToPath } from 'node:url'

import { describe, expect, it } from 'vitest'

import { extendNativeFixtures } from '../src/native-fixture-layers.js'
import { nativeFixtureLocation } from '../src/native-fixture-location.js'
import { NativeFixtureBindings } from '../src/native-runner-fixture-bindings.js'

const base = () => undefined,
  override = () => undefined
const declarations = () =>
  extendNativeFixtures(
    { foo: { setup: base, scope: 'worker' }, bar: { setup: base, dependencies: ['foo'] } },
    { foo: { setup: override, dependencies: ['foo'] } },
  )
const fixture = () => new NativeFixtureBindings(declarations())
function callback(binding: NativeFixtureBindings, key: string) {
  const definition = binding.version(key).definition
  if (!('setup' in definition)) throw new Error('Expected actual callback declaration')
  return definition.setup
}
function admit(binding: NativeFixtureBindings) {
  binding.packet(['FVERSION', '1', 'nativeFixture1', 'worker', '-'])
  binding.packet(['FVERSION', '2', 'nativeFixture2', 'test', 'foo:nativeFixture3'])
  binding.packet(['FVERSION', '3', 'nativeFixture3', 'worker', 'foo:nativeFixture1'])
  binding.packet(['FROOT', 'foo', 'nativeFixture3'])
  binding.packet(['FROOT', 'bar', 'nativeFixture2'])
  binding.packet(['FPLAN_READY', '3', '2'])
}
describe('native fixture mapping marshaling (controlled native packets)', () => {
  it('serializes only original declarations and leaves inheritance/resolution to C++', () => {
    const file = fileURLToPath(import.meta.url)
    const catalog = declarations()
    const captured = nativeFixtureLocation(catalog.layers[0]!.foo!)
    expect(captured?.file).toBe(file)
    // Validate the genuine V8 capture, including the executing loader's line
    // and column, rather than substituting guessed source-text offsets.
    for (const value of [captured?.line, captured?.column]) {
      expect(Number.isSafeInteger(value)).toBe(true)
      expect(value).toBeGreaterThan(0)
      expect(value).toBeLessThanOrEqual(2147483647)
    }
    for (const layer of catalog.layers)
      for (const definition of Object.values(layer))
        expect(nativeFixtureLocation(definition)).toEqual(captured)
    const location = [Buffer.from(file).toString('hex'), captured!.line, captured!.column]
    expect(new NativeFixtureBindings(catalog).rows()).toEqual([
      ['FLAYER', 1],
      ['FDECL', 1, 'foo', 'worker', '-', '-'],
      ['FLOCATION', 1, ...location],
      ['FDECL', 2, 'bar', '-', '-', 'foo'],
      ['FLOCATION', 2, ...location],
      ['FLAYER', 2],
      ['FDECL', 3, 'foo', '-', '-', 'foo'],
      ['FLOCATION', 3, ...location],
    ])
  })
  it('exposes the actual original callback under the admitted native version', () => {
    const binding = fixture()
    expect(() => binding.version('nativeFixture1')).toThrow('admitted')
    admit(binding)
    expect(callback(binding, 'nativeFixture1')).toBe(base)
    expect(callback(binding, 'nativeFixture3')).toBe(override)
    expect(binding.version('nativeFixture3').scope).toBe('worker')
    expect(binding.version('nativeFixture3').bindings).toEqual([['foo', 'nativeFixture1']])
    expect(binding.root('foo')).toBe('nativeFixture3')
    expect(binding.root('bar')).toBe('nativeFixture2')
  })
  it('rejects foreign/repeated declaration identity and explicit scope mismatch', () => {
    const binding = fixture()
    for (const fields of [
      ['FVERSION', '4', 'nativeFixture4', 'worker', '-'],
      ['FVERSION', '1', 'nativeFixture3', 'worker', '-'],
      ['FVERSION', '1', 'nativeFixture1', 'test', '-'],
    ])
      expect(() => binding.packet(fields)).toThrow()
    binding.packet(['FVERSION', '1', 'nativeFixture1', 'worker', '-'])
    expect(() => binding.packet(['FVERSION', '1', 'nativeFixture1', 'worker', '-'])).toThrow()
  })
  it('rejects alias shape/name mismatches without selecting a replacement target', () => {
    for (const alias of [
      '-',
      'other:nativeFixture1',
      'foo:nativeFixture1:extra',
      'foo:foreign1',
      'foo:nativeFixture1,foo:nativeFixture2',
    ])
      expect(() => fixture().packet(['FVERSION', '3', 'nativeFixture3', 'worker', alias])).toThrow()
  })
  it('rejects missing effective roots and unmapped dependency targets', () => {
    const binding = fixture()
    binding.packet(['FVERSION', '2', 'nativeFixture2', 'test', 'foo:nativeFixture3'])
    binding.packet(['FROOT', 'bar', 'nativeFixture2'])
    expect(() => binding.packet(['FPLAN_READY', '1', '1'])).toThrow('incomplete')
    binding.packet(['FVERSION', '3', 'nativeFixture3', 'worker', 'foo:nativeFixture1'])
    binding.packet(['FROOT', 'foo', 'nativeFixture3'])
    expect(() => binding.packet(['FPLAN_READY', '2', '2'])).toThrow('target')
  })
  it('rejects roots pointing at a different original name or a missing version', () => {
    const binding = fixture()
    binding.packet(['FVERSION', '1', 'nativeFixture1', 'worker', '-'])
    expect(() => binding.packet(['FROOT', 'bar', 'nativeFixture1'])).toThrow()
    expect(() => binding.packet(['FROOT', 'foo', 'nativeFixture3'])).toThrow()
  })
  it('rejects post-admission remapping while retaining mappings for live worker reuse', () => {
    const binding = fixture()
    admit(binding)
    expect(() => binding.packet(['FROOT', 'foo', 'nativeFixture1'])).toThrow('already admitted')
    expect(callback(binding, 'nativeFixture3')).toBe(override)
    expect(() => binding.root('missing')).toThrow()
    expect(() => binding.version('nativeFixture4')).toThrow()
    expect(binding.packet(['BODY', '1', '1'])).toBe(false)
  })
  it('accepts an actual empty raw catalog with complete native acknowledgment', () => {
    const binding = new NativeFixtureBindings(extendNativeFixtures({}, {}))
    expect(binding.rows()).toEqual([
      ['FLAYER', 1],
      ['FLAYER', 2],
    ])
    expect(binding.packet(['FPLAN_READY', '0', '0'])).toBe(true)
  })
})
