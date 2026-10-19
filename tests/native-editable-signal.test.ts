// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativePage } from '../src/native-page.js'
import { NativeTimeoutError } from '../src/native-timeouts.js'
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
  return { runAutomation, page: new NativePage(source) }
}
describe('native editable signal admission', () => {
  it('forwards caller cancellation separately from actual input payloads and preserves native default/zero deadlines', async () => {
    const f = fixture(),
      controller = new AbortController()
    f.page.setDefaultTimeout(317)
    await f.page.fill('input', 'A', { signal: controller.signal })
    await f.page.locator('input').clear({ signal: controller.signal, timeout: 0 })
    expect(f.runAutomation.mock.calls[0]).toEqual([
      'locator',
      expect.objectContaining({ action: 'fill', value: 'A', timeoutMs: 317 }),
      expect.any(AbortSignal),
    ])
    expect(f.runAutomation.mock.calls[1]).toEqual([
      'locator',
      expect.objectContaining({ action: 'clear', timeoutMs: 0 }),
      expect.any(AbortSignal),
    ])
    for (const call of f.runAutomation.mock.calls) {
      expect(call[1]).not.toHaveProperty('signal')
      expect(call[1]).not.toHaveProperty('force')
    }
    const forwarded = f.runAutomation.mock.calls[0]?.[2]
    expect(forwarded?.aborted).toBe(false)
    const reason = new Error('original cause')
    controller.abort(reason)
    expect(forwarded?.reason).toBe(reason)
  })
  it('keeps pinned child handle/frame identity within the single native operation', async () => {
    const f = fixture(),
      frame = new NativeFrame(f.page, {
        id: 'child',
        parentId: 'root',
        name: 'child',
        url: 'about:blank',
      }),
      handle = new NativeElementHandle(frame, 17, 'JSHandle@input', 'editable-node'),
      controller = new AbortController()
    await handle.fill('A', { signal: controller.signal, timeout: 71 })
    await handle.clear({ signal: controller.signal, timeout: 72 })
    expect(f.runAutomation).toHaveBeenCalledTimes(2)
    for (const call of f.runAutomation.mock.calls)
      expect(call).toEqual([
        'locator',
        expect.objectContaining({ nativeHandle: 17, frame: 'child', strict: true }),
        expect.any(AbortSignal),
      ])
    expect(f.runAutomation.mock.calls[0]?.[1]).toHaveProperty('timeoutMs', 71)
    expect(f.runAutomation.mock.calls[1]?.[1]).toHaveProperty('timeoutMs', 72)
  })
  it('preserves the original abort cause even when it looks like a fill error, while native timeouts keep their type', async () => {
    const f = fixture(),
      controller = new AbortController(),
      reason = new Error('fill: exact caller abort')
    f.runAutomation.mockImplementation(
      (_command, _params, signal) =>
        new Promise((_resolve, reject) => {
          signal?.addEventListener(
            'abort',
            () => {
              const cause = signal.reason as UntrustedInput
              reject(cause instanceof Error ? cause : new Error('Non-Error edit abort reason'))
            },
            { once: true },
          )
        }),
    )
    const pending = f.page.fill('input', 'A', { signal: controller.signal, timeout: 0 })
    controller.abort(reason)
    await expect(pending).rejects.toBe(reason)
    f.runAutomation.mockRejectedValue(new NativeTimeoutError('fill: Native operation timed out'))
    await expect(f.page.fill('input', 'A')).rejects.toBeInstanceOf(NativeTimeoutError)
  })
  it('rejects disposed/invalid options before transport and retains explicit unsupported force', async () => {
    const f = fixture(),
      handle = new NativeElementHandle(f.page, 17, 'JSHandle@input', 'editable-node')
    await handle.dispose()
    f.runAutomation.mockClear()
    expect(() => handle.clear({ signal: new AbortController().signal })).toThrow('disposed')
    await expect(f.page.fill('input', 'A', { signal: {} as AbortSignal })).rejects.toThrow(
      'AbortSignal',
    )
    await expect(f.page.locator('input').clear({ timeout: -1 })).rejects.toThrow('timeout')
    await expect(f.page.locator('input').fill('A', { force: true } as never)).rejects.toThrow(
      'Unsupported native action option: force',
    )
    expect(f.runAutomation).not.toHaveBeenCalled()
  })
})
