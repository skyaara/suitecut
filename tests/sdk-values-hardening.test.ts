// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { nativeURLGlobMatcher } from '../src/native-url-glob.js'
import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

describe('SDK boundary hardening regressions', () => {
  it('keeps a long-lived URL registration usable beyond the old lifetime budget', () => {
    const match = nativeURLGlobMatcher('https://a.test/*.html')
    for (let i = 0; i < 100000; i++) expect(match('https://a.test/frame.html')).toBe(true)
    expect(match('https://a.test/nested/frame.html')).toBe(false)
  })

  it('preserves quoted stars, literal braces and disjoint prefix/suffix matching', () => {
    for (const [pattern, url, result] of [
      ['a*a', 'a', false],
      ['a*a', 'aa', true],
      ['a*a', 'a/a', false],
      ['a**a', 'a/a', true],
      ['a\\*a', 'a*a', true],
      ['*\\**', 'abc*def', true],
      ['a\\', 'a\\', true],
      ['{literal}*x', '{literal}x', true],
      ['**/{x,y}/file.*', 'https://a.test/x/file.js', true],
    ] as const)
      expect(nativeURLGlobMatcher(pattern)(url)).toBe(result)
  })

  it('decodes every UTF-16 unit exactly while rejecting noncanonical hex', () => {
    const codec = createNativeValueCodec()
    const chunks: string[] = []
    for (let start = 0; start < 65536; start += 1024)
      chunks.push(String.fromCharCode(...Array.from({ length: 1024 }, (_, i) => start + i)))
    const text = chunks.join('')
    expect(codec.decode(JSON.parse(JSON.stringify(codec.encode(text))) as UntrustedInput)).toBe(
      text,
    )
    for (const bad of ['D800', 'd80G', 'd80/', 'd80:', 'd80`', 'd80g', '\u0000000'])
      expect(() => codec.decode(['utf16', bad])).toThrow('Invalid native serialized value')
  })
})
