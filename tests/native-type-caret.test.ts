// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'

function fixture(signal?: AbortSignal) {
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
  return { runAutomation, page: new NativePage(source, signal) }
}

describe('native pinned type operation admission', () => {
  it('types into a pinned child-frame node with one original native operation', async () => {
    const f = fixture(),
      frame = new NativeFrame(f.page, {
        id: 'child',
        parentId: 'root',
        name: 'child',
        url: 'about:blank',
      })
    const handle = new NativeElementHandle(frame, 17, 'JSHandle@input', 'actual-type-node')
    await handle.type('world', { timeout: 0, delay: 2 })
    expect(f.runAutomation).toHaveBeenCalledTimes(1)
    expect(f.runAutomation).toHaveBeenCalledWith(
      'locator',
      expect.objectContaining({
        action: 'type',
        nativeHandle: 17,
        frame: 'child',
        strict: true,
        timeoutMs: 0,
        delay: 2,
        value: 'world',
      }),
      expect.any(AbortSignal),
    )
  })
  it('retains lazy frame identity, explicit nonstrictness and the current default deadline', async () => {
    const f = fixture(),
      framePath = [[{ engine: 'css' as const, selector: 'iframe' }]]
    f.page.setDefaultTimeout(451)
    await new NativeLocator(
      f.page,
      'textarea',
      [{ engine: 'css', selector: 'textarea' }],
      framePath,
      undefined,
      false,
    ).type('actual')
    expect(f.runAutomation).toHaveBeenCalledWith(
      'locator',
      expect.objectContaining({
        action: 'type',
        framePath,
        strict: false,
        timeoutMs: 451,
        value: 'actual',
      }),
      expect.any(AbortSignal),
    )
  })
  it('rejects disposed fixed handles before any native focus or key command', async () => {
    const f = fixture(),
      handle = new NativeElementHandle(f.page, 17, 'JSHandle@input', 'actual-type-node')
    await handle.dispose()
    f.runAutomation.mockClear()
    await expect(handle.type('undelivered')).rejects.toThrow('disposed')
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
  it('rejects invalid values/options before native admission', async () => {
    const f = fixture(),
      handle = new NativeElementHandle(f.page, 17, 'JSHandle@input', 'actual-type-node')
    for (const timeout of [-1, 0.5, Number.POSITIVE_INFINITY, 120001])
      await expect(handle.type('text', { timeout })).rejects.toThrow()
    await expect(handle.type(3 as never)).rejects.toThrow()
    await expect(handle.type('text', { delay: -1 })).rejects.toThrow()
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
})
