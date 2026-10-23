// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativePage } from '../src/native-page.js'
import { NativeTimeoutError } from '../src/native-timeouts.js'

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
describe('Page.fill native first-match and strict admission', () => {
  it('forwards only the native strict flag and never resolves duplicate nodes in the SDK', async () => {
    const f = fixture(),
      controller = new AbortController()
    f.page.setDefaultTimeout(137)
    for (const strict of [undefined, false, true]) {
      await f.page.fill(
        'input',
        'actual',
        strict === undefined ? {} : { strict, signal: controller.signal, timeout: 0 },
      )
    }
    expect(f.runAutomation).toHaveBeenCalledTimes(3)
    expect(f.runAutomation.mock.calls.map((call) => call[1]?.strict)).toEqual([false, false, true])
    expect(f.runAutomation.mock.calls[0]?.[1]).toMatchObject({
      selector: 'input',
      query: [{ engine: 'css', selector: 'input' }],
      action: 'fill',
      value: 'actual',
      timeoutMs: 137,
    })
    expect(f.runAutomation.mock.calls[1]?.[1]).toHaveProperty('timeoutMs', 0)
    for (const call of f.runAutomation.mock.calls) expect(call[1]).not.toHaveProperty('signal')
  })
  it('retains strict locator/clear and fixed frame handle identity', async () => {
    const f = fixture(),
      frame = new NativeFrame(f.page, {
        id: 'child',
        parentId: 'root',
        name: '',
        url: 'about:blank',
      })
    await f.page.locator('input').fill('actual')
    await f.page.locator('input').clear()
    const handle = new NativeElementHandle(frame, 17, 'JSHandle@input', 'actual-node')
    await handle.fill('fixed')
    await handle.clear()
    expect(f.runAutomation.mock.calls.map((call) => call[1]?.strict)).toEqual([
      true,
      true,
      true,
      true,
    ])
    for (const call of f.runAutomation.mock.calls.slice(2))
      expect(call[1]).toMatchObject({ nativeHandle: 17, frame: 'child' })
    await expect(f.page.locator('input').fill('x', { strict: false } as never)).rejects.toThrow(
      'Unsupported native action option: strict',
    )
  })
  it('keeps genuine frame scoping while allowing Page-style first-match and strict forwarding', async () => {
    const f = fixture(),
      frame = new NativeFrame(f.page, {
        id: 'child',
        parentId: 'root',
        name: '',
        url: 'about:blank',
      })
    await frame.fill('input', 'actual')
    await frame.fill('input', 'actual', { strict: true })
    expect(f.runAutomation.mock.calls[0]?.[1]).toMatchObject({ frame: 'child', strict: false })
    expect(f.runAutomation.mock.calls[1]?.[1]).toMatchObject({ frame: 'child', strict: true })
  })
  it('rejects invalid strict before native admission and preserves the exact admitted signal/deadline types', async () => {
    const f = fixture()
    await expect(f.page.fill('input', 'actual', { strict: 'false' } as never)).rejects.toThrow()
    expect(f.runAutomation).not.toHaveBeenCalled()
    const controller = new AbortController(),
      reason = new Error('fill: original admitted signal')
    const options = { strict: false, signal: controller.signal }
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
    const pending = f.page.fill('input', 'actual', options)
    options.signal = new AbortController().signal
    controller.abort(reason)
    await expect(pending).rejects.toBe(reason)
    f.runAutomation.mockRejectedValue(new NativeTimeoutError('fill: Native operation timed out'))
    await expect(f.page.fill('input', 'actual')).rejects.toBeInstanceOf(NativeTimeoutError)
  })
})
