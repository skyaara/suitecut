// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { parseNativeFixtureDeclaration } from '../src/native-fixture-declarations.js'

describe('raw literal/callback fixture declarations', () => {
  it('retains actual callback identity and only declared metadata', () => {
    const setup = () => undefined
    const fixture = parseNativeFixtureDeclaration({
      setup,
      dependencies: ['previous'],
      option: true,
    })
    expect(fixture).toEqual({ setup, dependencies: ['previous'], option: true })
    expect(Object.isFrozen(fixture)).toBe(true)
    expect('scope' in fixture).toBe(false)
    expect('setup' in fixture && fixture.setup).toBe(setup)
  })
  it('requires explicit undefined rather than treating a missing value as a literal', () => {
    expect(parseNativeFixtureDeclaration({ value: undefined })).toEqual({ value: undefined })
    for (const input of [
      {},
      { scope: 'worker' },
      { option: true },
      { value: undefined, setup: () => undefined },
    ])
      expect(() => parseNativeFixtureDeclaration(input)).toThrow()
  })
  it('snapshots and freezes JSON data with exact negative-zero/UTF16 content', () => {
    const value = { nested: [-0, '\ud800\0'] },
      fixture = parseNativeFixtureDeclaration({ value, scope: 'worker', option: true })
    value.nested.push('later')
    if (
      !('value' in fixture) ||
      !fixture.value ||
      typeof fixture.value !== 'object' ||
      Array.isArray(fixture.value)
    )
      throw new Error('Expected raw record declaration')
    expect(fixture.value).toEqual({ nested: [-0, '\ud800\0'] })
    expect(Object.isFrozen(fixture.value)).toBe(true)
    expect(Object.isFrozen(Object.values(fixture.value)[0])).toBe(true)
  })
  it('rejects literal dependency lists and malformed kinds instead of invoking callbacks', () => {
    let invoked = 0
    for (const input of [
      { value: 1, dependencies: ['other'] },
      { setup: () => invoked++, option: 'true' },
      { value: new Date() },
      { value: [undefined] },
      { value: 1, extra: true },
    ])
      expect(() => parseNativeFixtureDeclaration(input)).toThrow()
    expect(invoked).toBe(0)
  })
})
