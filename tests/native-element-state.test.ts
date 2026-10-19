// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage, NativeElementHandle } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture(value: UntrustedInput = { matches: true }) {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>((): Promise<UntrustedInput> =>
    Promise.resolve({ result: { value } }),
  )
  const browser: NativeBrowser = {
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
  const page = createNativePage(browser)
  return { page, runAutomation, handle: new NativeElementHandle(page, 17, '<input>') }
}

describe('native element state ownership boundary', () => {
  it('rejects unsupported states and deadlines before native work', async () => {
    const { handle, runAutomation } = fixture()
    await expect(handle.waitForElementState('attached' as never)).rejects.toThrow()
    await expect(handle.waitForElementState('stable', { timeout: Number.NaN })).rejects.toThrow()
    await expect(handle.waitForElementState('stable', { timeout: -1 })).rejects.toThrow()
    await expect(handle.waitForElementState('stable', { timeout: 120001 })).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('uses exact retained handle identity and preserves an unlimited original deadline', async () => {
    const { handle, runAutomation } = fixture()
    await handle.waitForElementState('stable', { timeout: 0 })
    expect(runAutomation).toHaveBeenCalledOnce()
    expect(runAutomation).toHaveBeenLastCalledWith(
      'queryEvaluate',
      expect.objectContaining({
        nativeHandle: 17,
        stableState: true,
        elementState: true,
        timeoutMs: 0,
      }),
      expect.any(AbortSignal),
    )
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('selector')
  })
  it('retains real native attachment errors rather than converting them to timeouts', async () => {
    const { handle, runAutomation } = fixture()
    runAutomation.mockResolvedValueOnce({
      exceptionDetails: {
        text: 'Uncaught',
        exception: { description: 'Error: Element is not attached to the DOM' },
      },
    })
    await expect(handle.waitForElementState('visible')).rejects.toThrow('not attached to the DOM')
  })
  it('reports native final nonmatch with the original requested state and timeout', async () => {
    const { handle } = fixture({ matches: false })
    await expect(handle.waitForElementState('editable', { timeout: 37 })).rejects.toThrow(
      /Timeout 37ms exceeded, waiting for editable/u,
    )
  })
  it('refuses work after handle disposal', async () => {
    const { handle, runAutomation } = fixture()
    await handle.dispose()
    runAutomation.mockClear()
    await expect(handle.waitForElementState('hidden')).rejects.toThrow(/disposed/u)
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('uses immediate native visibility and rejects malformed observations', async () => {
    const { page, runAutomation } = fixture(false)
    expect(await page.locator('#missing').isVisible()).toBe(false)
    runAutomation.mockResolvedValueOnce({ result: { value: 'not a native boolean' } })
    await expect(page.locator('#missing').isVisible()).rejects.toThrow()
  })
})
