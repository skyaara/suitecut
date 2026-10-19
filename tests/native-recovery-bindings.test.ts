// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  NativeBindings,
  type NativeBindingEvent,
  type NativeBindingTransport,
} from '../src/native-bindings.js'
import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

describe('renderer epoch binding ownership', () => {
  it('preserves registrations while old canceled host closures retain their physical slots', async () => {
    let epoch = new AbortController()
    const page = new AbortController()
    let crash: (() => void) | undefined
    let receive: ((event: NativeBindingEvent) => void) | undefined
    let settleOld: (value: number) => void = () => undefined
    const replies: Record<string, UntrustedInput>[] = []
    const transport: NativeBindingTransport = {
      get signal() {
        return epoch.signal
      },
      pageSignal: page.signal,
      subscribeCrash: (listener) => {
        crash = listener
        return () => {
          crash = undefined
        }
      },
      subscribe: (listener) => {
        receive = listener
        return () => {
          receive = undefined
        }
      },
      run: (params) => {
        replies.push(params)
        return Promise.resolve({})
      },
    }
    const bindings = new NativeBindings(
      transport,
      () => {
        throw new Error('Unexpected frame lookup')
      },
      () => undefined,
    )
    await bindings.expose('retained', (value) =>
      value === 1
        ? new Promise<number>((resolve) => {
            settleOld = resolve
          })
        : 42,
    )
    const call = (id: string, value: number): void =>
      receive?.({
        event: 'call',
        id,
        name: 'retained',
        frame: { id: 'main', parentId: '', name: '', url: 'https://actual.test' },
        args: createNativeValueCodec().encode([value]),
      })
    call('old', 1)
    expect(bindings.diagnostics()).toMatchObject({ logical: 1, physical: 1, registrations: 1 })
    epoch.abort(new Error('Actual renderer failure'))
    crash?.()
    expect(bindings.diagnostics()).toMatchObject({ logical: 0, physical: 1, registrations: 1 })
    epoch = new AbortController()
    call('new', 2)
    await Promise.resolve()
    await Promise.resolve()
    await Promise.resolve()
    expect(replies.filter((reply) => reply.bindingCallId === 'new')).toHaveLength(1)
    settleOld(1)
    await Promise.resolve()
    await Promise.resolve()
    await Promise.resolve()
    expect(replies.filter((reply) => reply.bindingCallId === 'old')).toHaveLength(0)
    expect(bindings.diagnostics()).toMatchObject({ logical: 0, physical: 0, registrations: 1 })
    page.abort(new Error('Actual page close'))
    expect(bindings.diagnostics().registrations).toBe(0)
    expect(receive).toBeUndefined()
    expect(crash).toBeUndefined()
  })
})
