// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>((): Promise<UntrustedInput> =>
    Promise.resolve({ handle: 19, preview: 'JSHandle@<iframe>', node: true, frame: 'parent' }),
  )
  const browser: NativeBrowser = {
    state: 'running',
    url: 'http://parent.test/',
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
    frameInfos: () => [
      { id: 'parent', parentId: '', name: '', url: 'http://parent.test/' },
      { id: 'child', parentId: 'parent', name: 'same', url: 'http://child.test/' },
    ],
  }
  const page = createNativePage(browser)
  const child = page.frames().find((frame) => frame.identity() === 'child')
  if (!child) throw new Error('Controlled child identity missing')
  return { page, child, runAutomation }
}

describe('native frame owner retained handle boundary', () => {
  it('requests the actual child identity and binds returned handle operations to the native parent identity', async () => {
    const { child, runAutomation } = fixture()
    const owner = await child.frameElement()
    expect(runAutomation).toHaveBeenLastCalledWith(
      'handle',
      expect.objectContaining({ action: 'frameElement', frame: 'child' }),
      expect.any(AbortSignal),
    )
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('selector')
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('nativeQueryNodes')
    runAutomation.mockResolvedValueOnce({})
    await owner.dispose()
    expect(runAutomation).toHaveBeenLastCalledWith(
      'handle',
      expect.objectContaining({ action: 'dispose', frame: 'parent', handle: 19 }),
      expect.any(AbortSignal),
    )
  })
  it('preserves genuine native failure and never falls back to selector or frame-index lookup', async () => {
    const { child, runAutomation } = fixture()
    const native = new Error('Actual native parent context was destroyed')
    runAutomation.mockRejectedValueOnce(native)
    await expect(child.frameElement()).rejects.toBe(native)
    expect(runAutomation).toHaveBeenCalledOnce()
  })
  it('rejects unknown returned native frame metadata rather than binding a retained handle to the child', async () => {
    const { child, runAutomation } = fixture()
    runAutomation.mockResolvedValueOnce({
      handle: 19,
      preview: 'JSHandle@<iframe>',
      node: true,
      frame: 'unknown-frame',
    })
    await expect(child.frameElement()).rejects.toThrow(/frame was detached before delivery/u)
    expect(runAutomation).toHaveBeenCalledOnce()
  })
})
