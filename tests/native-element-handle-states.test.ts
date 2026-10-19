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

describe('fixed native handle state read boundary', () => {
  it('reads every state from the original native handle, without creating a selector target', async () => {
    const { handle, runAutomation } = fixture({ visible: true, enabled: false, editable: false })
    expect(await handle.isVisible()).toBe(true)
    expect(await handle.isHidden()).toBe(false)
    expect(await handle.isEnabled()).toBe(false)
    expect(await handle.isDisabled()).toBe(true)
    expect(await handle.isEditable()).toBe(false)
    for (const [command, config] of runAutomation.mock.calls) {
      expect(command).toBe('queryEvaluate')
      expect(config).toHaveProperty('nativeHandle', 17)
      expect(config).not.toHaveProperty('selector')
      expect(config).not.toHaveProperty('expectation')
    }
  })
  it('rejects malformed or absent native booleans rather than inferring public states', async () => {
    const { handle, runAutomation } = fixture({ visible: false })
    expect(await handle.isHidden()).toBe(true)
    await expect(handle.isEnabled()).rejects.toThrow()
    runAutomation.mockResolvedValueOnce({ result: { value: { visible: 'false' } } })
    await expect(handle.isVisible()).rejects.toThrow()
    runAutomation.mockResolvedValueOnce({ result: { value: { visible: true, enabled: true } } })
    await expect(handle.isEditable()).rejects.toThrow(/not an input/u)
  })
  it('refuses all immediate reads on disposed native handles', async () => {
    const { handle, runAutomation } = fixture()
    await handle.dispose()
    runAutomation.mockClear()
    for (const method of [
      () => handle.isVisible(),
      () => handle.isHidden(),
      () => handle.isEnabled(),
      () => handle.isDisabled(),
      () => handle.isEditable(),
    ])
      await expect(method()).rejects.toThrow(/disposed/u)
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
