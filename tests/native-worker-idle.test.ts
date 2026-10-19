// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeWorkerIdleCallbacks } from '../src/native-runner-idle.js'

const request = (nonce = '1'): string[] => ['WORKER_IDLE', '1', '3', '4', '5', '6', nonce, '2']
const setup = () => {
  const sent: (string | number)[][] = [],
    errors: Error[] = []
  let ready = false,
    workerFixtures = 2
  const idle = new NativeWorkerIdleCallbacks(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    { workerIndex: 7, pid: 101, fileId: () => '1' },
    () => ({ ready, workerFixtures }),
    (error) => {
      errors.push(error)
    },
  )
  return {
    idle,
    sent,
    errors,
    ready: () => {
      ready = true
    },
    fixtures: (n: number) => {
      workerFixtures = n
    },
  }
}
describe('native worker idle receipt marshalling', () => {
  it('waits actual SDK queue snapshot and echoes exact native ownership without choosing new work', () => {
    const f = setup()
    expect(f.idle.packet(request())).toBe(true)
    expect(f.sent).toEqual([])
    expect(() => f.idle.beforeAuthorAdmission()).toThrow('before idle')
    f.ready()
    f.idle.settled()
    expect(f.sent).toEqual([['WORKER_IDLE_READY', '1', '3', '4', '5', '6', '1', 2, 7, 101]])
    f.idle.beforeAuthorAdmission()
    f.idle.settled()
    expect(f.sent).toHaveLength(1)
  })
  it('rejects stale or duplicate native requests and preserves the actual pending one', () => {
    const f = setup()
    f.idle.packet(request())
    expect(() => f.idle.packet(request('2'))).toThrow('duplicate or stale')
    f.ready()
    f.idle.settled()
    expect(() => f.idle.packet(request())).toThrow('duplicate or stale')
    expect(f.idle.packet(request('2'))).toBe(true)
    expect(f.sent).toHaveLength(2)
  })
  it('requires exact retained worker fixture count after all local queues drain', () => {
    const f = setup()
    f.idle.packet(request())
    f.fixtures(1)
    f.ready()
    expect(() => f.idle.settled()).toThrow('fixture ownership mismatch')
    expect(f.sent).toEqual([])
    expect(() => f.idle.beforeAuthorAdmission()).toThrow()
  })
  it('validates exact native file and bounded packet identities without mutating the accepted nonce', () => {
    const f = setup()
    const foreign = request()
    foreign[1] = '2'
    expect(() => f.idle.packet(foreign)).toThrow()
    for (const index of [1, 2, 3, 4, 5, 6]) {
      const bad = request()
      bad[index] = '0'
      expect(() => f.idle.packet(bad)).toThrow()
    }
    for (const count of ['65', '-1', '0x2', '2.0', '']) {
      const bad = request()
      bad[7] = count
      expect(() => f.idle.packet(bad)).toThrow()
    }
    expect(() => f.idle.packet([...request(), 'extra'])).toThrow()
    expect(f.idle.packet(['OTHER'])).toBe(false)
    f.ready()
    expect(f.idle.packet(request())).toBe(true)
    expect(f.sent).toHaveLength(1)
  })
  it('surfaces actual receipt write rejection through the fatal channel', async () => {
    const failure = new Error('real channel rejected'),
      errors: Error[] = []
    const idle = new NativeWorkerIdleCallbacks(
      () => Promise.reject(failure),
      { workerIndex: 1, pid: 101, fileId: () => '1' },
      () => ({ ready: true, workerFixtures: 2 }),
      (error) => {
        errors.push(error)
      },
    )
    idle.packet(request())
    await Promise.resolve()
    expect(errors).toEqual([failure])
  })
})
