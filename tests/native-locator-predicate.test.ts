// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue({})
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

describe('native locator predicate transport', () => {
  it('submits one lazy strict native query, genuine frame path and original timeout, returning void', async () => {
    const { page, runAutomation } = fixture()
    expect(
      await page
        .frameLocator('iframe')
        .locator('#target')
        .waitForFunction((element) => !!element, undefined, { timeout: 71 }),
    ).toBeUndefined()
    expect(runAutomation).toHaveBeenCalledOnce()
    expect(runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'waitForFunction',
        strict: true,
        query: [{ engine: 'css', selector: '#target' }],
        framePath: [[{ engine: 'css', selector: 'iframe' }]],
        timeoutMs: 71,
        handles: [],
      }),
      expect.any(AbortSignal),
    )
    const declaration = runAutomation.mock.lastCall?.[1]?.function
    expect(declaration).toContain('return !!await predicate(this,')
    expect(declaration).not.toContain('requestAnimationFrame')
  })
  it('carries actual handle IDs and encoded values without reinterpreting author tag objects', async () => {
    const { page, runAutomation } = fixture()
    const child = new NativeFrame(page, {
      id: 'actual-child',
      parentId: 'main',
      name: '',
      url: 'about:blank',
    })
    const handle = new NativeElementHandle(child, 17)
    await child.locator('div').waitForFunction('element => !!element', {
      handle,
      tag: { __suitecutNativeUTF16Scalar: 'd800' },
      text: '\ud800',
    })
    expect(runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({ frame: 'actual-child', handles: [17] }),
      expect.any(AbortSignal),
    )
    expect(runAutomation.mock.lastCall?.[1]?.function).toContain('__suitecutNativeUTF16Scalar')
    runAutomation.mockResolvedValue({})
    await handle.dispose()
    runAutomation.mockClear()
    await expect(child.locator('div').waitForFunction(() => true, handle)).rejects.toThrow(
      /disposed/u,
    )
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('propagates only the selected native error-string field with exact isolated units', async () => {
    const { page, runAutomation } = fixture()
    const actual = 'Error: genuine predicate failure \ud800'
    runAutomation.mockResolvedValue({ predicateErrorJSON: JSON.stringify(actual) })
    await expect(page.locator('div').waitForFunction(() => true)).rejects.toThrow(actual)
    runAutomation.mockResolvedValue({
      predicateErrorJSON: '{"__suitecutNativeUTF16Scalar":"d800"}',
    })
    await expect(page.locator('div').waitForFunction(() => true)).rejects.toThrow()
  })
  it('wraps active and already-aborted signals as AbortError with the exact original cause', async () => {
    const { page, runAutomation } = fixture()
    const controller = new AbortController(),
      reason = new Error('actual predicate cancellation')
    runAutomation.mockImplementation(
      (_command, _params, signal) =>
        new Promise((_resolve, reject) =>
          signal?.addEventListener(
            'abort',
            () => {
              const error = signal.reason as UntrustedInput
              reject(error instanceof Error ? error : new Error('Non-Error native cancellation'))
            },
            { once: true },
          ),
        ),
    )
    const pending = page
      .locator('div')
      .waitForFunction(() => false, undefined, { timeout: 0, signal: controller.signal })
    controller.abort(reason)
    await expect(pending).rejects.toMatchObject({ name: 'AbortError', cause: reason })
    runAutomation.mockClear()
    const aborted = new AbortController()
    aborted.abort('already canceled')
    await expect(
      page.locator('div').waitForFunction(() => true, undefined, { signal: aborted.signal }),
    ).rejects.toMatchObject({ name: 'AbortError', cause: 'already canceled' })
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('rejects unsupported options and isolated function-source units explicitly before native admission', async () => {
    const { page, runAutomation } = fixture()
    for (const options of [{ timeout: -1 }, { polling: 17 }])
      await expect(
        page.locator('div').waitForFunction(() => true, undefined, options as never),
      ).rejects.toThrow()
    await expect(
      page.locator('div').waitForFunction("element => element.id === '\ud800'"),
    ).rejects.toThrow(/isolated UTF16 predicate source/u)
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
