// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'

function boundarySource() {
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
  return { source, runAutomation }
}

describe('native editor target ownership', () => {
  it('keeps a pinned child-frame node for fill, focus, and clear and rejects disposed nodes', async () => {
    const { source, runAutomation } = boundarySource()
    const page = new NativePage(source)
    const frame = new NativeFrame(page, {
      id: 'child',
      parentId: 'root',
      name: '',
      url: 'about:blank',
    })
    const element = new NativeElementHandle(frame, 17, 'JSHandle@input', 'actual-unit-handle')
    await element.fill('changed', { timeout: 93 })
    await element.focus({ timeout: 94 })
    await element.clear({ timeout: 95 })
    expect(runAutomation.mock.calls.map((call) => call[1])).toEqual([
      expect.objectContaining({
        action: 'fill',
        value: 'changed',
        nativeHandle: 17,
        strict: true,
        frame: 'child',
        timeoutMs: 93,
      }),
      expect.objectContaining({
        action: 'focus',
        handle: 17,
        frame: 'child',
        timeoutMs: 94,
      }),
      expect.objectContaining({
        action: 'clear',
        nativeHandle: 17,
        strict: true,
        frame: 'child',
        timeoutMs: 95,
      }),
    ])
    await element.dispose()
    runAutomation.mockClear()
    expect(() => element.fill('late')).toThrow('disposed')
    expect(() => element.clear()).toThrow('disposed')
    await expect(element.focus()).rejects.toThrow('disposed')
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('preserves lazy frame paths and non-strict target selection instead of falling back to the main frame', async () => {
    const { source, runAutomation } = boundarySource()
    const page = new NativePage(source)
    const framePath = [[{ engine: 'css' as const, selector: 'iframe' }]]
    const locator = new NativeLocator(
      page,
      'input',
      [{ engine: 'css', selector: 'input' }],
      framePath,
      undefined,
      false,
    )
    await locator.fill('value')
    await locator.focus()
    await locator.clear()
    expect(runAutomation).toHaveBeenCalledTimes(3)
    for (const call of runAutomation.mock.calls) {
      expect(call[1]).toMatchObject({ framePath, strict: false })
      expect(call[1]).not.toHaveProperty('nativeHandle')
    }
  })
})
