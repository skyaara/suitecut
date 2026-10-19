// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import * as util from 'node:util'

import { expect, it } from 'vitest'

import { NativeStepCallbacks } from '../src/native-runner-steps.js'

it('pins captured boxed frames separately from explicit locations and preserves the authored rejection', async () => {
  const packets: (string | number)[][] = []
  const bridge = new NativeStepCallbacks(
    (...fields) => {
      packets.push(fields)
      return Promise.resolve()
    },
    (error) => {
      throw error
    },
    () => undefined,
  )
  const step = bridge.bind({ token: '0', scope: '1', generation: '1' }, () => true)
  const original = new Error('genuine authored error')
  const stack = original.stack
  const invoke = () =>
    step(
      'boxed',
      () => {
        throw original
      },
      { box: true, location: { file: 'author-declared.ts', line: 123, column: 45 } },
    )
  const result = invoke()
  const rejected = expect(result).rejects.toBe(original)
  expect(packets[0]?.slice(8, 13)).toEqual([
    Buffer.from('author-declared.ts', 'utf16le').toString('hex'),
    123,
    45,
    2,
    1,
  ])
  expect(packets[0]).toHaveLength(21)
  if (typeof util.getCallSites === 'function') {
    expect(packets[0]?.[16]).toBe(1)
    expect(packets[0]?.[20]).toBe(1)
    expect(Buffer.from(String(packets[0]?.[13]), 'hex').toString('utf16le')).toContain(
      'native-step-box.test.ts',
    )
    expect(Number(packets[0]?.[18])).not.toBe(Number(packets[0]?.[14]))
  }
  bridge.handle(['STEP_ADMITTED', '1', '0', '1', '1', '7', '1', '-'])
  await Promise.resolve()
  await Promise.resolve()
  bridge.handle([
    'STEP_RESULT',
    '7',
    '0',
    '1',
    '1',
    'failed',
    Buffer.from('actual admitted failure', 'utf16le').toString('hex'),
  ])
  bridge.handle(['STEP_RECEIVED', '7', '0', '1', '1'])
  await rejected
  expect(original.stack).toBe(stack)
})

it('rejects invalid boxed option types before native requests or author callbacks', () => {
  const packets: (string | number)[][] = []
  let invoked = false
  const bridge = new NativeStepCallbacks(
    (...fields) => {
      packets.push(fields)
      return Promise.resolve()
    },
    (error) => {
      throw error
    },
    () => undefined,
  )
  const step = bridge.bind({ token: '0', scope: '1', generation: '1' }, () => true)
  for (const box of ['true', 1, null, {}])
    expect(
      () =>
        void Reflect.apply(step, undefined, [
          'invalid',
          () => {
            invoked = true
          },
          { box },
        ]),
    ).toThrow()
  expect(packets).toEqual([])
  expect(invoked).toBe(false)
  expect(bridge.pending).toBe(0)
})
