// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { hideNativeHighlight, showNativeHighlight } from '../src/native-highlight.js'
import { createNativePage, NativeFrame } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

const token = 'hActualSource_123'
function fixture() {
  const runAutomation = vi.fn(
    (command: string, params?: Record<string, UntrustedInput>): Promise<UntrustedInput> =>
      Promise.resolve(command === 'highlight' && params?.action === 'show' ? { token } : {}),
  )
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
  return { source, page: createNativePage(source), runAutomation }
}

describe('genuine native locator highlight transport and delivery ownership', () => {
  it('claims the actual native token and disposes that token only once', async () => {
    const run = vi.fn((params: Record<string, UntrustedInput>): Promise<UntrustedInput> =>
      Promise.resolve(params.action === 'show' ? { token } : {}),
    )
    const cleanup = vi.fn(() => Promise.resolve())
    const handle = await showNativeHighlight(run, cleanup)
    expect(run.mock.calls).toEqual([[{ action: 'show' }], [{ action: 'claim', token }]])
    await Promise.all([handle.dispose(), handle[Symbol.asyncDispose]()])
    expect(cleanup.mock.calls).toEqual([[token]])
  })

  it('preserves a claim failure and performs physical-source cleanup', async () => {
    const failure = new Error('Actual caller cancellation')
    const run = vi.fn((params: Record<string, UntrustedInput>): Promise<UntrustedInput> =>
      params.action === 'show' ? Promise.resolve({ token }) : Promise.reject(failure),
    )
    const cleanup = vi.fn(() => Promise.resolve())
    await expect(showNativeHighlight(run, cleanup)).rejects.toBe(failure)
    expect(cleanup.mock.calls).toEqual([[token, true]])
  })

  it('publishes both original delivery and cleanup errors', async () => {
    const failure = new Error('Actual caller cancellation'),
      release = new Error('Actual release deadline')
    const run = (params: Record<string, UntrustedInput>): Promise<UntrustedInput> =>
      params.action === 'show' ? Promise.resolve({ token }) : Promise.reject(failure)
    await expect(showNativeHighlight(run, () => Promise.reject(release))).rejects.toMatchObject({
      errors: [failure, release],
      cause: release,
    })
  })

  it('allows a later explicit disposal retry after a logical cleanup failure', async () => {
    const cleanup = vi
      .fn<() => Promise<void>>()
      .mockRejectedValueOnce(new Error('Actual hide deadline'))
      .mockResolvedValue(undefined)
    const handle = await showNativeHighlight(() => Promise.resolve({ token, empty: true }), cleanup)
    await expect(handle.dispose()).rejects.toThrow('Actual hide deadline')
    await expect(handle.dispose()).resolves.toBeUndefined()
    expect(cleanup).toHaveBeenCalledTimes(2)
  })

  it('never claims an actual empty-result no-resource token', async () => {
    const run = vi.fn((): Promise<UntrustedInput> => Promise.resolve({ token, empty: true }))
    const handle = await showNativeHighlight(run, () => Promise.resolve())
    expect(run).toHaveBeenCalledTimes(1)
    await handle.dispose()
  })

  it('rejects unsupported style before any native side effect', async () => {
    const run = vi.fn((): Promise<UntrustedInput> => Promise.resolve({ token }))
    await expect(
      showNativeHighlight(run, () => Promise.resolve(), { style: 'color: red' }),
    ).rejects.toThrow('arbitrary CSS')
    expect(run).not.toHaveBeenCalled()
  })

  it('rejects malformed native identities rather than deriving a DOM identifier', async () => {
    for (const invalid of ['#target', 'hA_01', 'hA_0', 'hA_1] injected', 'hA_\ud800']) {
      const cleanup = vi.fn(() => Promise.resolve())
      await expect(
        showNativeHighlight(() => Promise.resolve({ token: invalid }), cleanup),
      ).rejects.toThrow()
      expect(cleanup).not.toHaveBeenCalled()
    }
  })

  it('forwards genuine locator AST and lazy frame scope only for selection', async () => {
    const { page, runAutomation } = fixture()
    const handle = await page.frameLocator('iframe').getByRole('button', { name: 'Go' }).highlight()
    expect(runAutomation.mock.calls[0]?.[0]).toBe('highlight')
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      action: 'show',
      query: [
        { engine: 'css', selector: ':scope' },
        { engine: 'role', role: 'button', name: { text: 'Go' } },
      ],
      framePath: [[{ engine: 'css', selector: 'iframe' }]],
    })
    expect(runAutomation.mock.calls[1]?.[1]).toMatchObject({ action: 'claim', token })
    expect(runAutomation.mock.calls[1]?.[1]).not.toHaveProperty('query')
    await handle.dispose()
    expect(runAutomation.mock.calls[2]?.[1]).toMatchObject({ action: 'hide', token })
    expect(runAutomation.mock.calls[2]?.[1]).not.toHaveProperty('query')
  })

  it('keeps a genuine Frame owner in scoped show and hides without rematching elements', async () => {
    const { page, runAutomation } = fixture()
    const frame = new NativeFrame(page, {
      id: 'actual-child',
      parentId: 'actual-parent',
      name: 'Actual',
      url: 'about:blank',
    })
    await frame.locator('#actual').highlight()
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      action: 'show',
      frame: 'actual-child',
    })
    await frame.locator('#does-not-need-to-exist').hideHighlight()
    expect(runAutomation.mock.calls.at(-1)?.[1]).toMatchObject({
      action: 'hide',
      frame: 'actual-child',
    })
  })

  it('passes explicit hide as one native command without a fabricated token', async () => {
    const run = vi.fn(() => Promise.resolve({}))
    await hideNativeHighlight(run)
    expect(run.mock.calls).toEqual([[{ action: 'hide' }]])
  })
})
