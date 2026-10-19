// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { type NativeFixtureDefinition } from './native-suite.js'
import { type UntrustedInput } from './untrusted.js'

export type NativeFixtureJSON =
  | null
  | boolean
  | number
  | string
  | readonly NativeFixtureJSON[]
  | { readonly [key: string]: NativeFixtureJSON }
export type NativeFixtureLiteral = NativeFixtureJSON | undefined
export interface NativeLiteralFixtureDefinition {
  readonly title?: string
  readonly timeout?: number
  readonly value: NativeFixtureLiteral
  readonly scope?: 'worker' | 'test'
  readonly automatic?: boolean
  readonly option?: boolean
}
export type NativeSelectableFixtureDefinition =
  NativeFixtureDefinition | NativeLiteralFixtureDefinition
export type NativeFixtureLiteralWire =
  { readonly kind: 'undefined' } | { readonly kind: 'json'; readonly json: string }

/** Bounded declaration encoding only. Native C++ validates and selects values. */
export function nativeFixtureLiteralWire(input: UntrustedInput): NativeFixtureLiteralWire {
  if (input === undefined) return Object.freeze({ kind: 'undefined' })
  const chunks: string[] = [],
    active = new Set<object>()
  let bytes = 0,
    nodes = 0
  const add = (text: string): void => {
    bytes += Buffer.byteLength(text)
    if (bytes > 8192) throw new RangeError('Native fixture literal JSON byte capacity8192 exceeded')
    chunks.push(text)
  }
  const quote = (value: string): void => {
    if (value.length > 8192) throw new RangeError('Native fixture literal string capacity exceeded')
    add(JSON.stringify(value))
  }
  const encode = (value: UntrustedInput, depth: number): void => {
    if (depth > 16 || ++nodes > 2048)
      throw new RangeError('Native fixture literal JSON depth/node capacity exceeded')
    if (value === null) return add('null')
    if (typeof value === 'string') return quote(value)
    if (typeof value === 'boolean') return add(value ? 'true' : 'false')
    if (typeof value === 'number') {
      if (!Number.isFinite(value))
        throw new TypeError('Native fixture literal number must be finite')
      return add(Object.is(value, -0) ? '-0' : String(value))
    }
    if (typeof value !== 'object')
      throw new TypeError('Native fixture literals require JSON data or top-level undefined')
    if (active.has(value)) throw new TypeError('Native fixture literal cycle rejected')
    const array = Array.isArray(value),
      prototype = Object.getPrototypeOf(value) as UntrustedInput
    if (
      array ? prototype !== Array.prototype : prototype !== Object.prototype && prototype !== null
    )
      throw new TypeError('Native fixture literals require plain data objects or arrays')
    const descriptors = Object.getOwnPropertyDescriptors(value),
      keys = Reflect.ownKeys(descriptors)
    if (keys.some((key) => typeof key !== 'string'))
      throw new TypeError('Native fixture literal symbol property rejected')
    active.add(value)
    try {
      if (array) {
        if (value.length > 2048 || keys.length !== value.length + 1)
          throw new TypeError('Native fixture literal sparse or extended array rejected')
        add('[')
        for (let index = 0; index < value.length; index++) {
          const descriptor = descriptors[String(index)]
          if (!descriptor?.enumerable || !Object.hasOwn(descriptor, 'value'))
            throw new TypeError('Native fixture literal array accessor or hole rejected')
          if (index) add(',')
          encode(descriptor.value as UntrustedInput, depth + 1)
        }
        add(']')
      } else {
        add('{')
        for (const [index, key] of keys.entries()) {
          if (typeof key !== 'string')
            throw new TypeError('Native fixture literal property rejected')
          const descriptor = descriptors[key]
          if (!descriptor?.enumerable || !Object.hasOwn(descriptor, 'value'))
            throw new TypeError('Native fixture literal accessor or hidden property rejected')
          if (index) add(',')
          quote(key)
          add(':')
          encode(descriptor.value as UntrustedInput, depth + 1)
        }
        add('}')
      }
    } finally {
      active.delete(value)
    }
  }
  encode(input, 0)
  return Object.freeze({ kind: 'json', json: chunks.join('') })
}
