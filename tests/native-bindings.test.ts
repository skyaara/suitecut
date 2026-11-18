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
  it('retains physical slots after logical cancellation and admits calls past the old 128 cap', async () => {
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
    call('admitted')
    await Promise.resolve()
    expect(invoked).toBe(129)
    expect(bindings.diagnostics()).toMatchObject({ logical: 1, physical: 129 })
    expect(replies.filter((reply) => reply.action === 'resolve')).toHaveLength(0)
    for (const resolve of complete) resolve(1)
    await expect
      .poll(() => bindings.diagnostics())
      .toMatchObject({ logical: 0, physical: 0, physicalBytes: 0 })
    await expect.poll(() => replies.filter((reply) => reply.action === 'resolve')).toHaveLength(1)
    expect(replies.find((reply) => reply.action === 'resolve')).toMatchObject({
      bindingCallId: 'admitted',
      success: true,
    })
  })
  it('admits physical argument bytes past the old 8 MiB budget', async () => {
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
    expect(invoked).toBe(10)
    expect(bindings.diagnostics()).toMatchObject({ logical: 0, physical: 10 })
    expect(bindings.diagnostics().physicalBytes).toBeGreaterThan(8 * 1024 * 1024)
    expect(replies.filter((reply) => reply.action === 'resolve')).toHaveLength(0)
    for (const resolve of complete) resolve(1)
    await expect
      .poll(() => NativeBindings.hostDiagnostics())
      .toEqual({ physical: 0, physicalBytes: 0 })
  })
  it('counts raw host work across canceled sources without blocking another source', async () => {
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
    second.call('source-two-admitted')
    await expect.poll(() => invoked).toBe(1)
    await expect
      .poll(() => second.replies.find((reply) => reply.action === 'resolve'))
      .toMatchObject({ bindingCallId: 'source-two-admitted', success: true })
    for (const resolve of complete) resolve(1)
    await expect
      .poll(() => NativeBindings.hostDiagnostics())
      .toEqual({ physical: 0, physicalBytes: 0 })
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
