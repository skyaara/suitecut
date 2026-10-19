// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  NativeBindings,
  type NativeBindingEvent,
  type NativeBindingTransport,
} from '../src/native-bindings.js'
import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const controller = new AbortController()
  let listener: ((event: NativeBindingEvent) => void) | undefined
  const replies: Record<string, UntrustedInput>[] = []
  const transport: NativeBindingTransport = {
    signal: controller.signal,
    run: (params) => {
      replies.push(params)
      return Promise.resolve({})
    },
    subscribe: (next) => {
      listener = next
      return () => {
        listener = undefined
      }
    },
  }
  const bindings = new NativeBindings(
    transport,
    () => {
      throw new Error('Unexpected source lookup')
    },
    () => undefined,
  )
  const call = (id: string, value: UntrustedInput = 1) =>
    listener?.({
      event: 'call',
      id,
      name: 'callback',
      frame: { id: 'main', parentId: '', name: '', url: 'about:blank' },
      args: createNativeValueCodec().encode([value]),
    })
  const cancel = (id: string) =>
    listener?.({ event: 'canceled', id, error: 'Native frame detached' })
  return { bindings, controller, replies, call, cancel }
}

describe('native binding host callback ownership', () => {
  it('retains bounded physical slots after logical cancellation until host work settles', async () => {
    const { bindings, replies, call, cancel } = fixture()
    const complete: ((value: number) => void)[] = []
    let invoked = 0
    await bindings.expose(
      'callback',
      () =>
        new Promise<number>((resolve) => {
          invoked++
          complete.push(resolve)
        }),
    )
    for (let i = 0; i < 128; i++) {
      call(`call-${i}`)
      cancel(`call-${i}`)
    }
    expect(bindings.diagnostics()).toMatchObject({ logical: 0, physical: 128 })
    call('rejected')
    await Promise.resolve()
    expect(invoked).toBe(128)
    expect(replies.filter((reply) => reply.action === 'resolve')).toHaveLength(1)
    expect(replies.at(-1)).toMatchObject({ bindingCallId: 'rejected', success: false })
    for (const resolve of complete) resolve(1)
    await Promise.resolve()
    await Promise.resolve()
    expect(bindings.diagnostics()).toMatchObject({ logical: 0, physical: 0, physicalBytes: 0 })
    expect(replies.filter((reply) => reply.action === 'resolve')).toHaveLength(1)
  })
  it('bounds physical argument bytes even when callbacks never settle', async () => {
    const { bindings, call, cancel, replies } = fixture()
    let invoked = 0
    const complete: ((value: number) => void)[] = []
    await bindings.expose('callback', () => {
      invoked++
      return new Promise<number>((resolve) => {
        complete.push(resolve)
      })
    })
    for (let i = 0; i < 10; i++) {
      call(`bytes-${i}`, 'x'.repeat(1024 * 1024 - 100))
      cancel(`bytes-${i}`)
    }
    await Promise.resolve()
    expect(invoked).toBe(8)
    expect(bindings.diagnostics()).toMatchObject({ logical: 0, physical: 8 })
    expect(bindings.diagnostics().physicalBytes).toBeLessThanOrEqual(8 * 1024 * 1024)
    expect(replies.filter((reply) => reply.action === 'resolve')).toHaveLength(2)
    for (const resolve of complete) resolve(1)
    await Promise.resolve()
    await Promise.resolve()
    expect(NativeBindings.hostDiagnostics()).toEqual({ physical: 0, physicalBytes: 0 })
  })
  it('holds the process quota across canceled sources until raw host work settles', async () => {
    const first = fixture(),
      second = fixture()
    const complete: ((value: number) => void)[] = []
    await first.bindings.expose(
      'callback',
      () =>
        new Promise<number>((resolve) => {
          complete.push(resolve)
        }),
    )
    for (let i = 0; i < 128; i++) first.call(`source-one-${i}`)
    first.controller.abort(new Error('Native helper closed'))
    expect(NativeBindings.hostDiagnostics().physical).toBe(128)
    let invoked = 0
    await second.bindings.expose('callback', () => {
      invoked++
      return 42
    })
    second.call('source-two-blocked')
    await Promise.resolve()
    expect(invoked).toBe(0)
    expect(second.replies.at(-1)).toMatchObject({
      bindingCallId: 'source-two-blocked',
      success: false,
    })
    for (const resolve of complete) resolve(1)
    await Promise.resolve()
    await Promise.resolve()
    expect(NativeBindings.hostDiagnostics()).toEqual({ physical: 0, physicalBytes: 0 })
    second.call('source-two-admitted')
    await expect
      .poll(() => NativeBindings.hostDiagnostics())
      .toEqual({ physical: 0, physicalBytes: 0 })
    expect(invoked).toBe(1)
  })
  it('preserves thrown null and prevents late replies after page closure', async () => {
    const { bindings, controller, call, replies } = fixture()
    let finish: (value: number) => void = () => undefined
    let sequence = 0
    await bindings.expose('callback', () => {
      // The public binding contract preserves arbitrary thrown values.
      // eslint-disable-next-line @typescript-eslint/only-throw-error
      if (sequence++ === 0) throw null
      return new Promise<number>((resolve) => {
        finish = resolve
      })
    })
    call('null')
    await Promise.resolve()
    await Promise.resolve()
    expect(replies.find((reply) => reply.bindingCallId === 'null')).toMatchObject({
      success: false,
      value: ['null'],
    })
    call('late')
    controller.abort(new Error('Native page is closed'))
    finish(42)
    await Promise.resolve()
    await Promise.resolve()
    expect(replies.find((reply) => reply.bindingCallId === 'late')).toBeUndefined()
    expect(bindings.diagnostics()).toEqual({
      logical: 0,
      physical: 0,
      physicalBytes: 0,
      registrations: 0,
    })
  })
})
