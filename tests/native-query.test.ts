// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  nativeRoleQuery,
  nativeQueryDescription,
  nativeTextPattern,
  nativeTestIdQuery,
  setNativeTestIdAttribute,
} from '../src/native-query.js'

describe('native query encoding', () => {
  it('preserves exact whitespace and regular expression flags for the DOM matcher', () => {
    expect(nativeTextPattern(' a ', { exact: true })).toEqual({ text: ' a ', exact: true })
    expect(nativeTextPattern(/text/giu)).toEqual({ pattern: 'text', flags: 'giu' })
  })

  it('serializes all role states without introducing undefined properties', () => {
    const query = nativeRoleQuery('heading', {
      name: 'Example',
      level: 2,
      disabled: false,
      includeHidden: true,
    })
    expect(query).toEqual({
      engine: 'role',
      role: 'heading',
      name: { text: 'Example' },
      level: 2,
      disabled: false,
      includeHidden: true,
    })
    expect(nativeRoleQuery('button')).toEqual({ engine: 'role', role: 'button' })
  })

  it('rejects malformed filters and unsupported options before native transport', () => {
    expect(() => nativeRoleQuery('', {})).toThrow()
    expect(() => nativeRoleQuery('heading', { level: 1.5 })).toThrow()
    expect(() => nativeRoleQuery('heading', { level: 0 })).toThrow()
    expect(() => nativeRoleQuery('button', { level: 2 })).toThrow(/only supported/u)
    expect(() => nativeRoleQuery('heading', { checked: false })).toThrow(/only supported/u)
    expect(() => nativeRoleQuery('button', { unsupported: true } as never)).toThrow()
    expect(() => nativeTextPattern('x', { unknown: true } as never)).toThrow()
  })
  it('validates configurable test id attributes and preserves exact id matching', () => {
    setNativeTestIdAttribute('data-pw, data-ti, data-pw')
    expect(nativeTestIdQuery('Exact')).toEqual({
      engine: 'testId',
      attribute: 'data-pw,data-ti',
      text: 'Exact',
      exact: true,
    })
    expect(() => setNativeTestIdAttribute('data-pw,,data-ti')).toThrow()
    expect(() => setNativeTestIdAttribute('invalid name')).toThrow()
    setNativeTestIdAttribute('data-testid')
  })
  it('encodes accessible descriptions using the same exact and regex semantics as names', () => {
    expect(nativeRoleQuery('button', { description: ' form ', exact: true })).toEqual({
      engine: 'role',
      role: 'button',
      description: { text: ' form ', exact: true },
    })
    expect(nativeRoleQuery('button', { description: /draft/i })).toEqual({
      engine: 'role',
      role: 'button',
      description: { pattern: 'draft', flags: 'i' },
    })
  })
  it('describes actual frame paths for deferred composition errors', () => {
    expect(
      nativeQueryDescription(
        [{ engine: 'css', selector: 'span' }],
        [[{ engine: 'css', selector: 'iframe' }]],
      ),
    ).toBe("locator('iframe').contentFrame().locator('span')")
  })
})
