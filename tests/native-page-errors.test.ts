// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { createNativePageError, NativePageErrorInfoSchema } from '../src/native-console.js'

describe('native page error metadata', () => {
  it('retains exact complete name, message and independent custom stack strings', () => {
    const units = '\ud800a\0\\"\udfff😀'
    const source = {
      name: 'Kind: ' + units,
      message: units + '\ncomplete second line',
      stack: 'custom stack\n' + units,
    }
    const error = createNativePageError(NativePageErrorInfoSchema.parse(source))
    expect(error.name).toBe(source.name)
    expect(error.message).toBe(source.message)
    expect(error.stack).toBe(source.stack)
  })
  it('preserves bounded genuine source identity without shortening complete names', () => {
    const frame = {
      id: 'genuine-frame',
      parentId: '',
      name: 'actual',
      url: 'https://actual.test/error',
    }
    const source = { name: 'K'.repeat(1024), message: 'actual', stack: 'independent', frame }
    const result = NativePageErrorInfoSchema.parse(source)
    expect(result.frame).toEqual(frame)
    expect(createNativePageError(result).name).toHaveLength(1024)
    expect(() =>
      NativePageErrorInfoSchema.parse({ ...source, name: 'K'.repeat(1_000_001) }),
    ).toThrow()
  })
})
