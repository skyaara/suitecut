// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeLocator, NativePage } from '../src/native-page.js'

function fixture() {
  const runAutomation = vi
    .fn<NativeBrowser['runAutomation']>()
    .mockResolvedValue({ result: { type: 'object', value: { matches: true, actual: null } } })
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
  return { page: new NativePage(source), source, runAutomation }
}

describe('strict native locator wait diagnostics', () => {
  it('requests the genuine native formatter for each state while retaining its observation and original timeout', async () => {
    const { page, runAutomation } = fixture()
    for (const state of ['attached', 'detached', 'visible', 'hidden'] as const) {
      await page.locator('span').waitFor({ state, timeout: 73 })
      expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
        all: true,
        expectation: true,
        dom: true,
        strictDiagnostics: true,
        visibility: state === 'visible' || state === 'hidden',
        timeoutMs: 73,
        query: [{ engine: 'css', selector: 'span' }],
      })
    }
    expect(runAutomation).toHaveBeenCalledTimes(4)
  })
  it('retains explicit first-match policy instead of forcing diagnostic strictness', async () => {
    const { page, runAutomation } = fixture()
    await new NativeLocator(page, 'span', undefined, [], undefined, false).waitFor({
      state: 'attached',
    })
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      strictDiagnostics: false,
      expectation: true,
    })
  })
  it('passes only the real AST expression and frame path, without converting user description labels to aliases', async () => {
    const { page, runAutomation } = fixture()
    await page
      .frameLocator('#child')
      .getByRole('button')
      .describe('Unrelated label')
      .waitFor({ timeout: 87 })
    const request = runAutomation.mock.lastCall?.[1]
    expect(request).toMatchObject({
      query: [
        { engine: 'css', selector: ':scope' },
        { engine: 'role', role: 'button' },
      ],
      framePath: [[{ engine: 'css', selector: '#child' }]],
      strictDiagnostics: true,
      timeoutMs: 87,
    })
    expect(request?.diagnosticExpressionJSON).toContain('getByRole')
    expect(request?.diagnosticExpressionJSON).not.toContain('Unrelated label')
  })
  it('preserves the actual native strict error rather than synthesizing a host diagnostic', async () => {
    const { page, runAutomation } = fixture(),
      error = new Error('strict mode violation: actual native matched nodes and validated aliases')
    runAutomation.mockRejectedValue(error)
    await expect(page.locator('span').waitFor()).rejects.toBe(error)
  })
  it('forwards the actual page projection cancellation signal and validates options before admission', async () => {
    const { source, runAutomation } = fixture(),
      abort = new AbortController(),
      page = new NativePage(source, abort.signal)
    await page.locator('span').waitFor()
    const signal = runAutomation.mock.lastCall?.[2]
    expect(signal).toBeInstanceOf(AbortSignal)
    abort.abort('actual cause')
    expect(signal?.aborted).toBe(true)
    expect(signal?.reason).toBe('actual cause')
    runAutomation.mockClear()
    await expect(page.locator('span').waitFor({ state: 'stable' } as never)).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
