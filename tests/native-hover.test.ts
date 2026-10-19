// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'

function sourceFixture() {
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
  return { source, runAutomation, page: new NativePage(source) }
}

describe('native hover admission and target ownership', () => {
  it('keeps legacy page hover non-strict while explicit locators remain strict', async () => {
    const f = sourceFixture()
    await f.page.hover('.multiple', {
      timeout: 0,
      position: { x: 3, y: 4 },
      force: true,
      trial: true,
      steps: 7,
      modifiers: ['Shift'],
    })
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'hover',
        strict: false,
        timeoutMs: 0,
        position: { x: 3, y: 4 },
        force: true,
        trial: true,
        steps: 7,
        modifiers: ['Shift'],
      }),
      expect.any(AbortSignal),
    )
    await f.page.locator('.multiple').hover()
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({ action: 'hover', strict: true }),
      expect.any(AbortSignal),
    )
    await f.page.hover('.multiple', { strict: true })
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({ strict: true }),
      expect.any(AbortSignal),
    )
  })
  it('preserves pinned child-frame handles instead of querying the main page', async () => {
    const f = sourceFixture(),
      frame = new NativeFrame(f.page, {
        id: 'child',
        parentId: 'root',
        name: 'child',
        url: 'about:blank',
      })
    const handle = new NativeElementHandle(frame, 17, 'JSHandle@button', 'actual-hover-test-handle')
    await handle.hover({ timeout: 93, position: { x: 2, y: 3 } })
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'hover',
        nativeHandle: 17,
        frame: 'child',
        strict: true,
        timeoutMs: 93,
        position: { x: 2, y: 3 },
      }),
      expect.any(AbortSignal),
    )
    await handle.dispose()
    f.runAutomation.mockClear()
    expect(() => handle.hover()).toThrow('disposed')
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
  it('retains lazy frame plans and non-strict selectors', async () => {
    const f = sourceFixture(),
      path = [[{ engine: 'css' as const, selector: 'iframe' }]]
    const locator = new NativeLocator(
      f.page,
      'button',
      [{ engine: 'css', selector: 'button' }],
      path,
      undefined,
      false,
    )
    await locator.hover({ timeout: 71 })
    expect(f.runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({ action: 'hover', framePath: path, strict: false, timeoutMs: 71 }),
      expect.any(AbortSignal),
    )
  })
  it('rejects click-only and malformed options before native admission', async () => {
    const f = sourceFixture()
    for (const options of [
      { button: 'left' },
      { delay: 5 },
      { steps: 0 },
      { position: { x: NaN, y: 1 } },
    ])
      await expect(f.page.locator('button').hover(options)).rejects.toThrow()
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
  it('retains the original caller abort cause and sends no late operation', async () => {
    const f = sourceFixture(),
      controller = new AbortController(),
      reason = new Error('authored hover cancellation')
    controller.abort(reason)
    await expect(
      f.page.locator('button').hover({ signal: controller.signal }),
    ).rejects.toMatchObject({ name: 'AbortError', cause: reason })
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
})
