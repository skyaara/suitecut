// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue(null)
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: new AbortController().signal,
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
  }
  return { runAutomation, page: new NativePage(source) }
}

describe('native blur admission and target ownership', () => {
  it('pins child-frame nodes and rejects disposed handles before native transport', async () => {
    const f = fixture()
    const frame = new NativeFrame(f.page, {
      id: 'child',
      parentId: 'root',
      name: 'child',
      url: 'about:blank',
    })
    const handle = new NativeElementHandle(frame, 17, 'JSHandle@textarea', 'actual-blur-node')
    await handle.blur({ timeout: 0 })
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'blur',
        nativeHandle: 17,
        frame: 'child',
        timeoutMs: 0,
      }),
      expect.any(AbortSignal),
    )
    await handle.dispose()
    f.runAutomation.mockClear()
    expect(() => handle.blur()).toThrow('disposed')
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
  it('retains lazy frames, strictness and the actual current default deadline', async () => {
    const f = fixture(),
      framePath = [[{ engine: 'css' as const, selector: 'iframe' }]]
    f.page.setDefaultTimeout(451)
    await new NativeLocator(
      f.page,
      'textarea',
      [{ engine: 'css', selector: 'textarea' }],
      framePath,
    ).blur()
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'blur',
        strict: true,
        framePath,
        timeoutMs: 451,
      }),
      expect.any(AbortSignal),
    )
  })
  it('preserves the original cancellation Error through the native owner signal', async () => {
    const f = fixture(),
      controller = new AbortController(),
      reason = new Error('original blur owner')
    f.runAutomation.mockImplementation(
      (_command, _params, signal) =>
        new Promise((_resolve, reject) => {
          signal?.addEventListener(
            'abort',
            () => {
              const failure = signal.reason as UntrustedInput
              reject(
                failure instanceof Error ? failure : new Error('Non-Error cancellation reason'),
              )
            },
            { once: true },
          )
        }),
    )
    const result = f.page.locator('textarea').blur({ timeout: 0, signal: controller.signal })
    controller.abort(reason)
    await expect(result).rejects.toBe(reason)
  })
  it('rejects invalid or pointer-only options before native admission', async () => {
    const f = fixture()
    for (const timeout of [-1, 0.5, Number.NaN, Number.POSITIVE_INFINITY, 120_001])
      await expect(f.page.locator('textarea').blur({ timeout })).rejects.toThrow()
    await expect(
      f.page.locator('textarea').blur({ position: { x: 1, y: 2 } } as never),
    ).rejects.toThrow()
    await expect(f.page.locator('textarea').blur({ force: true } as never)).rejects.toThrow()
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
})
