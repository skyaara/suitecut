// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it } from 'vitest'

import { NativeRepeatCallbacks, nativeRepeatCount } from '../src/native-runner-repeat.js'

const actor = (local = '0', index = '1', declared = '3', effective = '2', iterations = '2') => [
  'REPEAT_ACTOR_CONTEXT',
  '11',
  '2',
  local,
  index,
  declared,
  effective,
  iterations,
]
const test = (id = '1', logical = `native-repeat:11:2:${id}:1`) => [
  'TEST_REPEAT_CONTEXT',
  id,
  Buffer.from(logical).toString('hex'),
]

describe('native repeat callback transport', () => {
  it('keeps legacy native callbacks at ABI index zero with original public IDs', () => {
    const callbacks = new NativeRepeatCallbacks()
    expect(callbacks.snapshot('1')).toEqual({
      repeatEachIndex: 0,
      declaredRepeatEach: 1,
      effectiveRepeatEach: 1,
      iterations: 1,
      logicalTestId: '1',
    })
  })
  it('retains native admitted identity/counts as an immutable callback snapshot', () => {
    const callbacks = new NativeRepeatCallbacks()
    expect(callbacks.begin(actor('1'), 1)).toBe(3)
    callbacks.packet(test(), 2)
    const first = callbacks.snapshot('1')
    expect(first).toEqual({
      repeatEachIndex: 1,
      declaredRepeatEach: 3,
      effectiveRepeatEach: 2,
      iterations: 2,
      logicalTestId: 'native-repeat:11:2:1:1',
    })
    expect(callbacks.snapshot('1')).toBe(first)
    expect(Object.isFrozen(first)).toBe(true)
  })
  it('requires selected native mappings once the repeat actor is admitted', () => {
    const callbacks = new NativeRepeatCallbacks()
    callbacks.begin(actor(), 0)
    expect(() => callbacks.snapshot('1')).toThrow('required selected test mapping')
    callbacks.packet(test(), 2)
    expect(() => callbacks.snapshot('2')).toThrow('required selected test mapping')
  })
  it('rejects foreign and duplicate actor contexts without replacing the owner', () => {
    const callbacks = new NativeRepeatCallbacks()
    expect(() => callbacks.begin(actor('2'), 1)).toThrow('original project/context')
    callbacks.begin(actor('1'), 1)
    expect(() => callbacks.begin(actor('1'), 1)).toThrow('original project/context')
    callbacks.packet(test(), 1)
    expect(callbacks.snapshot('1').declaredRepeatEach).toBe(3)
  })
  it('rejects absent actor, foreign declaration and replayed test mappings', () => {
    const callbacks = new NativeRepeatCallbacks()
    expect(() => callbacks.packet(test(), 1)).toThrow('exact actor/declaration')
    callbacks.begin(actor(), 0)
    expect(() => callbacks.packet(test('2'), 1)).toThrow('exact actor/declaration')
    callbacks.packet(test(), 1)
    expect(() => callbacks.packet(test(), 1)).toThrow('exact actor/declaration')
  })
  it('rejects mismatched logical identities without authorizing a callback', () => {
    const callbacks = new NativeRepeatCallbacks()
    callbacks.begin(actor(), 0)
    for (const logical of [
      'native-repeat:11:2:1:0',
      'native-repeat:12:2:1:1',
      'native-repeat:11:3:1:1',
      'native-repeat:11:2:2:1',
    ])
      expect(() => callbacks.packet(test('1', logical), 1)).toThrow('admitted actor/declaration')
    expect(() => callbacks.snapshot('1')).toThrow('required selected test mapping')
  })
  it('normalizes configured zero only from actual native count context', () => {
    const callbacks = new NativeRepeatCallbacks()
    callbacks.begin(actor('0', '0', '0', '0', '1'), 0)
    callbacks.packet(test('1', 'native-repeat:11:2:1:0'), 1)
    expect(callbacks.snapshot('1')).toMatchObject({
      repeatEachIndex: 0,
      declaredRepeatEach: 0,
      effectiveRepeatEach: 0,
      iterations: 1,
    })
    expect(() => new NativeRepeatCallbacks().begin(actor('0', '0', '0', '0', '0'), 0)).toThrow()
  })
  it('rejects unsupported numeric coercions and out-of-range iteration claims', () => {
    for (const value of [-1, 1.5, NaN, Infinity, Number.MAX_SAFE_INTEGER + 1])
      expect(() => nativeRepeatCount.parse(value)).toThrow()
    expect(() => new NativeRepeatCallbacks().begin(actor('0', '2'), 0)).toThrow(
      'original project/context',
    )
  })
  it('transports declared project counts including zero before repeated callbacks', () => {
    const callbacks = new NativeRepeatCallbacks()
    expect(callbacks.project(['PROJECT_REPEAT_CONTEXT', '1', '0'], 1)).toBe(0)
    expect(callbacks.snapshot('1').repeatEachIndex).toBe(0)
    expect(() => callbacks.project(['PROJECT_REPEAT_CONTEXT', '1', '0'], 1)).toThrow(
      'original project admission',
    )
    const repeated = new NativeRepeatCallbacks()
    expect(repeated.project(['PROJECT_REPEAT_CONTEXT', '1', '3'], 1)).toBe(3)
    expect(repeated.begin(actor('1'), 1)).toBe(3)
  })
  it('rejects foreign, late and contradictory native project count contexts', () => {
    const callbacks = new NativeRepeatCallbacks()
    expect(() => callbacks.project(['PROJECT_REPEAT_CONTEXT', '2', '1'], 1)).toThrow(
      'original project admission',
    )
    expect(() => callbacks.project(['PROJECT_REPEAT_CONTEXT', '0', '1'], 0)).toThrow(
      'original project admission',
    )
    callbacks.project(['PROJECT_REPEAT_CONTEXT', '1', '1'], 1)
    expect(() => callbacks.begin(actor('1'), 1)).toThrow('original project/context')
    const late = new NativeRepeatCallbacks()
    late.begin(actor('1'), 1)
    expect(() => late.project(['PROJECT_REPEAT_CONTEXT', '1', '3'], 1)).toThrow(
      'original project admission',
    )
  })
})
