// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'
import { createNativeValueCodec } from '../src/native-values.js'

function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>(() =>
    Promise.resolve({ result: { value: createNativeValueCodec().encode(0) } }),
  )
  const browser: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: new AbortController().signal,
    width: 320,
    height: 240,
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
  return { page: createNativePage(browser), runAutomation }
}

describe('native locator descriptions', () => {
  it('keeps immutable exact description metadata including empty text and UTF16 units', () => {
    const { page, runAutomation } = fixture(),
      original = page.locator('button')
    const text = 'Submit "quotes" \'apostrophes\'\n\ud800'
    const described = original.describe(text)
    expect(original.description()).toBe(null)
    expect(described).not.toBe(original)
    expect(described.description()).toBe(text)
    expect(String(described)).toBe(text)
    expect(described.describe('').description()).toBe('')
    expect(String(described.describe(''))).toBe('')
    expect(described.description()).toBe(text)
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('resets the description for actual derived locators without modifying the parent', () => {
    const { page } = fixture(),
      parent = page.locator('form').describe('Form')
    const child = parent.locator('input').describe('Input')
    expect(parent.description()).toBe('Form')
    expect(child.description()).toBe('Input')
    for (const derived of [
      child.locator('span'),
      child.nth(0),
      child.filter({ visible: true }),
      child.getByText('Name'),
    ])
      expect(derived.description()).toBe(null)
    expect(String(parent.locator('input'))).toBe("locator('form').locator('input')")
  })
  it('formats genuine public CSS and role query construction using existing native query metadata', () => {
    const { page } = fixture()
    expect(String(page.locator('#outer'))).toBe("locator('#outer')")
    expect(String(page.locator('#outer').locator('#inner'))).toBe(
      "locator('#outer').locator('#inner')",
    )
    expect(String(page.getByRole('button', { name: 'Submit' }))).toBe(
      "getByRole('button', { name: 'Submit' })",
    )
    expect(page.getByRole('button', { name: 'Submit' }).description()).toBe(null)
    expect(String(page.getByRole('button', { name: 'Submit' }).describe('Submit button'))).toBe(
      'Submit button',
    )
  })
  it('keeps all native query and screenshot targets identical when description metadata changes', async () => {
    const { page, runAutomation } = fixture(),
      original = page.locator('form').getByRole('button', { name: 'Submit' }).nth(1)
    const described = original.describe('Second submit')
    expect(described.nativeScreenshotDescriptor()).toEqual(original.nativeScreenshotDescriptor())
    await original.count()
    await described.count()
    expect(runAutomation).toHaveBeenCalledTimes(2)
    for (const call of runAutomation.mock.calls)
      expect(call.slice(0, 2)).toMatchObject([
        'queryEvaluate',
        { ...original.nativeScreenshotDescriptor().target, count: true },
      ])
  })
})
