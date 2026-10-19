// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it } from 'vitest'

import { nativeAssertionDeadlineError } from '../src/native-assertion-deadline.js'
import {
  closeNativeMatcherInvocation,
  createNativeMatcherInvocation,
  nativeMatcherAssertionDeadline,
  nativeMatcherFailure,
  runNativeMatcherInvocation,
  takeNativeMatcherFailure,
} from '../src/native-matcher-failure.js'
import { NativeSoftAssertionCallbacks } from '../src/native-runner-soft-assertions.js'

const owner = { token: '0', scope: '1', generation: '1' }
const wire = (value: string): string => Buffer.from(value, 'utf16le').toString('hex') || '-'
function fixture() {
  const sent: (string | number)[][] = []
  const journal = new NativeSoftAssertionCallbacks(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    (error) => {
      throw error
    },
    () => undefined,
    (error) => (error instanceof Error ? (error.stack ?? error.message) : 'controlled raw value'),
    () => '0',
    { workerIndex: 1, pid: 101 },
  )
  journal.packet(['SOFT_CONTEXT', '0', '1', '1', '1', '1', '1', '101', 'body'])
  const admitted = journal.run(
    owner,
    () => true,
    () => journal.begin(),
  )
  journal.packet(['SOFT_ADMITTED', '1', '0', '1', '1', '19', '1', '1000', '-'])
  return { journal, sent, admitted }
}
function receipt(error: Error, deadline: boolean) {
  const invocation = createNativeMatcherInvocation()
  runNativeMatcherInvocation(invocation, () => {
    if (deadline) {
      // Controlled private transport metadata; this test claims no Source run.
      const source = nativeAssertionDeadlineError(
        'actual controlled deadline',
        {
          type: 'native-assertion-deadline',
          version: 1,
          operationId: 19,
          generation: '7',
          matcher: 'toHaveText',
          deadlineMs: 12,
          observed: false,
        },
        { id: 19, matcher: 'toHaveText' },
      )
      nativeMatcherAssertionDeadline(error, source)
    } else nativeMatcherFailure(error)
  })
  const actual = takeNativeMatcherFailure(invocation, error)
  closeNativeMatcherInvocation(invocation)
  if (!actual) throw new Error('Controlled issued receipt missing')
  return actual
}
describe('native assertion-deadline host marshaling (controlled private receipts/ACKs)', () => {
  it('sends distinct D with the original Error and exposes it only after actual ACK', async () => {
    const { journal, sent, admitted } = fixture(),
      error = new Error('original deadline \ud800\0')
    const lease = await admitted
    const settled = lease.settle({ kind: 'assertionDeadline', receipt: receipt(error, true) })
    expect(sent.at(-1)?.slice(0, 7)).toEqual(['SOFT_SETTLED', '1', '0', '1', '1', '19', 'D'])
    expect(sent.at(-1)?.[7]).toBe(wire(error.stack ?? error.message))
    expect(journal.physical).toBe(1)
    expect(journal.errors(owner)).toEqual([])
    journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '19', '1', '-'])
    await settled
    expect(journal.errors(owner)[0]).toBe(error)
    expect(journal.physical).toBe(0)
  })
  it('rejects relabeling an observed mismatch receipt as an assertion deadline', async () => {
    const { journal, sent, admitted } = fixture(),
      error = new Error('original mismatch')
    const lease = await admitted,
      actual = receipt(error, false)
    expect(() => lease.settle({ kind: 'assertionDeadline', receipt: actual })).toThrow(TypeError)
    expect(sent.filter((fields) => fields[0] === 'SOFT_SETTLED')).toEqual([])
    expect(journal.physical).toBe(1)
    const settled = lease.settle({ kind: 'softFailure', receipt: actual })
    expect(sent.at(-1)?.[6]).toBe('S')
    journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '19', '1', '-'])
    await settled
  })
  it('rejects relabeling an actual assertion deadline as an observed mismatch', async () => {
    const { journal, sent, admitted } = fixture(),
      error = new Error('original assertion deadline')
    const lease = await admitted,
      actual = receipt(error, true)
    expect(() => lease.settle({ kind: 'softFailure', receipt: actual })).toThrow(TypeError)
    expect(sent.filter((fields) => fields[0] === 'SOFT_SETTLED')).toEqual([])
    const settled = lease.settle({ kind: 'assertionDeadline', receipt: actual })
    journal.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '19', '1', '-'])
    await settled
    expect(journal.errors(owner)[0]).toBe(error)
  })
  it('retains physical work until rejected native late/canceled ACK and adds no accepted Error', async () => {
    const { journal, admitted } = fixture(),
      error = new Error('actual original source deadline')
    const lease = await admitted
    journal.packet(['SOFT_ABORT', '1', '0', '1', '1', '19', wire('actual callback deadline')])
    const settled = lease.settle({ kind: 'assertionDeadline', receipt: receipt(error, true) })
    const rejected = expect(settled).rejects.toThrow('actual native late receipt')
    expect(journal.physical).toBe(1)
    journal.packet([
      'SOFT_SETTLED_ACK',
      '1',
      '0',
      '1',
      '1',
      '19',
      '0',
      wire('actual native late receipt'),
    ])
    await rejected
    expect(journal.errors(owner)).toEqual([])
    expect(journal.physical).toBe(0)
  })
})
