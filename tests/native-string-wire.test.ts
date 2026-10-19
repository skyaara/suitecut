// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { captureNativeElementScreenshot } from '../src/native-artifacts.js'
import { NativeBindingEventSchema } from '../src/native-bindings.js'
import { decodeNativeBindingName, encodeNativeStringFields } from '../src/native-string-wire.js'

describe('explicit native inbound string wire', () => {
  const original = '\ud800a\0\\"\udfff😀'
  it('keeps ordinary fields and author objects intact while restoring exact selected units', () => {
    const author = { __suitecutNativeUTF16Scalar: 'd800', selector: original, nameJSON: '"evil"' }
    const input = { selector: original, query: [{ engine: 'text', text: original }], args: author }
    const encoded = encodeNativeStringFields('query', input)
    expect(encoded.args).toBe(author)
    expect(encoded.selector).toBeUndefined()
    expect(JSON.parse(encoded.selectorJSON as string)).toBe(original)
    expect(encoded.query).toEqual([{ engine: 'text', textJSON: JSON.stringify(original) }])
    expect(input.selector).toBe(original)
    expect(encodeNativeStringFields('query', { selector: '😀', args: author })).toEqual({
      selector: '😀',
      args: author,
    })
  })
  it('encodes composed scopes and role patterns without touching unrelated objects', () => {
    const result = encodeNativeStringFields('artifact', {
      selector: 'body',
      framePath: [[{ engine: 'css', selector: original }]],
      query: [
        {
          engine: 'filter',
          has: [{ engine: 'role', role: 'button', name: { text: original } }],
          hasText: { pattern: original, flags: 'u' },
        },
      ],
      masks: [{ selector: original, query: [{ engine: 'css', selector: original }] }],
      target: { selector: original },
    })
    const json = JSON.stringify(result)
    expect(json).not.toContain('"text":"\\ud800')
    expect(json).not.toContain('"selector":"\\ud800')
    expect(json).toContain('textJSON')
    expect(json).toContain('patternJSON')
    expect(() =>
      encodeNativeStringFields('query', {
        query: [{ engine: 'text', text: original, textJSON: '"bad"' }],
      }),
    ).toThrow('Ambiguous')
  })
  it('encodes the actual element and masks envelope produced by artifact capture', async () => {
    const page = {}
    const target = { selector: original, query: [{ engine: 'text', text: original, exact: true }] }
    let request: ReturnType<typeof encodeNativeStringFields> | undefined
    await expect(
      captureNativeElementScreenshot(
        (params) => {
          request = encodeNativeStringFields('artifact', params)
          return Promise.reject(new Error('captured actual artifact request'))
        },
        target,
        { mask: [{ nativeScreenshotDescriptor: () => ({ page, target }) }] },
        page,
      ),
    ).rejects.toThrow('captured actual artifact request')
    const expected = {
      selectorJSON: JSON.stringify(original),
      query: [{ engine: 'text', textJSON: JSON.stringify(original), exact: true }],
    }
    expect(request).toMatchObject({ element: expected, masks: [expected] })
    expect(target.query[0]?.text).toBe(original)
  })
  it('restricts binding-name decoding to the mutually exclusive dedicated field', () => {
    for (const command of ['bindings', 'context']) {
      const output = encodeNativeStringFields(
        command,
        command === 'bindings'
          ? { action: 'register', name: original }
          : { action: 'exposeBinding', params: { name: original } },
      )
      const wire = (command === 'bindings' ? output : output.params) as {
        name?: string
        nameJSON?: string
      }
      expect(wire.name).toBeUndefined()
      expect(decodeNativeBindingName(wire)).toBe(original)
    }
    expect(encodeNativeStringFields('bindings', { action: 'resolve', name: original }).name).toBe(
      original,
    )
    expect(() => decodeNativeBindingName({ name: 'a', nameJSON: '"b"' })).toThrow()
    expect(() => decodeNativeBindingName({ nameJSON: '{}' })).toThrow()
    expect(() => decodeNativeBindingName({ nameJSON: JSON.stringify('x'.repeat(257)) })).toThrow()
    const author = { __suitecutNativeUTF16Scalar: 'd800' }
    const event = NativeBindingEventSchema.parse({
      event: 'call',
      id: '1',
      nameJSON: JSON.stringify(original),
      frame: { id: 'main', parentId: '', name: '', url: 'about:blank' },
      args: author,
    })
    expect(event.event).toBe('call')
    if (event.event === 'call') {
      expect(event.name).toBe(original)
      expect(event.args).toBe(author)
    }
  })
  it('bounds composed wire traversal and does not serialize unrelated argument trees', () => {
    const cycle: { query?: object[] } = {}
    cycle.query = [cycle]
    expect(() => encodeNativeStringFields('query', cycle)).toThrow('limit')
    const args: { self?: object } = {}
    args.self = args
    expect(encodeNativeStringFields('evaluate', { args }).args).toBe(args)
  })
})
