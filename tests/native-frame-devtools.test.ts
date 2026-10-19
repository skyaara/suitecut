// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture(signal = new AbortController().signal) {
  const actual = {
    frameTree: { frame: { id: 'child', name: 'Actual\ud800', url: 'http://child.test/' } },
  }
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>((): Promise<UntrustedInput> =>
    Promise.resolve(actual),
  )
  const sendDevToolsCommand = vi.fn<NativeBrowser['sendDevToolsCommand']>(
    (): Promise<UntrustedInput> => Promise.resolve(actual),
  )
  const browser: NativeBrowser = {
    state: 'running',
    url: 'http://parent.test/',
    signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand,
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    frameInfos: () => [
      { id: 'parent', parentId: '', name: '', url: 'http://parent.test/' },
      { id: 'child', parentId: 'parent', name: 'same', url: 'http://child.test/' },
    ],
  }
  const page = createNativePage(browser)
  const child = page.frames().find((frame) => frame.identity() === 'child')
  if (!child) throw new Error('Controlled child identity missing')
  return { page, child, runAutomation, sendDevToolsCommand, actual }
}

describe('native frame Inspector command ownership', () => {
  it('routes a bound frame through its immutable native identity and original timeout', async () => {
    const { page, child, runAutomation, sendDevToolsCommand, actual } = fixture()
    page.setDefaultTimeout(17)
    expect(await child.sendDevToolsCommand('Page.getFrameTree')).toBe(actual)
    expect(runAutomation).toHaveBeenCalledWith(
      'devtools',
      expect.objectContaining({
        method: 'Page.getFrameTree',
        params: {},
        frame: 'child',
        timeoutMs: 17,
      }),
      expect.any(AbortSignal),
    )
    expect(sendDevToolsCommand).not.toHaveBeenCalled()
  })
  it('retains the raw page target path and does not inject caller params into frame ownership', async () => {
    const { page, child, runAutomation, sendDevToolsCommand } = fixture()
    await page.sendDevToolsCommand('Page.getFrameTree')
    expect(sendDevToolsCommand).toHaveBeenCalledWith('Page.getFrameTree', undefined)
    expect(runAutomation).not.toHaveBeenCalled()
    await child.sendDevToolsCommand('Runtime.evaluate', {
      expression: 'actual',
      frame: 'untrusted-param',
    })
    expect(runAutomation).toHaveBeenLastCalledWith(
      'devtools',
      expect.objectContaining({
        frame: 'child',
        params: { expression: 'actual', frame: 'untrusted-param' },
      }),
      expect.any(AbortSignal),
    )
  })
  it('preserves actual native failure without fallback to another target', async () => {
    const { child, runAutomation, sendDevToolsCommand } = fixture()
    const error = new Error('Actual Inspector target unavailable')
    runAutomation.mockRejectedValueOnce(error)
    await expect(child.sendDevToolsCommand('Page.getFrameTree')).rejects.toBe(error)
    expect(runAutomation).toHaveBeenCalledOnce()
    expect(sendDevToolsCommand).not.toHaveBeenCalled()
  })
  it('rejects an already-aborted projection before sending any native command', () => {
    const controller = new AbortController()
    const { child, runAutomation, sendDevToolsCommand } = fixture(controller.signal)
    const error = new Error('Actual caller cancellation')
    controller.abort(error)
    expect(() => child.sendDevToolsCommand('Page.getFrameTree')).toThrow(error)
    expect(runAutomation).not.toHaveBeenCalled()
    expect(sendDevToolsCommand).not.toHaveBeenCalled()
  })
})
