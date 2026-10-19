import { describe, expect, it, vi } from 'vitest'

import {
  nativeActionHookPhysicalCount,
  subscribeNativeActionHook,
} from '../src/native-action-hooks.js'
import { type NativeBrowser } from '../src/native-browser.js'
import { type NativeActionStage } from '../src/native-protocol.js'

function fixture() {
  let listener: ((stage: NativeActionStage) => void) | undefined
  const detach = vi.fn(() => {
    listener = undefined
  })
  const runAutomation = vi.fn(() => Promise.resolve({}))
  const lifecycle = new AbortController()
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: lifecycle.signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    onActionStage(callback) {
      listener = callback
      return detach
    },
  }
  return {
    source,
    detach,
    runAutomation,
    lifecycle,
    emit: (stage: NativeActionStage) => listener?.(stage),
  }
}

describe('native action-stage bridge', () => {
  it('waits for a real matching stage and acknowledges only after the callback completes', async () => {
    const live = fixture(),
      action = new AbortController()
    let finish: (() => void) | undefined
    const callback = vi.fn(
      () =>
        new Promise<void>((resolve) => {
          finish = resolve
        }),
    )
    const hook = subscribeNativeActionHook(live.source, callback, action)
    const stage = {
      clientToken: hook.clientToken,
      id: 42,
      generation: 7,
      token: 13,
      stage: 'beforeStable' as const,
    }
    live.emit({ ...stage, clientToken: 'another-action' })
    expect(callback).not.toHaveBeenCalled()
    live.emit(stage)
    await vi.waitFor(() => expect(callback).toHaveBeenCalledTimes(1))
    expect(live.runAutomation).not.toHaveBeenCalled()
    live.emit(stage)
    expect(callback).toHaveBeenCalledTimes(1)
    finish?.()
    await vi.waitFor(() =>
      expect(live.runAutomation).toHaveBeenCalledWith(
        'continueAction',
        {
          target: 42,
          generation: 7,
          token: 13,
          clientToken: hook.clientToken,
        },
        expect.any(AbortSignal),
      ),
    )
    hook.dispose()
    hook.dispose()
    expect(live.detach).toHaveBeenCalledTimes(1)
  })

  it('cancels the owner when the hook rejects', async () => {
    const live = fixture(),
      action = new AbortController(),
      error = new Error('hook failed')
    const hook = subscribeNativeActionHook(live.source, () => Promise.reject(error), action)
    live.emit({
      clientToken: hook.clientToken,
      id: 42,
      generation: 7,
      token: 13,
      stage: 'beforeStable',
    })
    await vi.waitFor(() => expect(action.signal.aborted).toBe(true))
    expect(action.signal.reason).toBe(error)
    expect(live.runAutomation).not.toHaveBeenCalled()
    hook.dispose()
  })

  it('disposes an unfinished hook without acknowledging or retaining its native subscription', async () => {
    const live = fixture(),
      action = new AbortController()
    let finish: (() => void) | undefined
    const callback = vi.fn(
      () =>
        new Promise<void>((resolve) => {
          finish = resolve
        }),
    )
    const hook = subscribeNativeActionHook(live.source, callback, action)
    const stage = {
      clientToken: hook.clientToken,
      id: 42,
      generation: 7,
      token: 13,
      stage: 'beforeStable' as const,
    }
    live.emit(stage)
    await vi.waitFor(() => expect(callback).toHaveBeenCalledTimes(1))
    hook.dispose()
    await Promise.resolve()
    await Promise.resolve()
    live.emit({ ...stage, token: 14 })
    expect(live.detach).toHaveBeenCalledTimes(1)
    expect(callback).toHaveBeenCalledTimes(1)
    expect(live.runAutomation).not.toHaveBeenCalled()
    expect(action.signal.aborted).toBe(false)
    expect(nativeActionHookPhysicalCount(live.source)).toBe(1)
    finish?.()
    await vi.waitFor(() => expect(nativeActionHookPhysicalCount(live.source)).toBe(0))
  })

  it('cancels the action if the native continuation rejects a stale identity', async () => {
    const live = fixture(),
      action = new AbortController(),
      error = new Error('Native action continuation is stale')
    live.runAutomation.mockRejectedValueOnce(error)
    const hook = subscribeNativeActionHook(live.source, () => undefined, action)
    live.emit({
      clientToken: hook.clientToken,
      id: 42,
      generation: 7,
      token: 13,
      stage: 'beforeStable',
    })
    await vi.waitFor(() => expect(action.signal.aborted).toBe(true))
    expect(action.signal.reason).toBe(error)
    hook.dispose()
  })

  it('retains a global physical quota through cancellation, close, and fresh sources until raw settlement', async () => {
    const live = fixture()
    const finish: (() => void)[] = []
    const hooks = Array.from({ length: 64 }, (_, index) => {
      const action = new AbortController()
      const hook = subscribeNativeActionHook(
        live.source,
        () => new Promise<void>((resolve) => finish.push(resolve)),
        action,
      )
      live.emit({
        clientToken: hook.clientToken,
        id: index + 1,
        generation: 1,
        token: index + 1,
        stage: 'beforeStable',
      })
      return hook
    })
    await vi.waitFor(() => expect(finish).toHaveLength(64))
    for (const hook of hooks) hook.dispose()
    live.lifecycle.abort(new Error('Native process closed'))
    expect(nativeActionHookPhysicalCount(live.source)).toBe(64)
    const fresh = fixture(),
      action = new AbortController(),
      callback = vi.fn()
    const blocked = subscribeNativeActionHook(fresh.source, callback, action)
    fresh.emit({
      clientToken: blocked.clientToken,
      id: 90,
      generation: 1,
      token: 90,
      stage: 'beforeStable',
    })
    await vi.waitFor(() => expect(action.signal.aborted).toBe(true))
    expect(action.signal.reason).toEqual(
      new Error('Native action hook physical callback capacity exceeded'),
    )
    expect(callback).not.toHaveBeenCalled()
    blocked.dispose()
    finish.shift()?.()
    await vi.waitFor(() => expect(nativeActionHookPhysicalCount(live.source)).toBe(63))
    const recovered = subscribeNativeActionHook(fresh.source, callback, new AbortController())
    fresh.emit({
      clientToken: recovered.clientToken,
      id: 91,
      generation: 1,
      token: 91,
      stage: 'beforeStable',
    })
    await vi.waitFor(() => expect(callback).toHaveBeenCalledTimes(1))
    recovered.dispose()
    for (const settle of finish) settle()
    await vi.waitFor(() => expect(nativeActionHookPhysicalCount(live.source)).toBe(0))
    expect(nativeActionHookPhysicalCount(fresh.source)).toBe(0)
  })
})
