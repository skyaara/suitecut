// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'

function sourceBoundary() {
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
describe('native standards send-keys transport ownership', () => {
  it('sends one bounded pinned child-frame intent and rejects a disposed handle before dispatch', async () => {
    const { source, runAutomation } = sourceBoundary()
    const page = new NativePage(source)
    const frame = new NativeFrame(page, {
      id: 'child',
      parentId: 'root',
      name: '',
      url: 'about:blank',
    })
    const element = new NativeElementHandle(frame, 17, 'JSHandle@input', 'actual-unit-handle')
    await element.sendKeys('input', { timeout: 93, delay: 2 })
    expect(runAutomation).toHaveBeenCalledTimes(1)
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      action: 'sendKeys',
      value: 'input',
      nativeHandle: 17,
      strict: true,
      frame: 'child',
      timeoutMs: 93,
      delay: 2,
    })
    await element.dispose()
    runAutomation.mockClear()
    await expect(element.sendKeys('late')).rejects.toThrow('disposed')
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('keeps lazy frame paths and strict selection, and validates timing before transport', async () => {
    const { source, runAutomation } = sourceBoundary()
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
    await locator.sendKeys('value')
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      action: 'sendKeys',
      framePath,
      strict: false,
    })
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('nativeHandle')
    runAutomation.mockClear()
    await expect(locator.sendKeys('late', { timeout: -1 })).rejects.toThrow()
    await expect(locator.sendKeys('late', { delay: 120001 })).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
