// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it, vi } from 'vitest'

import {
  createNativeMatcherInvocation,
  runNativeMatcherInvocation,
  nativeMatcherFailure,
  takeNativeMatcherFailure,
  closeNativeMatcherInvocation,
} from '../src/native-matcher-failure.js'
import { NativeSoftAssertionCallbacks } from '../src/native-runner-soft-assertions.js'
import { NativeStepCallbacks } from '../src/native-runner-steps.js'

const owner = { token: '0', scope: '1', generation: '1' }
const wire = (value: string): string => Buffer.from(value, 'utf16le').toString('hex') || '-'
function fixture() {
  const sent: (string | number)[][] = [],
    fatal = vi.fn<(error: Error) => void>()
  const journal = new NativeSoftAssertionCallbacks(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    fatal,
    () => undefined,
    (error) => (error instanceof Error ? (error.stack ?? error.message) : 'controlled raw value'),
    () => '0',
    { workerIndex: 1, pid: 101 },
  )
  journal.packet(['SOFT_CONTEXT', '0', '1', '1', '1', '1', '1', '101', 'body'])
  return { journal, sent, fatal }
}
function actualFailure(error: Error) {
  const invocation = createNativeMatcherInvocation()
  runNativeMatcherInvocation(invocation, () => nativeMatcherFailure(error))
  const receipt = takeNativeMatcherFailure(invocation, error)
  closeNativeMatcherInvocation(invocation)
  if (!receipt) throw new Error('Controlled actual producer failed')
  return receipt
}
describe('native soft host ACK marshaling (controlled packets)', () => {
  it('registers synchronously and exposes original mismatch object only after exact native ACK', async () => {
    const { journal, sent } = fixture(),
      original = new Error('genuine producer error')
    const admitted = journal.run(
      owner,
      () => true,
      () => journal.begin({ file: '/actual.ts', line: 3, column: 7 }),
    )
    expect(sent[0]?.slice(0, 6)).toEqual(['SOFT_BEGIN', '1', '0', '1', '1', '0'])
    expect(journal.physical).toBe(1)
    journal.packet(['SOFT_ADMITTED', '1', '0', '1', '1', '19', '1', '27', '-'])
    const lease = await admitted
    expect(lease.timeoutMs).toBe(27)
    const settled = lease.settle({ kind: 'softFailure', receipt: actualFailure(original) })
    expect(journal.errors(owner)).toEqual([])
    journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '19', '1', '-'])
    await settled
    const view = journal.errors(owner)
    expect(view[0]).toBe(original)
    expect(Object.isFrozen(view)).toBe(true)
    expect(journal.errors(owner)[0]).toBe(original)
  })
  it('retains native request until raw settlement ACK after real native abort', async () => {
    const { journal } = fixture(),
      admitted = journal.run(
        owner,
        () => true,
        () => journal.begin(),
      )
    journal.packet(['SOFT_ADMITTED', '1', '0', '1', '1', '1', '1', '1', '-'])
    const lease = await admitted
    journal.packet(['SOFT_ABORT', '1', '0', '1', '1', '1', wire('actual native deadline')])
    expect(lease.signal.aborted).toBe(true)
    expect(journal.physical).toBe(1)
    const settled = lease.settle({ kind: 'otherFailure', error: lease.signal.reason as Error })
    const rejected = expect(settled).rejects.toThrow('late native receipt')
    journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '1', '0', wire('late native receipt')])
    await rejected
    expect(journal.physical).toBe(0)
    expect(journal.errors(owner)).toEqual([])
  })
  it('holds callback release for native context retirement and rejects premature retirement', async () => {
    const { journal } = fixture(),
      released = vi.fn(),
      admitted = journal.run(
        owner,
        () => true,
        () => journal.begin(),
      )
    journal.retireCallback(owner, released)
    expect(released).not.toHaveBeenCalled()
    expect(() => journal.packet(['SOFT_CONTEXT_RETIRED', '0', '1', '1'])).toThrow(
      'before actual matcher ACKs',
    )
    journal.packet(['SOFT_ADMITTED', '1', '0', '1', '1', '1', '1', '29', '-'])
    const settled = (await admitted).settle({ kind: 'passed' })
    journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '1', '1', '-'])
    await settled
    journal.packet(['SOFT_CONTEXT_RETIRED', '0', '1', '1'])
    expect(released).toHaveBeenCalledOnce()
    expect(journal.idleReady).toBe(true)
    expect(() =>
      journal.run(
        owner,
        () => true,
        () => journal.begin(),
      ),
    ).toThrow('body/beforeEach/afterEach')
  })
  it('validates exact native owner, immutable attempt view and receipt nonce', async () => {
    const { journal } = fixture(),
      admitted = journal.run(
        owner,
        () => true,
        () => journal.begin(),
      )
    expect(() =>
      journal.packet(['SOFT_CONTEXT', '7', '2', '1', '1', '1', '1', '999', 'afterEach']),
    ).toThrow('foreign')
    expect(() => journal.packet(['SOFT_ADMITTED', '1', '0', '2', '1', '1', '1', '9', '-'])).toThrow(
      'foreign',
    )
    journal.packet(['SOFT_ADMITTED', '1', '0', '1', '1', '1', '1', '9', '-'])
    const lease = await admitted,
      original = new Error('same attempt')
    const settled = lease.settle({ kind: 'softFailure', receipt: actualFailure(original) })
    journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '1', '1', '-'])
    await settled
    expect(() => journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '1', '1', '-'])).toThrow(
      'retired',
    )
    journal.packet(['SOFT_CONTEXT_RETIRED', '0', '1', '1'])
    journal.packet(['SOFT_CONTEXT', '7', '2', '1', '1', '1', '1', '101', 'afterEach'])
    expect(journal.errors({ token: '7', scope: '2', generation: '1' })[0]).toBe(original)
    expect(() => journal.errors(owner)).toThrow('live callback')
  })
  it('never exposes raw hard errors as accepted soft mismatches', async () => {
    const { journal } = fixture(),
      admitted = journal.run(
        owner,
        () => true,
        () => journal.begin(),
      )
    journal.packet(['SOFT_ADMITTED', '1', '0', '1', '1', '1', '1', '19', '-'])
    const settled = (await admitted).settle({
      kind: 'otherFailure',
      error: new Error('actual hard'),
    })
    journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '1', '1', '-'])
    await settled
    expect(journal.errors(owner)).toEqual([])
  })
  it('rejects detached ALS contexts after native callback admission closes', () => {
    const { journal, sent } = fixture()
    expect(() =>
      journal.run(
        owner,
        () => false,
        () => journal.begin(),
      ),
    ).toThrow('active supported callback')
    expect(() => journal.begin()).toThrow('active supported callback')
    expect(sent).toEqual([])
  })
})
describe('soft step typed native result', () => {
  it('resolves the genuine callback object only after raw success and native physical receipt', async () => {
    const sent: (string | number)[][] = [],
      actual = { actual: true }
    const steps = new NativeStepCallbacks(
      (...fields) => {
        sent.push(fields)
        return Promise.resolve()
      },
      () => undefined,
      () => undefined,
    )
    const result = steps.bind(owner, () => true)('soft actual', () => {
      expect(steps.current(owner)).toBe('19')
      return actual
    })
    steps.handle(['STEP_ADMITTED', '1', '0', '1', '1', '19', '1', '-'])
    await vi.waitFor(() => expect(sent.some((f) => f[0] === 'STEP_SETTLED')).toBe(true))
    steps.handle(['STEP_SOFT_RESULT', '19', '0', '1', '1', '27', wire('actual soft mismatch')])
    steps.handle(['STEP_RECEIVED', '19', '0', '1', '1'])
    await expect(result).resolves.toBe(actual)
    expect(steps.current(owner)).toBe('0')
  })
})
