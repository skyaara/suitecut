import { describe, expect, it } from 'vitest'

import {
  invokeNativeRouteCallback,
  nativeRouteCallbackPhysicalCount,
  type NativeRouteCallbackTransport,
} from '../src/native-route-callback.js'

function transport() {
  const page = new AbortController(),
    process = new AbortController(),
    acknowledgments: string[] = []
  const source: NativeRouteCallbackTransport = {
    signal: page.signal,
    processSignal: process.signal,
    start: () => {
      acknowledgments.push('start')
      return Promise.resolve()
    },
    finish: () => {
      acknowledgments.push('finish')
      return Promise.resolve()
    },
  }
  return { source, page, process, acknowledgments }
}
describe('physical authored route callback leases', () => {
  it('waits for genuine native admission before invoking author code', async () => {
    const f = transport()
    const value = await invokeNativeRouteCallback(
      f.source,
      { invocation: '1', token: '1', scope: '1' },
      () => {
        expect(f.acknowledgments).toEqual(['start'])
        return 'actual'
      },
    )
    expect(value).toBe('actual')
    expect(f.acknowledgments).toEqual(['start', 'finish'])
    expect(nativeRouteCallbackPhysicalCount(f.process.signal)).toBe(0)
  })
  it('retains all 128 externally held unresolved closures after their page closes', async () => {
    const f = transport(),
      resolvers: (() => void)[] = [],
      calls: Promise<void>[] = []
    for (let index = 1; index <= 128; index++)
      calls.push(
        invokeNativeRouteCallback(
          f.source,
          { invocation: String(index), token: String(index), scope: '1' },
          () => new Promise<void>((resolve) => resolvers.push(resolve)),
        ),
      )
    await Promise.resolve()
    expect(resolvers).toHaveLength(128)
    f.page.abort(new Error('actual page closed'))
    expect(nativeRouteCallbackPhysicalCount(f.process.signal)).toBe(128)
    let overflowCalled = false
    await expect(
      invokeNativeRouteCallback(f.source, { invocation: '129', token: '129', scope: '1' }, () => {
        overflowCalled = true
      }),
    ).rejects.toThrow('capacity')
    expect(overflowCalled).toBe(false)
    expect(nativeRouteCallbackPhysicalCount(f.process.signal)).toBe(128)
    for (const resolve of resolvers) resolve()
    await Promise.all(calls)
    expect(nativeRouteCallbackPhysicalCount(f.process.signal)).toBe(0)
  })
  it('releases an issued lease without invoking author code when its scope closes during admission', async () => {
    const f = transport()
    let admit: (() => void) | undefined,
      called = false
    f.source.start = () =>
      new Promise<void>((resolve) => {
        admit = resolve
      })
    const result = invokeNativeRouteCallback(
      f.source,
      { invocation: '1', token: '1', scope: '1' },
      () => {
        called = true
      },
    )
    f.page.abort(new Error('native scope closed'))
    if (!admit) throw new Error('Expected actual pending native admission')
    admit()
    await expect(result).rejects.toThrow('scope closed')
    expect(called).toBe(false)
    expect(f.acknowledgments).toEqual(['finish'])
    expect(nativeRouteCallbackPhysicalCount(f.process.signal)).toBe(0)
  })
})
