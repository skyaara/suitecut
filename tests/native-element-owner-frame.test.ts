// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage, NativeElementHandle } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>((): Promise<UntrustedInput> =>
    Promise.resolve({ pageId: 1, frame: 'child' }),
  )
  const browser: NativeBrowser = {
    pageId: 1,
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
  const element = new NativeElementHandle(page.mainFrame(), 19)
  return { page, element, runAutomation }
}

describe('native element owner document identity transport', () => {
  it('maps the genuine native returned identity rather than the handle execution frame', async () => {
    const { page, element, runAutomation } = fixture()
    expect(await element.ownerFrame()).toBe(
      page.frames().find((frame) => frame.identity() === 'child'),
    )
    expect(runAutomation).toHaveBeenLastCalledWith(
      'handle',
      expect.objectContaining({ action: 'ownerFrame', handle: 19 }),
      expect.any(AbortSignal),
    )
    expect(runAutomation).toHaveBeenCalledOnce()
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('selector')
  })
  it('returns null only on actual native document absence and preserves native errors', async () => {
    const { element, runAutomation } = fixture()
    runAutomation.mockResolvedValueOnce(null)
    await expect(element.ownerFrame()).resolves.toBeNull()
    const error = new Error('Actual owner document identity unavailable')
    runAutomation.mockRejectedValueOnce(error)
    await expect(element.ownerFrame()).rejects.toBe(error)
  })
  it('refuses unknown native frame or page identity rather than guessing a matching URL or index', async () => {
    const { element, runAutomation } = fixture()
    runAutomation.mockResolvedValueOnce({ pageId: 1, frame: 'unknown' })
    await expect(element.ownerFrame()).rejects.toThrow(/identity is no longer available/u)
    runAutomation.mockResolvedValueOnce({ pageId: 2, frame: 'child' })
    await expect(element.ownerFrame()).rejects.toThrow(/page identity is unavailable/u)
  })
  it('rejects disposed handles before dispatch and validates actual response fields', async () => {
    const { element, runAutomation } = fixture()
    runAutomation.mockResolvedValueOnce({ pageId: 0, frame: '' })
    await expect(element.ownerFrame()).rejects.toThrow()
    runAutomation.mockResolvedValueOnce({})
    await element.dispose()
    await expect(element.ownerFrame()).rejects.toThrow(/disposed/u)
    expect(runAutomation).toHaveBeenCalledTimes(2)
  })
})
