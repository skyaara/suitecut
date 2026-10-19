// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeSourceSetupProtocol } from '../src/native-runner-source-protocol.js'
import { prepareNativeFixtureSource } from '../src/native-runner-source-setup.js'
import { NativeSourceWorkCallbacks } from '../src/native-source-work.js'
import { type NativeWorkWatermark } from '../src/native-work-watermark.js'
import { type UntrustedInput } from '../src/untrusted.js'

const watermark: NativeWorkWatermark = {
  version: 1,
  type: 'workwatermark',
  bytes: 0,
  barrier: '3',
  readerInstance: '2',
  readerThrough: '13',
  readerCut: '4',
  writerInstance: '1',
  writerThrough: '17',
  writerCut: '8',
}
async function fixture() {
  const sent: (string | number)[][] = [],
    failures: Error[] = []
  let resolve!: (value: UntrustedInput) => void
  const protocol = new NativeSourceSetupProtocol(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    () => undefined,
    true,
  )
  protocol.packet(['SOURCE_SETUP', '3', '11', '7', '5', '5', '13', 'nativeFixture11'])
  const selection = protocol.take('7', '5', '13', 'nativeFixture11', 'worker', false)!
  let connection!: NativeSourceWorkCallbacks
  await protocol.callbacks.invoke(selection, async (beforeUse) => {
    const pending = prepareNativeFixtureSource('/native/helper')
    protocol.packet(['SOURCE_REGISTERED', '1', '7', '5', '13', '1', '17', '4', '19'])
    const admitted = await pending
    if (!admitted?.bindWork) throw new Error('Missing actual work capability')
    connection = new NativeSourceWorkCallbacks('17', {
      ordered: () =>
        new Promise((yes) => {
          resolve = yes
        }),
      observed: () => Promise.resolve(),
      failed: (_challenge, error) => failures.push(error),
      changed: () => undefined,
    })
    admitted.bindWork(connection)
    beforeUse()
  })
  const mark = { ...watermark, barrier: '3' }
  const begin = () =>
    protocol.packet(['SOURCE_WORK_BEGIN', '17', '3', '9', '11', '1000.125', 'callbackIteration'])
  const receipt = (command: string) =>
    protocol.packet([
      command,
      '17',
      mark.barrier,
      mark.readerInstance,
      mark.readerThrough,
      mark.readerCut,
      mark.writerInstance,
      mark.writerThrough,
      mark.writerCut,
    ])
  return {
    protocol,
    connection,
    sent,
    resolve: (value: UntrustedInput) => resolve(value),
    failures,
    begin,
    receipt,
    mark,
  }
}
describe('callback iteration real Source SDK receipts (controlled)', () => {
  it('holds READY until actual raw stdout result and native ACK both arrive', async () => {
    const f = await fixture()
    f.begin()
    f.receipt('SOURCE_WORK_EXPECT')
    f.connection.observe(f.mark)
    f.receipt('SOURCE_WORK_ACK')
    expect(f.sent.some((row) => row[0] === 'SOURCE_WORK_READY')).toBe(false)
    expect(() => f.connection.beforeAuthorAdmission()).not.toThrow()
    f.resolve({ acceptedWork: true })
    await Promise.resolve()
    expect(f.sent.filter((row) => row[0] === 'SOURCE_WORK_READY')).toEqual([
      ['SOURCE_WORK_READY', '17', '3'],
    ])
    f.protocol.packet(['SOURCE_WORK_RESUME', '17', '3', '12'])
    expect(f.sent.at(-1)).toEqual(['SOURCE_WORK_HOST_RESUMED', '17', '3', '12'])
    expect(f.connection.idleReady).toBe(true)
  })
  it('preserves early native RESUME until genuine rawreturn, then clears actual host challenge once', async () => {
    const f = await fixture()
    f.begin()
    f.receipt('SOURCE_WORK_EXPECT')
    f.connection.observe(f.mark)
    f.receipt('SOURCE_WORK_ACK')
    f.protocol.packet(['SOURCE_WORK_RESUME', '17', '3', '12'])
    expect(f.sent.some((row) => row[0] === 'SOURCE_WORK_HOST_RESUMED')).toBe(false)
    expect(f.connection.physical).toBe(1)
    expect(() => f.protocol.packet(['SOURCE_WORK_RESUME', '17', '3', '12'])).toThrow('replayed')
    f.resolve({ acceptedWork: true })
    await Promise.resolve()
    expect(f.sent.slice(-2)).toEqual([
      ['SOURCE_WORK_READY', '17', '3'],
      ['SOURCE_WORK_HOST_RESUMED', '17', '3', '12'],
    ])
    expect(() => f.protocol.packet(['SOURCE_WORK_RESUME', '17', '3', '13'])).toThrow(
      'exact completed',
    )
  })
  it('rawresult alone cannot publish READY or HOST_RESUMED', async () => {
    const f = await fixture()
    f.begin()
    f.resolve({ acceptedWork: true })
    await Promise.resolve()
    f.protocol.packet(['SOURCE_WORK_RESUME', '17', '3', '12'])
    expect(
      f.sent.some((row) => row[0] === 'SOURCE_WORK_READY' || row[0] === 'SOURCE_WORK_HOST_RESUMED'),
    ).toBe(false)
    f.receipt('SOURCE_WORK_EXPECT')
    f.connection.observe(f.mark)
    f.receipt('SOURCE_WORK_ACK')
    expect(f.sent.at(-1)).toEqual(['SOURCE_WORK_HOST_RESUMED', '17', '3', '12'])
  })
  it('keeps existing WorkerReuse gate and six-field protocol unchanged', async () => {
    const f = await fixture()
    f.protocol.packet(['SOURCE_WORK_BEGIN', '17', '3', '9', '11', '1000.125'])
    expect(() => f.connection.beforeAuthorAdmission()).toThrow('not resumed')
    f.receipt('SOURCE_WORK_EXPECT')
    f.connection.observe(f.mark)
    f.resolve({ acceptedWork: true })
    await Promise.resolve()
    f.receipt('SOURCE_WORK_ACK')
    expect(f.sent.some((row) => row[0] === 'SOURCE_WORK_READY')).toBe(false)
    f.protocol.packet(['SOURCE_WORK_RESUME', '17', '3', '12'])
    expect(f.sent.some((row) => row[0] === 'SOURCE_WORK_HOST_RESUMED')).toBe(false)
    expect(() => f.connection.beforeAuthorAdmission()).not.toThrow()
  })
})
