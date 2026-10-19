// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue('ACTUAL')
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

describe('native control value read admission', () => {
  it('reads a pinned child node through one native primitive operation and retains UTF16 units', async () => {
    const f = fixture(),
      frame = new NativeFrame(f.page, {
        id: 'child',
        parentId: 'root',
        name: 'child',
        url: 'about:blank',
      })
    const handle = new NativeElementHandle(frame, 17, 'JSHandle@input', 'actual-value-node')
    f.runAutomation.mockResolvedValue('\ud800x\udfff')
    expect(await handle.inputValue({ timeout: 0 })).toBe('\ud800x\udfff')
    expect(f.runAutomation).toHaveBeenCalledTimes(1)
    expect(f.runAutomation).toHaveBeenCalledWith(
      'locator',
      expect.objectContaining({
        action: 'inputValue',
        nativeHandle: 17,
        frame: 'child',
        strict: true,
        timeoutMs: 0,
      }),
      expect.any(AbortSignal),
    )
  })
  it('retains lazy frame paths, strictness and the actual inherited deadline', async () => {
    const f = fixture(),
      framePath = [[{ engine: 'css' as const, selector: 'iframe' }]]
    f.page.setDefaultTimeout(451)
    expect(
      await new NativeLocator(
        f.page,
        'label',
        [{ engine: 'css', selector: 'label' }],
        framePath,
        undefined,
        false,
      ).inputValue(),
    ).toBe('ACTUAL')
    expect(f.runAutomation).toHaveBeenCalledWith(
      'locator',
      expect.objectContaining({
        action: 'inputValue',
        framePath,
        strict: false,
        timeoutMs: 451,
      }),
      expect.any(AbortSignal),
    )
  })
  it('requires a genuine primitive string without coercing or interpreting author object tags', async () => {
    const f = fixture()
    for (const result of [null, 42, false, { __suitecutNativeUTF16Scalar: 'd800' }]) {
      f.runAutomation.mockResolvedValue(result)
      await expect(f.page.locator('input').inputValue()).rejects.toThrow()
    }
  })
  it('preserves original cancellation and rejects disposed/invalid requests before transport', async () => {
    const f = fixture(),
      controller = new AbortController(),
      reason = new Error('original input read owner')
    f.runAutomation.mockImplementation(
      (_command, _params, signal) =>
        new Promise((_resolve, reject) => {
          signal?.addEventListener(
            'abort',
            () => {
              const cause = signal.reason as UntrustedInput
              reject(
                cause instanceof Error ? cause : new Error('Non-Error input read cancellation'),
              )
            },
            { once: true },
          )
        }),
    )
    const result = f.page.locator('input').inputValue({ signal: controller.signal, timeout: 0 })
    controller.abort(reason)
    await expect(result).rejects.toBe(reason)
    f.runAutomation.mockResolvedValue('ACTUAL')
    const handle = new NativeElementHandle(f.page, 17, 'JSHandle@input', 'actual-value-node')
    await handle.dispose()
    f.runAutomation.mockClear()
    await expect(handle.inputValue()).rejects.toThrow('disposed')
    await expect(f.page.locator('input').inputValue({ timeout: -1 })).rejects.toThrow()
    await expect(f.page.locator('input').inputValue({ force: true } as never)).rejects.toThrow()
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
})
