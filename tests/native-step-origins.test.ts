// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeStepFailureOrigins } from '../src/native-runner-step-origins.js'

const owner = { token: '0', scope: '1', generation: '1' }
describe('actual admitted object-identity origin bookkeeping', () => {
  it('retains distinct sibling origins of one shared Error instead of overwriting them', () => {
    const ledger = new NativeStepFailureOrigins(),
      error = new Error('shared')
    ledger.admit(owner, '1', '0')
    ledger.admit(owner, '2', '0')
    ledger.observe(owner, '1', error)
    expect(ledger.source(owner, error)).toBe('1')
    ledger.observe(owner, '2', error)
    expect(ledger.source(owner, error)).toBeUndefined()
    expect(ledger.source(owner, error, '1')).toBeUndefined()
    expect(ledger.source(owner, error, '2')).toBeUndefined()
  })
  it('uses the actual propagated outer receipt while restricting child claims to observed descendants', () => {
    const ledger = new NativeStepFailureOrigins(),
      error = new Error('same object')
    ledger.admit(owner, '1', '0')
    ledger.admit(owner, '2', '1')
    ledger.admit(owner, '3', '0')
    ledger.observe(owner, '2', error)
    expect(ledger.source(owner, error, '1')).toBe('2')
    expect(ledger.source(owner, error, '3')).toBeUndefined()
    ledger.observe(owner, '1', error)
    expect(ledger.source(owner, error)).toBe('1')
  })
  it('never matches primitive equality, equal text, or a foreign callback/attempt', () => {
    const ledger = new NativeStepFailureOrigins(),
      error = new Error('equal text')
    ledger.admit(owner, '1', '0')
    ledger.observe(owner, '1', error)
    expect(ledger.source(owner, new Error('equal text'))).toBeUndefined()
    expect(ledger.source({ ...owner, generation: '2' }, error)).toBeUndefined()
    for (const value of [17, 'equal text', undefined, null, false])
      expect(ledger.source(owner, value)).toBeUndefined()
  })
  it('rejects foreign parent, duplicate admission and unadmitted or repeated failure receipts', () => {
    const ledger = new NativeStepFailureOrigins(),
      error = new Error('actual')
    ledger.admit(owner, '1', '0')
    expect(() => ledger.admit(owner, '1', '0')).toThrow()
    expect(() => ledger.admit({ ...owner, scope: '2' }, '2', '1')).toThrow()
    expect(() => ledger.observe(owner, '2', error)).toThrow()
    ledger.observe(owner, '1', error)
    expect(() => ledger.observe(owner, '1', error)).toThrow()
  })
  it('retains actual uint64 identities beyond JavaScript number precision', () => {
    const ledger = new NativeStepFailureOrigins(),
      error = new Error('exact IDs')
    ledger.admit(owner, '9007199254740993', '0')
    ledger.admit(owner, '9007199254740994', '9007199254740993')
    ledger.observe(owner, '9007199254740994', error)
    expect(ledger.source(owner, error, '9007199254740993')).toBe('9007199254740994')
  })
  it('keeps identities separate for concurrent owners sharing one object', () => {
    const ledger = new NativeStepFailureOrigins(),
      error = new Error('shared across callbacks'),
      other = { ...owner, token: '7' }
    ledger.admit(owner, '1', '0')
    ledger.admit(other, '2', '0')
    ledger.observe(owner, '1', error)
    ledger.observe(other, '2', error)
    expect(ledger.source(owner, error)).toBe('1')
    expect(ledger.source(other, error)).toBe('2')
    expect(() => ledger.source(owner, error, '2')).toThrow()
  })
})
