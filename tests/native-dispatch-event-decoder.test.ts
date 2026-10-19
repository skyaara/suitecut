// SPDX-License-Identifier: MIT
import { readFileSync } from 'node:fs'
import { runInNewContext } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

const header = readFileSync(new URL('../native/browser/dispatch_event.h', import.meta.url), 'utf8')
const expression = /R"JS\(([\s\S]*?)\)JS"/u.exec(header)?.[1]
if (!expression) throw new Error('Native-owned dispatch decoder source is missing')
// These controlled tests exercise native-codec compatibility and actual Node
// EventTarget delivery. The constructor aliases do not establish browser event
// subclass behavior; the separate real-CEF verifier owns that proof.
const value: UntrustedInput = runInNewContext(`(${expression})`, {
  Event,
  CustomEvent,
  EventTarget,
  MouseEvent: Event,
  PointerEvent: Event,
  KeyboardEvent: Event,
  FocusEvent: Event,
  InputEvent: Event,
  WheelEvent: Event,
  DragEvent: Event,
  TouchEvent: Event,
  CompositionEvent: Event,
  ClipboardEvent: Event,
  SubmitEvent: Event,
})
if (typeof value !== 'function') throw new Error('Native dispatch source is not callable')
const dispatch = value as (
  element: EventTarget,
  packet: UntrustedInput,
  handles: UntrustedInput[],
) => UntrustedInput
class Target extends EventTarget {
  isConnected = true
  nodeType = 1
}

describe('native event decoder with controlled Node platform delivery', () => {
  it('delivers actual CustomEvent detail with codec cycles, literal proto key, units, bytes and node-reference identity', () => {
    const target = new Target(),
      related = new Target()
    const detail: Record<string, UntrustedInput> = {
      units: '\ud800',
      bytes: new Uint16Array([0, 255, 65535]),
      related,
    }
    detail.self = detail
    Object.defineProperty(detail, '__proto__', { value: 'literal', enumerable: true })
    const packet = createNativeValueCodec().encode(
      { type: 'custom', eventInit: { detail } },
      'argument',
      (item) => (item === related ? 0 : undefined),
    )
    let observed: UntrustedInput
    target.addEventListener('custom', (event) => {
      observed = (event as CustomEvent<UntrustedInput>).detail
      event.preventDefault()
    })
    expect(dispatch(target, packet, [related])).toEqual({ ready: true, dispatched: false })
    const data = observed as Record<string, UntrustedInput>
    expect(data.self).toBe(data)
    expect(data.related).toBe(related)
    expect(data.units).toBe('\ud800')
    expect(Object.hasOwn(data, '__proto__')).toBe(true)
    expect(data.__proto__).toBe('literal')
    expect(Array.from(data.bytes as Uint16Array)).toEqual([0, 255, 65535])
  })
  it('rejects malformed references/handles and bounded graph depth before any actual event delivery', () => {
    const target = new Target()
    let delivered = 0
    target.addEventListener('custom', () => ++delivered)
    for (const bad of [
      ['reference', 1],
      ['handle', 0],
      ['bytes', 'UnknownArray', [1]],
      ['utf16', 'xx'],
    ])
      expect(() => dispatch(target, bad, [])).toThrow('Invalid native dispatchEvent argument')
    let nested: UntrustedInput = ['null']
    for (let i = 0; i < 102; ++i) nested = ['array', i + 1, [nested]]
    expect(() => dispatch(target, nested, [])).toThrow('Invalid native dispatchEvent argument')
    expect(delivered).toBe(0)
  })
  it('does not dispatch to detached or invalid original receivers and retains attached Text identity', () => {
    const target = new Target(),
      packet = createNativeValueCodec().encode({ type: 'custom', eventInit: {} })
    let delivered = 0
    target.addEventListener('custom', () => ++delivered)
    target.isConnected = false
    expect(() => dispatch(target, packet, [])).toThrow('Element is not attached')
    target.isConnected = true
    target.nodeType = 0
    expect(() => dispatch(target, packet, [])).toThrow('Target is not a DOM node')
    expect(delivered).toBe(0)
    target.nodeType = 3
    dispatch(target, packet, [])
    expect(delivered).toBe(1)
  })
})
