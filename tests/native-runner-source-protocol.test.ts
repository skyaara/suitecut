// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeSourceSetupProtocol } from '../src/native-runner-source-protocol.js'
import { prepareNativeFixtureSource } from '../src/native-runner-source-setup.js'

const setup = ['SOURCE_SETUP', '3', '11', '7', '5', '5', '13', 'nativeFixture11']
const fixture = () => {
  const sent: (string | number)[][] = []
  const protocol = new NativeSourceSetupProtocol(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    () => undefined,
  )
  return { sent, protocol }
}
describe('native Source callback/receipt protocol', () => {
  it('marshals actual selected setup, registration and authenticated-startup ACK separately', async () => {
    const f = fixture()
    expect(f.protocol.packet(setup)).toBe(true)
    const selection = f.protocol.take('7', '5', '13', 'nativeFixture11', 'worker', false)
    expect(selection).toBeDefined()
    if (!selection) throw new Error('missing actual selection')
    await f.protocol.callbacks.invoke(selection, async (beforeUse) => {
      const pending = prepareNativeFixtureSource('/native/helper')
      expect(f.sent).toEqual([
        [
          'SOURCE_REGISTER',
          '1',
          '3',
          '11',
          '7',
          '5',
          '5',
          '13',
          'nativeFixture11',
          '2f6e61746976652f68656c706572',
        ],
      ])
      f.protocol.packet(['SOURCE_REGISTERED', '1', '7', '5', '13', '1', '17', '4', '19'])
      const admission = await pending
      expect(admission?.launchAllowed()).toBe(true)
      const startup = admission?.startup()
      await Promise.resolve()
      expect(f.sent[1]).toEqual(['SOURCE_WAIT', '2', '17', '7', '5', '13'])
      expect(f.protocol.idleReady).toBe(false)
      f.protocol.packet(['SOURCE_STARTED', '2', '17', '7', '5', '13', '1', '101', '999'])
      await startup
      beforeUse()
    })
    expect(f.protocol.idleReady).toBe(true)
  })
  it('rejects mismatched native version/constant setup without consuming genuine sidecar', () => {
    const f = fixture()
    f.protocol.packet(setup)
    for (const key of ['authorAlias', 'nativeFixture12'])
      expect(() => f.protocol.take('7', '5', '13', key, 'worker', false)).toThrow('differs')
    expect(() => f.protocol.take('7', '5', '13', 'nativeFixture11', 'worker', true)).toThrow(
      'differs',
    )
    expect(f.protocol.take('7', '5', '13', 'nativeFixture11', 'worker', false)?.declaration).toBe(
      '11',
    )
  })
  it('rejects foreign/replayed acknowledgment and preserves actual pending registration', async () => {
    const f = fixture()
    f.protocol.packet(setup)
    const selection = f.protocol.take('7', '5', '13', 'nativeFixture11', 'worker', false)
    if (!selection) throw new Error('missing actual selection')
    await f.protocol.callbacks.invoke(selection, async (beforeUse) => {
      const pending = prepareNativeFixtureSource('/native/helper')
      expect(() =>
        f.protocol.packet(['SOURCE_REGISTERED', '1', '8', '5', '13', '1', '17', '4', '19']),
      ).toThrow('differs')
      expect(() =>
        f.protocol.packet(['SOURCE_REGISTERED', '2', '7', '5', '13', '1', '17', '4', '19']),
      ).toThrow('no actual')
      f.protocol.packet(['SOURCE_REGISTERED', '1', '7', '5', '13', '0', '0', '0', '0'])
      expect(await pending).toBeUndefined()
      beforeUse()
      expect(() =>
        f.protocol.packet(['SOURCE_REGISTERED', '1', '7', '5', '13', '0', '0', '0', '0']),
      ).toThrow('no actual')
    })
  })
  it('retains cancellation request until native admitted-cancel ACK when setup closes', async () => {
    const f = fixture()
    f.protocol.packet(setup)
    const selection = f.protocol.take('7', '5', '13', 'nativeFixture11', 'worker', false)
    if (!selection) throw new Error('missing actual selection')
    await f.protocol.callbacks.invoke(selection, async (beforeUse) => {
      const pending = prepareNativeFixtureSource('/native/helper')
      beforeUse()
      f.protocol.packet(['SOURCE_REGISTERED', '1', '7', '5', '13', '1', '17', '4', '19'])
      await Promise.resolve()
      expect(f.sent[1]).toEqual(['SOURCE_CANCEL', '2', '17', '7', '5', '13'])
      expect(f.protocol.idleReady).toBe(false)
      expect(() => f.protocol.packet(['SOURCE_CANCELLED', '2', '17', '7', '5', '14'])).toThrow(
        'differs',
      )
      f.protocol.packet(['SOURCE_CANCELLED', '2', '17', '7', '5', '13'])
      expect(await pending).toBeUndefined()
    })
    expect(f.protocol.idleReady).toBe(true)
  })
  it('surfaces a genuine native admission denial instead of offering fresh fallback', async () => {
    const f = fixture()
    f.protocol.packet(setup)
    const selection = f.protocol.take('7', '5', '13', 'nativeFixture11', 'worker', false)
    if (!selection) throw new Error('missing actual selection')
    const pending = f.protocol.callbacks.invoke(selection, () =>
      prepareNativeFixtureSource('/native/helper'),
    )
    f.protocol.packet(['SOURCE_REGISTERED', '1', '7', '5', '13', '2', '0', '0', '0'])
    await expect(pending).rejects.toThrow('original deadline')
    expect(f.protocol.idleReady).toBe(true)
  })
  it('checks callback is still open at the final synchronous launch boundary', async () => {
    const f = fixture()
    f.protocol.packet(setup)
    const selection = f.protocol.take('7', '5', '13', 'nativeFixture11', 'worker', false)
    if (!selection) throw new Error('missing actual selection')
    await f.protocol.callbacks.invoke(selection, async (beforeUse) => {
      const pending = prepareNativeFixtureSource('/native/helper')
      f.protocol.packet(['SOURCE_REGISTERED', '1', '7', '5', '13', '1', '17', '4', '19'])
      const admission = await pending
      beforeUse()
      expect(admission?.launchAllowed()).toBe(false)
      const cancelled = admission?.cancel()
      await Promise.resolve()
      f.protocol.packet(['SOURCE_CANCELLED', '2', '17', '7', '5', '13'])
      await cancelled
    })
    expect(f.protocol.idleReady).toBe(true)
  })
})
