// SPDX-License-Identifier: MIT
import { expect, it } from 'vitest'

import { nativeCookieFilter } from '../src/native-cookie-filter.js'

it('marshals exact selected patterns and preserves caller regex state without matching in SDK', () => {
  const name = /^sess/giu
  name.lastIndex = 9
  expect(nativeCookieFilter({ name, domain: /one\.com$/i, path: '/api' })).toEqual({
    name: { sourceJSON: JSON.stringify('^sess'), flags: 'giu' },
    domain: { sourceJSON: JSON.stringify('one\\.com$'), flags: 'i' },
    path: '/api',
  })
  expect(name.lastIndex).toBe(9)
  expect(nativeCookieFilter({ name: '', domain: undefined })).toEqual({ name: '' })
})

it('keeps isolated UTF16 pattern units in the dedicated source JSON field', () => {
  const source = '\ud800\u0000'
  const filter = nativeCookieFilter({ name: new RegExp(source, 'u') })
  expect(filter).toEqual({
    name: { sourceJSON: JSON.stringify(new RegExp(source, 'u').source), flags: 'u' },
  })
})

it('rejects native regex and literal metadata capacity before sending control work', () => {
  expect(() => nativeCookieFilter({ name: new RegExp('a'.repeat(32769)) })).toThrow(/capacity/)
  expect(() => nativeCookieFilter({ name: 'a'.repeat(8193) })).toThrow()
})
