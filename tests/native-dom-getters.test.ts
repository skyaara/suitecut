// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'
import { NativeTimeoutError } from '../src/native-timeouts.js'
import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

const encoded = (value: UntrustedInput) => ({
  result: { type: 'object', value: createNativeValueCodec().encode(value) },
})
function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue(encoded('ACTUAL'))
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

describe('native DOM getter utility ownership', () => {
  it('keeps exact primitive UTF16/tag-looking strings and the genuine retained child handle', async () => {
    const { page, runAutomation } = fixture()
    const child = new NativeFrame(page, {
      id: 'child',
      parentId: 'main',
      name: '',
      url: 'about:blank',
    })
    const handle = new NativeElementHandle(child, 17)
    for (const actual of ['Actual\ud800\0\udfff', '{"__suitecutNativeUTF16Scalar":"d800"}', null]) {
      runAutomation.mockResolvedValue(encoded(actual))
      expect(await handle.getAttribute('data-original', { timeout: 0 })).toBe(actual)
      expect(runAutomation).toHaveBeenLastCalledWith(
        'queryEvaluate',
        expect.objectContaining({
          nativeHandle: 17,
          frame: 'child',
          dom: true,
          domGetter: true,
          timeoutMs: 0,
          query: [],
        }),
        expect.any(AbortSignal),
      )
      expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('nativeRootHandle')
    }
  })
  it('uses actual shared state metadata for locator and Page conveniences without host retargeting', async () => {
    const { page, runAutomation } = fixture()
    runAutomation.mockResolvedValue({
      result: { type: 'object', value: { enabled: false, editable: false } },
    })
    expect(await page.locator('label').isEnabled()).toBe(false)
    expect(await page.isDisabled('label')).toBe(true)
    expect(await page.isEditable('label')).toBe(false)
    expect(runAutomation).toHaveBeenLastCalledWith(
      'queryEvaluate',
      expect.objectContaining({
        selector: 'label',
        query: [{ engine: 'css', selector: 'label' }],
        dom: true,
        domGetter: true,
        elementState: true,
      }),
      expect.any(AbortSignal),
    )
    runAutomation.mockResolvedValue({ result: { type: 'object', value: { enabled: true } } })
    await expect(page.locator('div').isEditable()).rejects.toThrow(/not an <input>/u)
  })
  it('leaves public author evaluation in the author realm and preserves ordinary author tag objects', async () => {
    const { page, runAutomation } = fixture()
    const author = { __suitecutNativeUTF16Scalar: 'd800' }
    runAutomation.mockResolvedValue(encoded(author))
    expect(
      await page.locator('div').evaluate(() => ({ __suitecutNativeUTF16Scalar: 'd800' })),
    ).toEqual(author)
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('domGetter')
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('dom')
    expect(
      await page.locator('div').evaluateAll(() => ({ __suitecutNativeUTF16Scalar: 'd800' })),
    ).toEqual(author)
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('domGetter')
  })
  it('reads actual aggregate values through the utility path without per-node host handles', async () => {
    const { page, runAutomation } = fixture()
    runAutomation.mockResolvedValue(encoded(['a\ud800', 'b\udfff']))
    expect(await page.locator('div').allTextContents()).toEqual(['a\ud800', 'b\udfff'])
    expect(await page.locator('div').allInnerTexts()).toEqual(['a\ud800', 'b\udfff'])
    expect(runAutomation).toHaveBeenLastCalledWith(
      'queryEvaluate',
      expect.objectContaining({ dom: true, domGetter: true, all: true }),
      expect.any(AbortSignal),
    )
    expect(runAutomation.mock.calls.every((call) => call[0] === 'queryEvaluate')).toBe(true)
  })
  it('preserves lazy frame paths, original defaults and explicit caller cancellation', async () => {
    const { page, runAutomation } = fixture()
    page.setDefaultTimeout(451)
    const path = [[{ engine: 'css' as const, selector: 'iframe' }]]
    expect(
      await new NativeLocator(
        page,
        'div',
        [{ engine: 'css', selector: 'div' }],
        path,
      ).textContent(),
    ).toBe('ACTUAL')
    expect(runAutomation).toHaveBeenLastCalledWith(
      'queryEvaluate',
      expect.objectContaining({ timeoutMs: 451, framePath: path }),
      expect.any(AbortSignal),
    )
    const controller = new AbortController(),
      reason = new Error('original DOM getter owner')
    runAutomation.mockImplementation(
      (_command, _params, signal) =>
        new Promise((_resolve, reject) => {
          signal?.addEventListener(
            'abort',
            () => {
              const reason = signal.reason as UntrustedInput
              reject(
                reason instanceof Error ? reason : new Error('Non-Error DOM getter cancellation'),
              )
            },
            { once: true },
          )
        }),
    )
    const pending = page.locator('div').textContent({ timeout: 0, signal: controller.signal })
    controller.abort(reason)
    await expect(pending).rejects.toBe(reason)
  })
  it('keeps the real native timeout as cause and identifies the actual waiting selector', async () => {
    const { page, runAutomation } = fixture(),
      original = new NativeTimeoutError('Native operation timed out')
    runAutomation.mockRejectedValue(original)
    const result = page.locator("span's").innerText({ timeout: 17 })
    await expect(result).rejects.toMatchObject({ name: 'TimeoutError', cause: original })
    await expect(result).rejects.toThrow("waiting for locator('span\\'s')")
    await expect(result).rejects.toThrow('17ms')
    expect(runAutomation).toHaveBeenCalledOnce()
  })
  it('rejects invalid options and disposed fixed references without selector reacquisition', async () => {
    const { page, runAutomation } = fixture()
    await expect(page.locator('div').textContent({ timeout: -1 })).rejects.toThrow()
    await expect(page.locator('div').textContent({ force: true } as never)).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
    const handle = new NativeElementHandle(page, 17)
    runAutomation.mockResolvedValue({})
    await handle.dispose()
    runAutomation.mockClear()
    await expect(handle.textContent()).rejects.toThrow(/disposed/u)
    await expect(handle.getAttribute('x')).rejects.toThrow(/disposed/u)
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
