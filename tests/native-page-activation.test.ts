// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeFrame, NativePage } from '../src/native-page.js'
import { nativeCommandError, NativeTimeoutError } from '../src/native-timeouts.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue({})
  const abort = new AbortController()
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: abort.signal,
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
  return { page: new NativePage(source), runAutomation, abort }
}
describe('native page activation transport', () => {
  it('recognizes the actual native deadline failure without a host timer', () => {
    expect(
      nativeCommandError('Native page activation timed out at its original deadline'),
    ).toBeInstanceOf(NativeTimeoutError)
  })
  it('waits for genuine native completion and forwards its page operation timeout', async () => {
    const { page, runAutomation } = fixture()
    let acknowledge: ((value: object) => void) | undefined
    runAutomation.mockImplementationOnce(
      () =>
        new Promise((resolve) => {
          acknowledge = resolve
        }),
    )
    page.setDefaultTimeout(731)
    let complete = false
    const pending = page.bringToFront().then(() => {
      complete = true
    })
    expect(complete).toBe(false)
    expect(runAutomation).toHaveBeenCalledWith(
      'bringToFront',
      expect.objectContaining({ api: 'page.bringToFront', timeoutMs: 731 }),
      expect.any(AbortSignal),
    )
    acknowledge?.({})
    await pending
    expect(complete).toBe(true)
  })
  it('preserves native backend failure rather than inventing activation success', async () => {
    const { page, runAutomation } = fixture(),
      failure = new Error('Actual backend rejected activation')
    runAutomation.mockRejectedValueOnce(failure)
    await expect(page.bringToFront()).rejects.toBe(failure)
    expect(runAutomation).toHaveBeenCalledTimes(1)
  })
  it('does not admit work on an aborted page lifetime', async () => {
    const { page, runAutomation, abort } = fixture()
    abort.abort(new Error('Actual source closed'))
    await expect(page.bringToFront()).rejects.toThrow('Actual source closed')
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('does not substitute a child frame for the native page target', async () => {
    const { page, runAutomation } = fixture()
    const child = new NativeFrame(page, {
      id: 'child',
      parentId: 'main',
      name: '',
      url: 'about:blank',
    })
    await expect(child.bringToFront()).rejects.toThrow('requires a native page')
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
