import { describe, expect, it } from 'vitest'

import { nativeURLGlobMatcher, resolveNativeURLGlob } from '../src/native-url-glob.js'

describe('native frame URL glob lookup', () => {
  it('resolves base URLs without corrupting alternatives or quoted wildcards', () => {
    const base = 'https://suitecutglobtoken.test/root/'
    expect(resolveNativeURLGlob('../*.{html,svg}?x=*', base)).toBe(
      'https://suitecutglobtoken.test/*.{html,svg}?x=*',
    )
    expect(resolveNativeURLGlob('./\\*.html', base)).toBe(
      'https://suitecutglobtoken.test/root/\\*.html',
    )
    expect(resolveNativeURLGlob('**/frame.html', base)).toBe('**/frame.html')
    expect(resolveNativeURLGlob('/frame.html', base)).toBe(
      'https://suitecutglobtoken.test/frame.html',
    )
    expect(
      nativeURLGlobMatcher(resolveNativeURLGlob('./*.{html,svg}', base))(
        'https://suitecutglobtoken.test/root/frame.svg',
      ),
    ).toBe(true)
  })
  it('matches whole real URLs and distinguishes component and deep stars', () => {
    expect(nativeURLGlobMatcher('**/frame.html')('https://a.test/nested/frame.html')).toBe(true)
    expect(nativeURLGlobMatcher('https://a.test/*.html')('https://a.test/nested/frame.html')).toBe(
      false,
    )
    expect(nativeURLGlobMatcher('https://a.test/*.html')('https://a.test/frame.html')).toBe(true)
    expect(nativeURLGlobMatcher('frame.html')('https://a.test/frame.html')).toBe(false)
  })
  it('preserves literal query separators and escaped wildcard characters', () => {
    expect(nativeURLGlobMatcher('**/frame.html?x=*')('https://a.test/frame.html?x=1')).toBe(true)
    expect(nativeURLGlobMatcher('**/frame.html?x=*')('https://a.test/frame.htmlAx=1')).toBe(false)
    expect(nativeURLGlobMatcher('**/\\*.html')('https://a.test/*.html')).toBe(true)
    expect(nativeURLGlobMatcher('**/\\*.html')('https://a.test/frame.html')).toBe(false)
  })
  it('expands nested alternatives without treating single braces as choices', () => {
    expect(nativeURLGlobMatcher('**/*.{html,{htm,svg}}')('https://a.test/frame.svg')).toBe(true)
    expect(nativeURLGlobMatcher('**/*.{html,{htm,svg}}')('https://a.test/frame.js')).toBe(false)
    expect(nativeURLGlobMatcher('**/{literal}')('https://a.test/{literal}')).toBe(true)
    expect(nativeURLGlobMatcher('')('')).toBe(true)
  })
  it('rejects explosive expansion and excessive comparison work explicitly', () => {
    expect(() => nativeURLGlobMatcher('{a,b}'.repeat(10))).toThrow('capacity')
    expect(() => nativeURLGlobMatcher('*a'.repeat(3000))('a'.repeat(8192))).toThrow('work capacity')
  })
})
