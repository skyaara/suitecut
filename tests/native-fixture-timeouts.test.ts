// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { parseNativeFixtureDeclaration } from '../src/native-fixture-declarations.js'
import { extendNativeFixtures } from '../src/native-fixture-layers.js'
import { nativeFixtureLocation } from '../src/native-fixture-location.js'
import { NativeFixtureBindings } from '../src/native-runner-fixture-bindings.js'
import { defineNativeSuite } from '../src/native-suite.js'

const setup = async (): Promise<void> => {
  await Promise.resolve()
}
describe('fixture timeout declaration marshalling; native scheduling remains separate', () => {
  it('preserves absent versus explicit zero for real callback and literal declarations', () => {
    const absent = parseNativeFixtureDeclaration({ setup })
    expect(Object.hasOwn(absent, 'timeout')).toBe(false)
    expect(parseNativeFixtureDeclaration({ setup, timeout: 0 }).timeout).toBe(0)
    expect(parseNativeFixtureDeclaration({ value: undefined, timeout: 0 }).timeout).toBe(0)
  })
  it('rejects malformed/unbounded timeout data before serializing declarations', () => {
    for (const timeout of [-1, 0.1, 120001, NaN, Infinity, '100', null])
      expect(() => parseNativeFixtureDeclaration({ setup, timeout })).toThrow()
    expect(parseNativeFixtureDeclaration({ setup, timeout: 120000 }).timeout).toBe(120000)
  })
  it('sends raw timeout records tied to actual native declaration positions without resolving inheritance', () => {
    const declarations = extendNativeFixtures({ foo: { setup, timeout: 1000 } }, { foo: { setup } })
    const rows = new NativeFixtureBindings(declarations).rows()
    expect(rows.filter((row) => row[0] === 'FTIMEOUT')).toEqual([['FTIMEOUT', 1, 1000]])
    expect(rows.filter((row) => row[0] === 'FDECL').map((row) => row[1])).toEqual([1, 2])
  })
  it('captures genuine define/extend caller provenance while leaving unissued raw objects without a location', () => {
    const raw = { setup }
    expect(nativeFixtureLocation(raw)).toBeUndefined()
    const suite = defineNativeSuite({ fixtures: { raw }, tests: [] })
    const location = nativeFixtureLocation(raw)
    expect(location?.file).toMatch(/native-fixture-timeouts\.test\.ts$/u)
    expect(location?.line).toBeGreaterThan(0)
    expect(location?.column).toBeGreaterThan(0)
    const rows = new NativeFixtureBindings(suite.fixtures).rows()
    expect(rows.filter((row) => row[0] === 'FLOCATION')).toHaveLength(1)
    expect(
      new NativeFixtureBindings({ untouched: { setup } })
        .rows()
        .some((row) => row[0] === 'FLOCATION'),
    ).toBe(false)
  })
  it('retains explicit zero on an override rather than flattening it into omitted configuration', () => {
    const declarations = extendNativeFixtures(
      { foo: { setup, timeout: 1000 } },
      { foo: { setup, timeout: 0 } },
    )
    expect(
      new NativeFixtureBindings(declarations).rows().filter((row) => row[0] === 'FTIMEOUT'),
    ).toEqual([
      ['FTIMEOUT', 1, 1000],
      ['FTIMEOUT', 2, 0],
    ])
  })
})
