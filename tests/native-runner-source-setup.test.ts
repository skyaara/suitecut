// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  NativeSourceSetupCallbacks,
  prepareNativeFixtureSource,
  type NativeSourceSetupSelection,
  type NativeSourceLaunchAdmission,
} from '../src/native-runner-source-setup.js'

const selected = (token = '7'): NativeSourceSetupSelection => ({
  catalog: '3',
  declaration: '11',
  fixtureKey: 'nativeFixture11',
  token,
  owner: '0',
  test: '5',
  generation: '13',
  scope: 'worker',
  origin: 'callback',
})
const fixture = () => {
  const requests: { selection: NativeSourceSetupSelection; executable: string }[] = [],
    startups: string[] = []
  const callbacks = new NativeSourceSetupCallbacks({
    cancel: () => Promise.resolve(),
    request: (selection, executable) => {
      requests.push({ selection, executable })
      return Promise.resolve({ kind: 'registered', ticket: '17', descriptor: 4, cookie: '19' })
    },
    startup: (_selection, ticket) => {
      startups.push(ticket)
      return Promise.resolve()
    },
  })
  return { callbacks, requests, startups }
}
describe('private native Source setup marshalling prerequisites', () => {
  it('forwards exact selected native version rather than public root alias', async () => {
    const f = fixture()
    await f.callbacks.invoke(selected(), async (beforeUse) => {
      const admission = await prepareNativeFixtureSource('/actual/native/helper')
      expect(f.requests).toEqual([{ selection: selected(), executable: '/actual/native/helper' }])
      expect(admission?.descriptor).toBe(4)
      await admission?.startup()
      await admission?.startup()
      expect(f.startups).toEqual(['17'])
      beforeUse()
      expect(await prepareNativeFixtureSource('/later')).toBeUndefined()
    })
    expect(f.callbacks.idleReady).toBe(true)
  })
  it('does not parse a flat authored key as a layered declaration identity', async () => {
    const f = fixture(),
      selection = { ...selected(), fixtureKey: 'nativeFixture999', declaration: '2' }
    await f.callbacks.invoke(selection, async (beforeUse) => {
      await prepareNativeFixtureSource('/native')
      beforeUse()
    })
    expect(f.requests[0]?.selection).toEqual(selection)
  })
  it('does not supply an endpoint outside setup or from inherited post-use work', async () => {
    const f = fixture()
    expect(await prepareNativeFixtureSource('/outside')).toBeUndefined()
    let later: (() => Promise<NativeSourceLaunchAdmission | undefined>) | undefined
    await f.callbacks.invoke(selected(), (beforeUse) => {
      later = () => prepareNativeFixtureSource('/later')
      beforeUse()
      return later()
    })
    expect(await later?.()).toBeUndefined()
    expect(f.requests).toEqual([])
  })
  it('leaves fresh-only/native rejection authoritative without fabricating startup', async () => {
    const failure = new Error('actual native registration rejection')
    const fresh = new NativeSourceSetupCallbacks({
      cancel: () => Promise.resolve(),
      request: () => Promise.resolve({ kind: 'freshOnly' }),
      startup: () => {
        throw new Error('must not start')
      },
    })
    await fresh.invoke(selected(), async () => {
      expect(await prepareNativeFixtureSource('/native')).toBeUndefined()
    })
    const rejected = new NativeSourceSetupCallbacks({
      cancel: () => Promise.resolve(),
      request: () => Promise.reject(failure),
      startup: () => Promise.resolve(),
    })
    await expect(
      rejected.invoke(selected(), () => prepareNativeFixtureSource('/native')),
    ).rejects.toBe(failure)
    expect(rejected.idleReady).toBe(true)
  })
  it('preserves genuine raw callback return and error identity', () => {
    const f = fixture(),
      value = {},
      failure = new Error('actual setup throw')
    expect(f.callbacks.invoke(selected(), () => value)).toBe(value)
    expect(() =>
      f.callbacks.invoke(selected('8'), () => {
        throw failure
      }),
    ).toThrow(failure)
    expect(f.callbacks.close('8', '13')).toBe(false)
  })
  it('requires exact generation to close and retains pending native request across cancellation', async () => {
    let release: (() => void) | undefined
    const callbacks = new NativeSourceSetupCallbacks({
      cancel: () => Promise.resolve(),
      request: () =>
        new Promise((resolve) => {
          release = () => resolve({ kind: 'freshOnly' })
        }),
      startup: () => Promise.resolve(),
    })
    const pending = callbacks.invoke(selected(), () => prepareNativeFixtureSource('/native'))
    expect(callbacks.close('7', '12')).toBe(false)
    expect(callbacks.close('7', '13')).toBe(true)
    expect(callbacks.idleReady).toBe(false)
    release?.()
    await pending
    expect(callbacks.idleReady).toBe(true)
  })
  it('keeps an accepted registration charged until its actual startup acknowledgment', async () => {
    const f = fixture()
    const admission = await f.callbacks.invoke(selected(), async (beforeUse) => {
      const result = await prepareNativeFixtureSource('/native')
      beforeUse()
      return result
    })
    expect(f.callbacks.idleReady).toBe(false)
    await admission?.startup()
    expect(f.callbacks.idleReady).toBe(true)
  })
  it('does not wrap a literal setup as an authored registration callback', () => {
    const f = fixture(),
      literal = selected()
    Reflect.set(literal, 'origin', 'declaredLiteral')
    expect(() => f.callbacks.invoke(literal, () => undefined)).toThrow('selected worker callback')
    expect(f.requests).toEqual([])
  })
  it('waits genuine cancellation admission when a registered response arrives after use', async () => {
    let registered: (() => void) | undefined, cancelled: (() => void) | undefined
    const cancellations: string[] = []
    const callbacks = new NativeSourceSetupCallbacks({
      request: () =>
        new Promise((resolve) => {
          registered = () =>
            resolve({ kind: 'registered', ticket: '17', descriptor: 4, cookie: '19' })
        }),
      startup: () => {
        throw new Error('closed setup must not expose startup')
      },
      cancel: (selection, ticket) => {
        cancellations.push(selection.token + ':' + ticket)
        return new Promise((resolve) => {
          cancelled = resolve
        })
      },
    })
    const pending = callbacks.invoke(selected(), (beforeUse) => {
      const result = prepareNativeFixtureSource('/native')
      beforeUse()
      return result
    })
    registered?.()
    await Promise.resolve()
    expect(cancellations).toEqual(['7:17'])
    expect(callbacks.idleReady).toBe(false)
    cancelled?.()
    expect(await pending).toBeUndefined()
    expect(callbacks.idleReady).toBe(true)
  })
  it('retains cancellation ACK bookkeeping after an already settled startup', async () => {
    let cancel: (() => void) | undefined
    const callbacks = new NativeSourceSetupCallbacks({
      request: () =>
        Promise.resolve({ kind: 'registered', ticket: '17', descriptor: 4, cookie: '19' }),
      startup: () => Promise.resolve(),
      cancel: () =>
        new Promise((resolve) => {
          cancel = resolve
        }),
    })
    await callbacks.invoke(selected(), async (beforeUse) => {
      const admission = await prepareNativeFixtureSource('/native')
      await admission?.startup()
      beforeUse()
      const cancelled = admission?.cancel()
      await Promise.resolve()
      expect(callbacks.idleReady).toBe(false)
      cancel?.()
      await cancelled
    })
    expect(callbacks.idleReady).toBe(true)
  })
  it('keeps simultaneous actual selected callbacks separate', async () => {
    const f = fixture()
    await Promise.all(
      ['7', '8'].map((token) =>
        f.callbacks.invoke(selected(token), async (beforeUse) => {
          await Promise.resolve()
          const admission = await prepareNativeFixtureSource('/native/' + token)
          await admission?.startup()
          beforeUse()
        }),
      ),
    )
    expect(f.requests.map((request) => request.selection.token).sort()).toEqual(['7', '8'])
    expect(f.callbacks.idleReady).toBe(true)
  })
})
