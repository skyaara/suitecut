// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativePage } from '../src/native-page.js'
import { NativeTimeoutError } from '../src/native-timeouts.js'
import { createNativeValueCodec } from '../src/native-values.js'

// The native click gives up waiting this way once fallbackAfterMs has passed.
const covered = new Error(
  'Native click fallback\nCall log:\n - click action\n - resolving #go\n - waiting for element to receive pointer events\n - <div id="cover"> intercepts pointer events',
)
const disabled = new NativeTimeoutError(
  'Native operation timed out (Timeout exceeded)\nCall log:\n - click action\n - resolving #go\n - waiting for element to be enabled',
)

function fixture(clickError?: Error) {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>((_command, params) =>
    params?.action === 'click'
      ? clickError
        ? Promise.reject(clickError)
        : Promise.resolve(undefined)
      : Promise.resolve({ result: { value: createNativeValueCodec().encode(undefined) } }),
  )
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
const commands = (f: ReturnType<typeof fixture>) =>
  f.runAutomation.mock.calls.map((call) => call[0])

describe("locator.click({ fallback: 'js' })", () => {
  it('clicks with element.click() when the native click asks for the fallback', async () => {
    const f = fixture(covered)
    await f.page.locator('#go').click({ fallback: 'js', fallbackAfter: 250, timeout: 5000 })
    expect(commands(f)).toEqual(['locator', 'queryEvaluate'])
    expect(f.runAutomation.mock.calls[0]?.[1]).toMatchObject({
      action: 'click',
      timeoutMs: 5000,
      fallbackAfterMs: 250,
    })
    expect(f.runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('fallback')
    expect(f.runAutomation.mock.calls[1]?.[1]?.function).toEqual(
      expect.stringContaining('.click()'),
    )
  })
  it('waits 1 s by default and keeps the whole timeout for the click', async () => {
    const f = fixture()
    f.page.setDefaultTimeout(600)
    await f.page.locator('#go').click({ fallback: 'js' })
    expect(f.runAutomation.mock.calls[0]?.[1]).toMatchObject({
      timeoutMs: 600,
      fallbackAfterMs: 1000,
    })
  })
  it('leaves a click that landed, a timeout and other errors alone', async () => {
    const ok = fixture()
    await ok.page.locator('#go').click({ fallback: 'js' })
    expect(commands(ok)).toEqual(['locator'])
    const off = fixture(disabled)
    await expect(off.page.locator('#go').click({ fallback: 'js' })).rejects.toBe(disabled)
    expect(commands(off)).toEqual(['locator'])
    const strict = new Error('Strict locator matched multiple elements')
    await expect(fixture(strict).page.locator('a').click({ fallback: 'js' })).rejects.toBe(strict)
  })
  it('is off by default and refuses clicks it cannot reproduce', async () => {
    const f = fixture()
    await f.page.locator('#go').click()
    expect(f.runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('fallbackAfterMs')
    for (const options of [
      { trial: true },
      { position: { x: 1, y: 1 } },
      { button: 'right' as const },
      { modifiers: ['Shift' as const] },
    ])
      await expect(f.page.locator('#go').click({ fallback: 'js', ...options })).rejects.toThrow(
        "fallback 'js' supports only a single left click",
      )
    await expect(f.page.locator('#go').dblclick({ fallback: 'js' })).rejects.toThrow(
      "fallback 'js'",
    )
  })
})
