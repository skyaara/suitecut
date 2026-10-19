import { describe, expect, it } from 'vitest'

import { emulateNativeIdleState, type NativeIdleState } from '../src/native-idle.js'
import { type UntrustedInput } from '../src/untrusted.js'

describe('native idle state forwarding', () => {
  it('forwards exact booleans including false without implementing browser state', async () => {
    const calls: Record<string, UntrustedInput>[] = []
    for (const isUserActive of [false, true])
      for (const isScreenUnlocked of [false, true]) {
        await emulateNativeIdleState(
          (value) => {
            calls.push(value)
            return Promise.resolve({})
          },
          { isUserActive, isScreenUnlocked },
        )
        expect(calls.at(-1)).toEqual({
          action: 'emulateIdleState',
          state: { isUserActive, isScreenUnlocked },
        })
      }
  })
  it('uses native clear only for undefined', async () => {
    let actual: UntrustedInput
    await emulateNativeIdleState((value) => {
      actual = value
      return Promise.resolve({})
    })
    expect(actual).toEqual({ action: 'emulateIdleState', state: null })
  })
  it.each([
    null,
    {},
    { isUserActive: true },
    { isUserActive: 0, isScreenUnlocked: false },
    { isUserActive: true, isScreenUnlocked: true, unexpected: true },
  ])('rejects malformed state before native admission: %j', async (value) => {
    let called = false
    await expect(
      emulateNativeIdleState(() => {
        called = true
        return Promise.resolve({})
      }, value as NativeIdleState),
    ).rejects.toThrow()
    expect(called).toBe(false)
  })
  it('propagates actual native rejection', async () => {
    const failure = new Error('Actual native emulation command failed')
    await expect(
      emulateNativeIdleState(() => Promise.reject(failure), {
        isUserActive: true,
        isScreenUnlocked: false,
      }),
    ).rejects.toBe(failure)
  })
})
