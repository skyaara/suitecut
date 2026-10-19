// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'

function sourceBoundary() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue(null)
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
  return { source, runAutomation }
}
describe('targeted keyboard native transport', () => {
  it('sends a retained child-frame press as one target command with the original budget and signal', async () => {
    const { source, runAutomation } = sourceBoundary()
    const page = new NativePage(source)
    const frame = new NativeFrame(page, {
      id: 'actual-child',
      parentId: 'root',
      name: '',
      url: 'about:blank',
    })
    const handle = new NativeElementHandle(frame, 17, 'JSHandle@input', 'actual-node')
    const controller = new AbortController()
    const signal = controller.signal
    await handle.press('Control+a', { timeout: 73, delay: 3, signal })
    expect(runAutomation).toHaveBeenCalledTimes(1)
    expect(runAutomation.mock.calls[0]).toEqual([
      'locator',
      expect.objectContaining({
        action: 'press',
        nativeHandle: 17,
        strict: true,
        frame: 'actual-child',
        key: 'Control+a',
        timeoutMs: 73,
        delay: 3,
        query: [],
        framePath: [],
      }),
      expect.any(AbortSignal),
    ])
    const reason = new Error('actual caller abort')
    controller.abort(reason)
    expect(runAutomation.mock.calls[0]?.[2]?.aborted).toBe(true)
    expect(runAutomation.mock.calls[0]?.[2]?.reason).toBe(reason)
    expect(runAutomation.mock.calls[0]?.[1]).toHaveProperty('diagnosticExpressionJSON')
  })

  it('retains lazy frame/query policy instead of issuing a separate focus command', async () => {
    const { source, runAutomation } = sourceBoundary()
    const page = new NativePage(source)
    const framePath = [[{ engine: 'css' as const, selector: 'iframe' }]]
    const query = [{ engine: 'css' as const, selector: 'input' }]
    const locator = new NativeLocator(page, 'input', query, framePath, undefined, false)
    await locator.press('Enter', { timeout: 0 })
    expect(runAutomation).toHaveBeenCalledTimes(1)
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      action: 'press',
      strict: false,
      query,
      framePath,
      timeoutMs: 0,
    })
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('nativeHandle')
  })

  it('retains caller cancellation for all target keyboard intents and aliases', async () => {
    const { source, runAutomation } = sourceBoundary()
    const page = new NativePage(source)
    const controller = new AbortController()
    const signal = controller.signal
    const options = { timeout: 41, delay: 2, signal }
    await page.press('input', 'Enter', options)
    await page.type('input', 'a', options)
    await page.locator('input').sendKeys('b', options)
    await page.locator('input').pressSequentially('c', options)
    expect(runAutomation.mock.calls.map((call) => call[1]?.action)).toEqual([
      'press',
      'type',
      'sendKeys',
      'type',
    ])
    for (const call of runAutomation.mock.calls) {
      expect(call[1]).toMatchObject({ timeoutMs: 41, delay: 2 })
      expect(call[2]?.aborted).toBe(false)
    }
    const reason = new Error('actual caller abort')
    controller.abort(reason)
    for (const call of runAutomation.mock.calls) {
      expect(call[2]?.aborted).toBe(true)
      expect(call[2]?.reason).toBe(reason)
    }
  })

  it('rejects invalid target options before dispatch and disposed handles before focus', async () => {
    const { source, runAutomation } = sourceBoundary()
    const page = new NativePage(source)
    const locator = page.locator('input')
    await expect(locator.press('Enter', { timeout: -1 })).rejects.toThrow()
    await expect(locator.type('text', { delay: 120001 })).rejects.toThrow()
    await expect(locator.sendKeys('text', { signal: 'wrong' } as never)).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
    const handle = new NativeElementHandle(page, 27, 'JSHandle@input', 'actual-node')
    await handle.dispose()
    runAutomation.mockClear()
    await expect(handle.press('Enter')).rejects.toThrow('disposed')
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
