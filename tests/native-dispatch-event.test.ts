// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'
import * as z from 'zod'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeJSHandle, NativePage } from '../src/native-page.js'
import { NativeTimeoutError } from '../src/native-timeouts.js'
import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue(undefined)
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: new AbortController().signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'controlled',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
  }
  return { runAutomation, page: new NativePage(source) }
}

describe('native dispatchEvent admission', () => {
  it('leaves Page first-match and Locator strict selection to the native engine', async () => {
    const f = fixture()
    f.page.setDefaultTimeout(241)
    await f.page.dispatchEvent('button', 'click')
    await f.page.dispatchEvent('button', 'click', {}, { strict: true, timeout: 0 })
    await f.page.locator('button').dispatchEvent('click', { button: 1 })
    expect(f.runAutomation.mock.calls.map((call) => call[1]?.strict)).toEqual([false, true, true])
    expect(f.runAutomation.mock.calls[0]?.[1]).toMatchObject({
      action: 'dispatchEvent',
      timeoutMs: 241,
    })
    expect(f.runAutomation.mock.calls[1]?.[1]).toHaveProperty('timeoutMs', 0)
    expect(f.runAutomation.mock.calls).toHaveLength(3)
    for (const call of f.runAutomation.mock.calls) expect(call[1]).not.toHaveProperty('expression')
  })
  it('preserves exact fixed frame identity and real node handle references', async () => {
    const f = fixture(),
      frame = new NativeFrame(f.page, {
        id: 'child',
        parentId: 'root',
        name: '',
        url: 'about:blank',
      })
    const target = new NativeElementHandle(frame, 17, 'JSHandle@button', 'target-node')
    const related = new NativeElementHandle(frame, 18, 'JSHandle@span', 'related-node')
    await target.dispatchEvent('mouseover', { relatedTarget: related })
    const params = f.runAutomation.mock.calls[0]?.[1]
    expect(params).toMatchObject({ nativeHandle: 17, frame: 'child', handles: [18], strict: true })
    const encoded: UntrustedInput = JSON.parse(
      z.string().parse(params?.eventInitJSON),
    ) as UntrustedInput
    expect(createNativeValueCodec().decode(encoded, ['real-reference-marker'])).toEqual({
      type: 'mouseover',
      eventInit: { relatedTarget: 'real-reference-marker' },
    })
  })
  it('retains cycle identity, special values and isolated UTF-16 without executable payloads', async () => {
    const f = fixture(),
      detail: { units: string; self?: UntrustedInput; missing: undefined; negativeZero: number } = {
        units: '\ud800',
        missing: undefined,
        negativeZero: -0,
      }
    detail.self = detail
    await f.page.dispatchEvent('target', 'custom', { detail })
    const decoded = createNativeValueCodec().decode(
      JSON.parse(
        z.string().parse(f.runAutomation.mock.calls[0]?.[1]?.eventInitJSON),
      ) as UntrustedInput,
    ) as { eventInit: { detail: typeof detail } }
    expect(decoded.eventInit.detail.self).toBe(decoded.eventInit.detail)
    expect(decoded.eventInit.detail.units).toBe('\ud800')
    expect(Object.is(decoded.eventInit.detail.negativeZero, -0)).toBe(true)
  })
  it('rejects invalid or oversized payloads and foreign/disposed handles before transport', async () => {
    const f = fixture(),
      foreign = new NativeJSHandle(fixture().page, 9, 'JSHandle@object')
    await expect(f.page.dispatchEvent('x', '', {})).rejects.toThrow()
    await expect(f.page.dispatchEvent('x', 'click', null)).rejects.toThrow('eventInit')
    await expect(
      f.page.dispatchEvent('x', 'custom', { detail: 'x'.repeat(17_000) }),
    ).rejects.toThrow('16 KiB')
    await expect(f.page.dispatchEvent('x', 'click', { relatedTarget: foreign })).rejects.toThrow()
    await expect(
      f.page.locator('x').dispatchEvent('click', {}, { strict: false } as never),
    ).rejects.toThrow()
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
  it('keeps caller abort identity and native timeout classification', async () => {
    const f = fixture(),
      controller = new AbortController(),
      reason = new Error('original dispatch cancellation')
    f.runAutomation.mockImplementation(
      (_command, _params, signal) =>
        new Promise((_resolve, reject) =>
          signal?.addEventListener(
            'abort',
            () =>
              reject(
                signal.reason instanceof Error
                  ? signal.reason
                  : new Error('controlled non-Error abort'),
              ),
            { once: true },
          ),
        ),
    )
    const pending = f.page.dispatchEvent(
      'missing',
      'click',
      {},
      { signal: controller.signal, timeout: 0 },
    )
    controller.abort(reason)
    await expect(pending).rejects.toBe(reason)
    f.runAutomation.mockRejectedValue(
      new NativeTimeoutError('dispatchEvent: Native operation timed out'),
    )
    await expect(f.page.dispatchEvent('missing', 'click')).rejects.toBeInstanceOf(
      NativeTimeoutError,
    )
    expect(f.runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('signal')
  })
})
