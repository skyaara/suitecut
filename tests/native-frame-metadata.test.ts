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
  it('decodes names past the old 1024-unit and token-length caps whole', () => {
    expect(
      NativeFrameInfoSchema.parse({ ...frame, nameJSON: JSON.stringify('\ud800'.repeat(1025)) })
        .name,
    ).toBe('\ud800'.repeat(1025))
    expect(
      NativeFrameInfoSchema.parse({ ...frame, nameJSON: JSON.stringify('x'.repeat(100_000)) }).name,
    ).toBe('x'.repeat(100_000))
    const padded = `${' '.repeat(100_000)}${JSON.stringify('padded')}${' '.repeat(100_000)}`
    expect(NativeFrameInfoSchema.parse({ ...frame, nameJSON: padded }).name).toBe('padded')
    for (const nameJSON of [' ', '"unterminated', '"a" "b"'])
      expect(NativeFrameInfoSchema.safeParse({ ...frame, nameJSON }).success).toBe(false)
  })
})
