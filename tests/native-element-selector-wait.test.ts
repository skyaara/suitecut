// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'
import { NativeTimeoutError } from '../src/native-timeouts.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>((): Promise<UntrustedInput> =>
    Promise.resolve({ handle: 19, preview: 'JSHandle@<section>', node: true }),
  )
  const browser: NativeBrowser = {
    state: 'running',
    url: 'http://actual.test/',
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
  return { page, runAutomation }
}

describe('retained element selector wait transport', () => {
  it('uses the actual retained root and atomic state selection without selector reacquisition', async () => {
    const { page, runAutomation } = fixture()
    const root = await page.$('section')
    expect(root).not.toBeNull()
    runAutomation.mockResolvedValueOnce({ handle: 23, preview: 'JSHandle@<button>', node: true })
    const selected = await root?.waitForSelector('button', {
      state: 'attached',
      timeout: 173,
      strict: true,
    })
    expect(selected).not.toBeNull()
    expect(runAutomation).toHaveBeenLastCalledWith(
      'handle',
      expect.objectContaining({
        action: 'create',
        nativeRootHandle: 19,
        selectorState: 'attached',
        strict: true,
        timeoutMs: 173,
        query: [{ engine: 'css', selector: 'button' }],
      }),
      expect.any(AbortSignal),
    )
    expect(runAutomation).toHaveBeenCalledTimes(2)
    runAutomation.mockResolvedValueOnce({})
    await selected?.dispose()
    expect(runAutomation).toHaveBeenLastCalledWith(
      'handle',
      expect.objectContaining({ action: 'dispose', handle: 23 }),
      expect.any(AbortSignal),
    )
  })
  it('passes hidden/detached observations through the genuine scoped native polling owner', async () => {
    const { page, runAutomation } = fixture()
    const root = await page.$('section')
    for (const state of ['hidden', 'detached'] as const) {
      runAutomation.mockResolvedValueOnce({ result: { value: { matches: true, actual: null } } })
      await expect(
        root?.waitForSelector('button', { state, timeout: 241, strict: true }),
      ).resolves.toBeNull()
      expect(runAutomation).toHaveBeenLastCalledWith(
        'queryEvaluate',
        expect.objectContaining({
          nativeRootHandle: 19,
          all: true,
          dom: true,
          expectation: true,
          visibility: state === 'hidden',
          timeoutMs: 241,
          query: [{ engine: 'css', selector: 'button' }],
        }),
        expect.any(AbortSignal),
      )
    }
    expect(runAutomation).toHaveBeenCalledTimes(3)
  })
  it('preserves original native timeout causes and scope destruction errors', async () => {
    const { page, runAutomation } = fixture()
    const root = await page.$('section')
    const native = new NativeTimeoutError('Original native deadline exceeded')
    runAutomation.mockRejectedValueOnce(native)
    await expect(root?.waitForSelector('button', { timeout: 29 })).rejects.toMatchObject({
      cause: native,
      message: expect.stringContaining('29ms'),
    })
    const destroyed = new Error('Actual native root context was destroyed')
    runAutomation.mockRejectedValueOnce(destroyed)
    await expect(root?.waitForSelector('button')).rejects.toBe(destroyed)
    runAutomation.mockResolvedValueOnce({ result: { value: { matches: false, actual: null } } })
    await expect(root?.waitForSelector('button', { state: 'hidden', timeout: 31 })).rejects.toThrow(
      /31ms/u,
    )
  })
  it('validates options and refuses disposed root handles before dispatch', async () => {
    const { page, runAutomation } = fixture()
    const root = await page.$('section')
    await expect(root?.waitForSelector(' ')).rejects.toThrow()
    await expect(root?.waitForSelector('button', { timeout: -1 })).rejects.toThrow()
    expect(runAutomation).toHaveBeenCalledOnce()
    runAutomation.mockResolvedValueOnce({})
    await root?.dispose()
    await expect(root?.waitForSelector('button')).rejects.toThrow(/disposed/u)
    expect(runAutomation).toHaveBeenCalledTimes(2)
  })
})
