// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeWorkerIdleCallbacks } from '../src/native-runner-idle.js'
import { NativeSoftAssertionCallbacks } from '../src/native-runner-soft-assertions.js'

// Controlled wire receipts: these exercise SDK bookkeeping, not a browser matcher.
describe('native worker idle with genuine soft callback bookkeeping', () => {
  it('waits native admission, settlement ACK and joint callback/context retirement', async () => {
    const sent: (string | number)[][] = []
    const send = (...fields: (string | number)[]) => {
      sent.push(fields)
      return Promise.resolve()
    }
    const errors: Error[] = []
    const soft = new NativeSoftAssertionCallbacks(
      send,
      (error) => errors.push(error),
      () => idle.settled(),
      () => 'controlled error',
      () => '0',
      { workerIndex: 7, pid: 101 },
    )
    const idle = new NativeWorkerIdleCallbacks(
      send,
      { workerIndex: 7, pid: 101, fileId: () => '1' },
      () => ({
        ready:
          soft.idleReady &&
          soft.physical === 0 &&
          soft.contextCount === 0 &&
          soft.retirementPending === 0,
        workerFixtures: 2,
      }),
      (error) => errors.push(error),
    )
    const owner = { token: '0', scope: '1', generation: '1' }
    soft.packet(['SOFT_CONTEXT', '0', '1', '1', '1', '1', '7', '101', 'body'])
    const admitted = soft.run(
      owner,
      () => true,
      () => soft.begin(),
    )
    let released = false
    soft.retireCallback(owner, () => {
      released = true
    })
    idle.packet(['WORKER_IDLE', '1', '3', '4', '5', '6', '1', '2'])
    const idlePackets = () => sent.filter((packet) => packet[0] === 'WORKER_IDLE_READY')
    expect([soft.physical, soft.contextCount, soft.retirementPending]).toEqual([1, 1, 1])
    expect(idlePackets()).toEqual([])
    soft.packet(['SOFT_ADMITTED', '1', '0', '1', '1', '19', '1', '27', '-'])
    const lease = await admitted
    const settled = lease.settle({ kind: 'passed' })
    expect(soft.physical).toBe(1)
    expect(idlePackets()).toEqual([])
    expect(() => soft.packet(['SOFT_CONTEXT_RETIRED', '0', '1', '1'])).toThrow(
      'before actual matcher ACKs',
    )
    soft.packet(['SOFT_SETTLED_ACK', '1', '0', '1', '1', '19', '1', '-'])
    await settled
    expect([soft.physical, soft.contextCount, soft.retirementPending]).toEqual([0, 1, 1])
    expect(released).toBe(false)
    expect(idlePackets()).toEqual([])
    soft.packet(['SOFT_CONTEXT_RETIRED', '0', '1', '1'])
    expect(released).toBe(true)
    expect(soft.idleReady).toBe(true)
    expect(idlePackets()).toEqual([['WORKER_IDLE_READY', '1', '3', '4', '5', '6', '1', 2, 7, 101]])
    expect(errors).toEqual([])
  })
})
