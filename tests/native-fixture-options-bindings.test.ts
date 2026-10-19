// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it } from 'vitest'

import { extendNativeFixtures } from '../src/native-fixture-layers.js'
import { NativeFixtureBindings } from '../src/native-runner-fixture-bindings.js'

const encoded = (value: string) => Buffer.from(value).toString('hex')
describe('native selected fixture constants (controlled mapping receipts)', () => {
  it('serializes raw explicit undefined and option metadata without selecting a value', () => {
    const bindings = new NativeFixtureBindings(
      { foo: { value: undefined, option: true } },
      { foo: false },
    )
    expect(bindings.rows()).toEqual([
      ['FLAYER', 1],
      ['FDECL', 1, 'foo', '-', '-', '-'],
      ['FOPTION', 1, 1],
      ['FLITERAL', 1, 'U', '-'],
      ['FCONFIG', 'foo', 'J', encoded('false')],
    ])
    expect(() => bindings.version('nativeFixture1')).toThrow('admitted')
  })
  it('preserves genuine declared data only after exact native constant and plan acknowledgments', () => {
    const bindings = new NativeFixtureBindings({ foo: { value: { text: '\ud800\0', count: -0 } } })
    const wire = encoded('{"text":"\\ud800\\u0000","count":-0}')
    bindings.packet(['FCONST', '1', 'nativeFixture1', 'J', 'L', wire])
    bindings.packet(['FVERSION', '1', 'nativeFixture1', 'test', '-'])
    bindings.packet(['FROOT', 'foo', 'nativeFixture1'])
    bindings.packet(['FPLAN_READY', '1', '1'])
    expect(bindings.version('nativeFixture1').constant).toEqual({
      declarationId: '1',
      fixtureKey: 'nativeFixture1',
      origin: 'declaredLiteral',
      value: { text: '\ud800\0', count: -0 },
    })
  })
  it('rejects an author literal as an executable version until actual native constant selection', () => {
    const bindings = new NativeFixtureBindings({ foo: { value: null } })
    expect(() => bindings.packet(['FVERSION', '1', 'nativeFixture1', 'test', '-'])).toThrow(
      'constant selection',
    )
  })
  it('lets the actual native configured constant omit callback dependencies without invoking them', () => {
    let calls = 0
    const bindings = new NativeFixtureBindings(
      extendNativeFixtures(
        { foo: { value: 'original', option: true } },
        {
          foo: {
            setup: () => {
              calls++
            },
            dependencies: ['foo'],
          },
        },
      ),
      { foo: undefined },
    )
    bindings.packet(['FCONST', '2', 'nativeFixture2', 'U', 'C', '-'])
    bindings.packet(['FVERSION', '2', 'nativeFixture2', 'test', '-'])
    bindings.packet(['FROOT', 'foo', 'nativeFixture2'])
    bindings.packet(['FPLAN_READY', '1', '1'])
    expect(bindings.version('nativeFixture2').constant).toEqual({
      declarationId: '2',
      fixtureKey: 'nativeFixture2',
      origin: 'configuredLiteral',
      value: undefined,
    })
    expect(bindings.version('nativeFixture2').bindings).toEqual([])
    expect(calls).toBe(0)
  })
  it('retains original callback dependencies when no configured constant was admitted', () => {
    const bindings = new NativeFixtureBindings({
      foo: { setup: () => undefined, option: true, dependencies: ['bar'] },
      bar: { value: 1 },
    })
    expect(() => bindings.packet(['FVERSION', '1', 'nativeFixture1', 'test', '-'])).toThrow(
      'dependencies',
    )
  })
  it('rejects altered bytes, source kinds, foreign keys and unexpected undefined markers', () => {
    for (const packet of [
      ['FCONST', '1', 'nativeFixture1', 'J', 'L', encoded('2')],
      ['FCONST', '1', 'nativeFixture1', 'J', 'C', encoded('1')],
      ['FCONST', '1', 'nativeFixture1', 'J', 'X', encoded('1')],
      ['FCONST', '1', 'nativeFixture2', 'J', 'L', encoded('1')],
      ['FCONST', '1', 'nativeFixture1', 'U', 'L', '-'],
    ])
      expect(() => new NativeFixtureBindings({ foo: { value: 1 } }).packet(packet)).toThrow()
  })
  it('bounds flat raw declarations before sending any metadata or invoking a callback', () => {
    const callbacks = Object.fromEntries(
      Array.from({ length: 257 }, (_, index) => [`item${index}`, { setup: () => undefined }]),
    )
    expect(() => new NativeFixtureBindings(callbacks)).toThrow('capacity')
    const data = Object.fromEntries(
      Array.from({ length: 256 }, (_, index) => [`item${index}`, { value: 'x'.repeat(8190) }]),
    )
    expect(() => new NativeFixtureBindings(data)).toThrow('capacity')
  })
  it('rejects duplicate, late and unmapped constants without admitting a fake callback', () => {
    const bindings = new NativeFixtureBindings({ foo: { value: 1 } })
    const packet = ['FCONST', '1', 'nativeFixture1', 'J', 'L', encoded('1')]
    bindings.packet(packet)
    expect(() => bindings.packet(packet)).toThrow('identity')
    expect(() => bindings.packet(['FPLAN_READY', '0', '0'])).toThrow()
    bindings.packet(['FVERSION', '1', 'nativeFixture1', 'test', '-'])
    expect(() => bindings.packet(packet)).toThrow('identity')
  })
})
