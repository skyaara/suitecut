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

describe('native scroll command admission and ownership', () => {
  it('pins actual child-frame handles and rejects disposed handles', async () => {
    const f = fixture()
    const frame = new NativeFrame(f.page, {
      id: 'child',
      parentId: 'root',
      name: 'child',
      url: 'about:blank',
    })
    const handle = new NativeElementHandle(frame, 17, 'JSHandle@div', 'real-node-key')
    await handle.scrollIntoViewIfNeeded({ timeout: 0 })
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'scrollIntoViewIfNeeded',
        nativeHandle: 17,
        frame: 'child',
        timeoutMs: 0,
        strict: true,
      }),
      expect.any(AbortSignal),
    )
    await handle.dispose()
    f.runAutomation.mockClear()
    expect(() => handle.scrollIntoViewIfNeeded()).toThrow('disposed')
    expect(f.runAutomation).not.toHaveBeenCalled()
  })

  it('preserves lazy frame selection and explicit locator strictness', async () => {
    const f = fixture(),
      path = [[{ engine: 'css' as const, selector: 'iframe' }]]
    const locator = new NativeLocator(
      f.page,
      '.target',
      [{ engine: 'css', selector: '.target' }],
      path,
    )
    await locator.scrollIntoViewIfNeeded({ timeout: 123 })
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'scrollIntoViewIfNeeded',
        framePath: path,
        strict: true,
        timeoutMs: 123,
      }),
      expect.any(AbortSignal),
    )
    f.page.setDefaultTimeout(451)
    await new NativeLocator(f.page, '.target', [], [], undefined, false).scrollIntoViewIfNeeded()
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({ strict: false, timeoutMs: 451 }),
      expect.any(AbortSignal),
    )
  })

  it('passes caller cancellation directly with its exact original reason', async () => {
    const f = fixture(),
      controller = new AbortController(),
      reason = new Error('original scroll owner')
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
    const result = f.page
      .locator('div')
      .scrollIntoViewIfNeeded({ timeout: 0, signal: controller.signal })
    controller.abort(reason)
    await expect(result).rejects.toBe(reason)
  })

  it('rejects malformed options before issuing browser commands', async () => {
    const f = fixture(),
      target = f.page.locator('div')
    for (const timeout of [-1, 0.5, Number.NaN, Number.POSITIVE_INFINITY, 120_001])
      await expect(target.scrollIntoViewIfNeeded({ timeout })).rejects.toThrow()
    await expect(target.scrollIntoViewIfNeeded({ force: true } as never)).rejects.toThrow()
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
})
