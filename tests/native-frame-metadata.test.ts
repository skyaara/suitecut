// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeAutomationEventSchema, NativeFrameInfoSchema } from '../src/native-protocol.js'

const frame = { id: 'actual-frame', parentId: 'actual-parent', url: 'http://actual.test/child' }
describe('selected native frame name wire contract', () => {
  it('decodes only the dedicated native nameJSON field and preserves real URL and frame identity', () => {
    const name = 'Actual\ud800\0\\"\udfff😀'
    expect(NativeFrameInfoSchema.parse({ ...frame, nameJSON: JSON.stringify(name) })).toEqual({
      ...frame,
      name,
    })
    expect(
      NativeAutomationEventSchema.parse({
        event: 'framenavigated',
        frame: { ...frame, nameJSON: JSON.stringify(name) },
      }),
    ).toEqual({ event: 'framenavigated', frame: { ...frame, name } })
  })
  it('keeps ordinary names and private-tag-looking author strings opaque', () => {
    for (const name of ['', 'Actual 😀 Ω', '{"__suitecutNativeUTF16Scalar":"d800"}', '"\\ud800"'])
      expect(NativeFrameInfoSchema.parse({ ...frame, name })).toEqual({ ...frame, name })
  })
  it('fails closed for missing or ambiguous identity and malformed or non-string JSON without throwing outside safeParse', () => {
    for (const extra of [
      {},
      { name: 'regular', nameJSON: '"encoded"' },
      { nameJSON: '{"author":true}' },
      { nameJSON: 'unterminated' },
      { nameJSON: 'null' },
      { nameJSON: '42' },
    ])
      expect(NativeFrameInfoSchema.safeParse({ ...frame, ...extra }).success).toBe(false)
  })
  it('bounds actual decoded UTF16 units and encoded token length independently', () => {
    expect(
      NativeFrameInfoSchema.parse({ ...frame, nameJSON: JSON.stringify('\ud800'.repeat(1024)) })
        .name,
    ).toHaveLength(1024)
    expect(
      NativeFrameInfoSchema.safeParse({ ...frame, nameJSON: JSON.stringify('\ud800'.repeat(1025)) })
        .success,
    ).toBe(false)
    expect(
      NativeFrameInfoSchema.safeParse({ ...frame, nameJSON: JSON.stringify('x'.repeat(1025)) })
        .success,
    ).toBe(false)
    expect(NativeFrameInfoSchema.safeParse({ ...frame, nameJSON: ' '.repeat(6147) }).success).toBe(
      false,
    )
  })
})
