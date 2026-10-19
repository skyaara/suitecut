// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const runAutomation = vi
    .fn<NativeBrowser['runAutomation']>()
    .mockResolvedValue({ x: 127, y: 87, width: 60, height: 25 })
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
    frameInfos: () => [{ id: 'main', parentId: '', name: '', url: 'about:blank' }],
  }
  return { page: new NativePage(source), runAutomation }
}

describe('native bounding box transport', () => {
  it('sends the genuine lazy frame query and original timeout to native geometry without author evaluation', async () => {
    const { page, runAutomation } = fixture()
    const locator = page.frameLocator('iframe').locator('button')
    expect(await locator.boundingBox({ timeout: 71 })).toEqual({
      x: 127,
      y: 87,
      width: 60,
      height: 25,
    })
    expect(runAutomation).toHaveBeenCalledOnce()
    expect(runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'boundingBox',
        query: [{ engine: 'css', selector: 'button' }],
        framePath: [[{ engine: 'css', selector: 'iframe' }]],
        strict: true,
        timeoutMs: 71,
      }),
      expect.any(AbortSignal),
    )
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('function')
  })
  it('retains the child handle identity and actual negative/offscreen coordinates', async () => {
    const { page, runAutomation } = fixture()
    const frame = new NativeFrame(page, {
      id: 'actual-child',
      parentId: 'main',
      name: '',
      url: 'about:blank',
    })
    const handle = new NativeElementHandle(frame, 17)
    runAutomation.mockResolvedValue({ x: -11, y: -23, width: 4, height: 5 })
    expect(await handle.boundingBox({ timeout: 0 })).toEqual({
      x: -11,
      y: -23,
      width: 4,
      height: 5,
    })
    expect(runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        frame: 'actual-child',
        nativeHandle: 17,
        query: [],
        action: 'boundingBox',
        timeoutMs: 0,
      }),
      expect.any(AbortSignal),
    )
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('nativeRootHandle')
    runAutomation.mockResolvedValue({})
    await handle.dispose()
    runAutomation.mockClear()
    await expect(handle.boundingBox()).rejects.toThrow(/disposed/u)
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('preserves null only from the native reader and rejects malformed or nonfinite responses', async () => {
    const { page, runAutomation } = fixture()
    runAutomation.mockResolvedValue(null)
    expect(await page.locator('div').boundingBox()).toBeNull()
    for (const actual of [
      { x: NaN, y: 0, width: 2, height: 2 },
      { x: 0, y: 0, width: -1, height: 2 },
      { x: 0, y: 0, width: 2 },
    ]) {
      runAutomation.mockResolvedValue(actual)
      await expect(page.locator('div').boundingBox()).rejects.toThrow()
    }
  })
  it('validates options and forwards logical cancellation without another query', async () => {
    const { page, runAutomation } = fixture()
    await expect(page.locator('div').boundingBox({ timeout: -1 })).rejects.toThrow()
    await expect(page.locator('div').boundingBox({ force: true } as never)).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
    runAutomation.mockImplementation(
      (_command, _params, signal) =>
        new Promise((_resolve, reject) => {
          signal?.addEventListener(
            'abort',
            () => {
              const reason = signal.reason as UntrustedInput
              reject(
                reason instanceof Error ? reason : new Error('Non-Error geometry cancellation'),
              )
            },
            { once: true },
          )
        }),
    )
    const controller = new AbortController(),
      original = new Error('actual geometry cancellation')
    const pending = page.locator('div').boundingBox({ timeout: 0, signal: controller.signal })
    controller.abort(original)
    await expect(pending).rejects.toBe(original)
    expect(runAutomation).toHaveBeenCalledOnce()
  })
})
