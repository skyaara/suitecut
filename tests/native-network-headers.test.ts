// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  NativeObservedHeadersSchema,
  normalizeNativeObservedHeaders,
  nativeObservedHeaderValues,
  NativeStringHeadersSchema,
} from '../src/native-network-headers.js'
import { type UntrustedInput } from '../src/untrusted.js'

describe('native owned HTTP header fields', () => {
  it('preserves own poison-named fields while cloning actual JSON metadata', () => {
    const input: UntrustedInput = JSON.parse(
      '{"__proto__":"wire","constructor":"actual","X-Number":5}',
    )
    const observed = NativeObservedHeadersSchema.parse(input)
    expect(Object.hasOwn(observed, '__proto__')).toBe(true)
    expect(observed.__proto__).toBe('wire')
    expect(observed.constructor).toBe('actual')
    expect(observed['X-Number']).toBe(5)
    expect(Object.getPrototypeOf(observed)).toBe(Object.prototype)
    expect(observed).not.toBe(input)
    const options = NativeStringHeadersSchema.parse(JSON.parse('{"__proto__":"authored"}'))
    expect(Object.hasOwn(options, '__proto__')).toBe(true)
    expect(options.__proto__).toBe('authored')
  })
  it('rejects non-header values and inherited tables', () => {
    expect(() => NativeStringHeadersSchema.parse({ count: 2 })).toThrow('headers')
    expect(() => NativeObservedHeadersSchema.parse({ count: Infinity })).toThrow('headers')
    expect(() => NativeObservedHeadersSchema.parse({ count: {} })).toThrow('headers')
    expect(() => NativeObservedHeadersSchema.parse(['invalid'])).toThrow('headers')
    expect(() => NativeObservedHeadersSchema.parse(new Map())).toThrow('headers')
  })
  it('combines every actual case variant without discarding empty values or poison names', () => {
    const observed = Object.fromEntries([
      ['Name-A', 'v1'],
      ['Name-a', 'v2'],
      ['name-A', 'v3\nv4'],
      ['Empty', ''],
      ['empty', 'actual'],
      ['__proto__', 'actual wire field'],
    ])
    const headers = normalizeNativeObservedHeaders(observed)
    expect(headers['name-a']).toBe('v1, v2, v3, v4')
    expect(headers.empty).toBe(', actual')
    expect(Object.hasOwn(headers, '__proto__')).toBe(true)
    expect(headers.__proto__).toBe('actual wire field')
    expect(Object.getPrototypeOf(headers)).toBe(Object.prototype)
    expect(nativeObservedHeaderValues(observed, 'NAME-a')).toEqual(['v1', 'v2', 'v3', 'v4'])
    expect(nativeObservedHeaderValues(observed, 'absent')).toEqual([])
  })
  it('preserves every Set-Cookie occurrence and commas inside an actual field value', () => {
    const observed = {
      'Set-Cookie': 'first=1; Expires=Wed, 21 Oct 2037 07:28:00 GMT\nsecond=2',
      'set-cookie': 'third=3',
      Link: '<first>; rel=a, <second>; rel=b',
    }
    expect(normalizeNativeObservedHeaders(observed)['set-cookie']).toBe(
      'first=1; Expires=Wed, 21 Oct 2037 07:28:00 GMT\nsecond=2\nthird=3',
    )
    expect(nativeObservedHeaderValues(observed, 'SET-cookie')).toEqual([
      'first=1; Expires=Wed, 21 Oct 2037 07:28:00 GMT',
      'second=2',
      'third=3',
    ])
    expect(nativeObservedHeaderValues(observed, 'link')).toEqual([
      '<first>; rel=a, <second>; rel=b',
    ])
  })
})
