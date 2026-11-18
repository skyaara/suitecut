// SPDX-License-Identifier: MIT
import { type UntrustedInput } from './untrusted.js'

type Fields = Record<string, UntrustedInput>
function isolated(value: string): boolean {
  for (let index = 0; index < value.length; index++) {
    const unit = value.charCodeAt(index)
    if (unit >= 0xd800 && unit <= 0xdbff) {
      const next = value.charCodeAt(index + 1)
      if (next >= 0xdc00 && next <= 0xdfff) {
        index++
        continue
      }
      return true
    }
    if (unit >= 0xdc00 && unit <= 0xdfff) return true
  }
  return false
}
const queryStrings = [
  'selector',
  'text',
  'pattern',
  'flags',
  'attribute',
  'role',
  'name',
  'message',
]
const queryLists = ['query', 'has', 'hasNot', 'descendants']
const patterns = ['name', 'description', 'hasText', 'hasNotText']
function record(value: UntrustedInput): value is Fields {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
}
/** Encode only explicitly selected protocol string fields; never inspect author arguments. */
export function encodeNativeStringFields(command: string, fields: Fields): Fields {
  const strings = (input: Fields, keys: readonly string[]): Fields => {
    const result = { ...input }
    for (const key of keys) {
      const value = input[key]
      if (typeof value === 'string' && isolated(value)) {
        if (Object.hasOwn(input, key + 'JSON')) throw new Error('Ambiguous native string field')
        result[key + 'JSON'] = JSON.stringify(value)
        delete result[key]
      }
    }
    return result
  }
  // A cyclic query is malformed; fail with a clear error instead of exhausting the stack.
  const ancestors = new Set<object>()
  const visit = <T>(input: object, walk: () => T): T => {
    if (ancestors.has(input)) throw new Error('Native query wire is cyclic')
    ancestors.add(input)
    try {
      return walk()
    } finally {
      ancestors.delete(input)
    }
  }
  const query = (input: UntrustedInput): UntrustedInput => {
    if (Array.isArray(input))
      return visit(input, () => input.map((item: UntrustedInput) => query(item)))
    if (!record(input)) return input
    return visit(input, () => {
      const result = strings(input, queryStrings)
      for (const key of queryLists) if (Array.isArray(input[key])) result[key] = query(input[key])
      for (const key of patterns) {
        const value = input[key]
        if (record(value)) result[key] = strings(value, ['text', 'pattern', 'flags'])
      }
      return result
    })
  }
  const scope = (input: Fields): Fields =>
    visit(input, () => {
      const result = strings(input, ['selector'])
      for (const key of ['query', 'framePath'])
        if (Array.isArray(input[key])) result[key] = query(input[key])
      for (const key of ['target', 'element'])
        if (record(input[key])) result[key] = scope(input[key])
      if (Array.isArray(input.masks))
        result.masks = input.masks.map((mask: UntrustedInput) =>
          record(mask) ? scope(mask) : mask,
        )
      return result
    })
  const result = scope(fields)
  if (
    command === 'bindings' &&
    typeof fields.action === 'string' &&
    ['register', 'remove'].includes(fields.action)
  )
    return strings(result, ['name'])
  if (
    command === 'context' &&
    typeof fields.action === 'string' &&
    ['exposeBinding', 'removeBinding'].includes(fields.action) &&
    record(fields.params)
  )
    result.params = strings(fields.params, ['name'])
  return result
}

/** Decode one dedicated native-produced name field; unrelated objects remain opaque. */
export function decodeNativeBindingName(input: {
  name?: string | undefined
  nameJSON?: string | undefined
}): string {
  if ((input.name === undefined) === (input.nameJSON === undefined))
    throw new Error('Invalid native binding name wire identity')
  const value: UntrustedInput =
    input.nameJSON === undefined ? input.name : (JSON.parse(input.nameJSON) as UntrustedInput)
  if (typeof value !== 'string' || !value.length) throw new Error('Invalid native binding name')
  return value
}
