// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativePage } from '../src/native-page.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue([])
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
  return { page: new NativePage(source), runAutomation }
}

describe('Page form native first-match policy', () => {
  it('delegates every Page form selector with native first-match policy and original action metadata', async () => {
    const { page, runAutomation } = fixture()
    page.setDefaultTimeout(319)
    await page.check('input')
    await page.uncheck('input')
    await page.setChecked('input', true)
    await page.selectOption('select', 'actual')
    await page.setInputFiles('input', [])
    expect(runAutomation).toHaveBeenCalledTimes(5)
    expect(runAutomation.mock.calls.map((call) => call[1]?.strict)).toEqual([
      false,
      false,
      false,
      false,
      false,
    ])
    expect(runAutomation.mock.calls.map((call) => call[1]?.action)).toEqual([
      'setChecked',
      'setChecked',
      'setChecked',
      'selectOption',
      'setInputFiles',
    ])
    expect(runAutomation.mock.calls.map((call) => call[1]?.timeoutMs)).toEqual([
      319, 319, 319, 319, 319,
    ])
    expect(runAutomation.mock.calls[3]?.[1]).toMatchObject({
      query: [{ engine: 'css', selector: 'select' }],
      options: [{ valueOrLabel: 'actual' }],
    })
  })

  it('forwards explicit strictness without SDK node resolution and retains zero timeout', async () => {
    const { page, runAutomation } = fixture()
    await page.check('input', { strict: true, timeout: 0, position: { x: 4, y: 7 } })
    await page.uncheck('input', { strict: false })
    await page.setChecked('input', true, { strict: true })
    await page.selectOption('select', 'actual', { strict: true })
    await page.setInputFiles('input', [], { strict: true })
    expect(runAutomation.mock.calls.map((call) => call[1]?.strict)).toEqual([
      true,
      false,
      true,
      true,
      true,
    ])
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      timeoutMs: 0,
      position: { x: 4, y: 7 },
    })
    expect(runAutomation.mock.calls.every((call) => call[0] === 'form')).toBe(true)
  })

  it('keeps locator queries strict and fixed handles pinned to their original frame/node', async () => {
    const { page, runAutomation } = fixture()
    const frame = new NativeFrame(page, {
      id: 'actual-child',
      parentId: 'root',
      name: '',
      url: 'about:blank',
    })
    await page.locator('input').check()
    await page.locator('select').selectOption('actual')
    await page.locator('input').setInputFiles([])
    const handle = new NativeElementHandle(frame, 27, 'JSHandle@input', 'actual-node')
    await handle.check()
    await handle.setInputFiles([])
    expect(runAutomation.mock.calls.every((call) => call[1]?.strict === true)).toBe(true)
    for (const call of runAutomation.mock.calls.slice(3))
      expect(call[1]).toMatchObject({ nativeHandle: 27, frame: 'actual-child' })
    await expect(page.locator('input').check({ strict: false } as never)).rejects.toThrow()
  })

  it('preserves native Frame scope while applying Page first-match options', async () => {
    const { page, runAutomation } = fixture()
    const frame = new NativeFrame(page, {
      id: 'actual-child',
      parentId: 'root',
      name: '',
      url: 'about:blank',
    })
    await frame.check('input')
    await frame.selectOption('select', 'actual', { strict: true })
    await frame.setInputFiles('input', [])
    expect(runAutomation.mock.calls.map((call) => call[1]?.strict)).toEqual([false, true, false])
    for (const call of runAutomation.mock.calls)
      expect(call[1]).toHaveProperty('frame', 'actual-child')
  })

  it('rejects invalid strict options before any native admission for all form methods', async () => {
    const { page, runAutomation } = fixture()
    const invalid = { strict: 'false' } as never
    await expect(page.check('input', invalid)).rejects.toThrow()
    await expect(page.uncheck('input', invalid)).rejects.toThrow()
    await expect(page.setChecked('input', true, invalid)).rejects.toThrow()
    await expect(page.selectOption('select', 'actual', invalid)).rejects.toThrow()
    await expect(page.setInputFiles('input', [], invalid)).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })

  it('preserves each form method option boundary after removing Page-only strictness', async () => {
    const { page, runAutomation } = fixture()
    await expect(
      page.selectOption('select', 'actual', { strict: false, trial: true }),
    ).rejects.toThrow()
    await expect(
      page.setInputFiles('input', [], { strict: false, force: true } as never),
    ).rejects.toThrow()
    await expect(page.check('input', { strict: false, timeout: -1 })).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })

  it('omits explicitly undefined optional fields while retaining the native default budget', async () => {
    const { page, runAutomation } = fixture()
    page.setDefaultTimeout(271)
    await page.check('input', {
      strict: undefined,
      timeout: undefined,
      force: undefined,
      trial: undefined,
      position: undefined,
    })
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      strict: false,
      timeoutMs: 271,
      force: false,
      trial: false,
    })
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('position')
  })
})
