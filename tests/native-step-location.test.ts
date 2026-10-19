// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import * as util from 'node:util'

import { describe, expect, it } from 'vitest'

import { NativeStepCallbacks } from '../src/native-runner-steps.js'

const owner = { token: '0', scope: '1', generation: '1' }
function bridge() {
  const sent: (string | number)[][] = []
  const callbacks = new NativeStepCallbacks(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    (error) => {
      throw error
    },
    () => undefined,
  )
  return { sent, callbacks, step: callbacks.bind(owner, () => true) }
}
describe('native admitted step location metadata', () => {
  it('snapshots exact explicit UTF16 metadata before native admission', async () => {
    const b = bridge(),
      location = { file: 'custom\ud800\0<&.ts', line: 123, column: 45 }
    let invoked = false
    const result = b.step(
      'custom',
      () => {
        invoked = true
        return 17
      },
      { location },
    )
    location.line = 1
    expect(invoked).toBe(false)
    expect(b.sent[0]?.slice(8)).toEqual([
      Buffer.from('custom\ud800\0<&.ts', 'utf16le').toString('hex'),
      123,
      45,
      2,
    ])
    b.callbacks.handle(['STEP_ADMITTED', '1', '0', '1', '1', '7', '1', '-'])
    await Promise.resolve()
    b.callbacks.handle(['STEP_RESULT', '7', '0', '1', '1', 'passed', '-'])
    b.callbacks.handle(['STEP_RECEIVED', '7', '0', '1', '1'])
    await expect(result).resolves.toBe(17)
  })
  it('rejects invalid location values before any native request or callback', () => {
    const b = bridge()
    for (const location of [
      { file: '', line: 1, column: 1 },
      { file: 'a', line: 0, column: 1 },
      { file: 'a', line: 1, column: NaN },
      { file: 'a', line: 2147483648, column: 1 },
      { file: 'a', line: 1, column: 1.5 },
    ])
      expect(() =>
        b.step(
          'invalid',
          () => {
            throw Error('must not run')
          },
          { location },
        ),
      ).toThrow()
    expect(b.sent).toEqual([])
    expect(b.callbacks.pending).toBe(0)
  })
  it('uses a genuine caller file rather than an internal SDK frame when available', async () => {
    const b = bridge()
    const result = b.step.skip('omitted', () => {
      throw Error('must not run')
    })
    if (typeof util.getCallSites === 'function') {
      expect(b.sent[0]?.[11]).toBe(1)
      expect(Buffer.from(String(b.sent[0]?.[8]), 'hex').toString('utf16le')).toContain(
        'native-step-location.test.ts',
      )
      expect(Number(b.sent[0]?.[9])).toBeGreaterThan(1)
    } else expect(b.sent[0]?.slice(8)).toEqual(['-', 0, 0, 0])
    b.callbacks.handle(['STEP_ADMITTED_NOT_INVOKED', '1', '0', '1', '1', '7', '1', '-'])
    await expect(result).resolves.toBeUndefined()
  })
  it('preserves author-declared metadata for intentionally omitted callbacks', async () => {
    const b = bridge()
    const result = b.step.skip(
      'omitted',
      () => {
        throw Error('must not run')
      },
      { location: { file: 'dummy-file.ts', line: 123, column: 45 } },
    )
    expect(b.sent[0]?.slice(8)).toEqual([
      Buffer.from('dummy-file.ts', 'utf16le').toString('hex'),
      123,
      45,
      2,
    ])
    b.callbacks.handle(['STEP_ADMITTED_NOT_INVOKED', '1', '0', '1', '1', '9', '1', '-'])
    await expect(result).resolves.toBeUndefined()
  })
  it('rejects combined metadata beyond the actual native control line before consuming a request', async () => {
    const b = bridge()
    expect(() =>
      b.step('\0'.repeat(4096), () => undefined, {
        location: { file: 'x'.repeat(4096), line: 1, column: 1 },
      }),
    ).toThrow('control budget')
    expect(b.sent).toEqual([])
    expect(b.callbacks.pending).toBe(0)
    const result = b.step.skip('next valid request', () => undefined)
    expect(b.sent[0]?.[1]).toBe('1')
    b.callbacks.handle(['STEP_ADMITTED_NOT_INVOKED', '1', '0', '1', '1', '7', '1', '-'])
    await expect(result).resolves.toBeUndefined()
  })
  it('rejects invalid boxed attribution types before native admission', () => {
    const b = bridge()
    expect(() => {
      Reflect.apply(b.step, undefined, ['unsupported', () => 1, { box: 'true' }])
    }).toThrow()
    expect(b.sent).toEqual([])
  })
})
