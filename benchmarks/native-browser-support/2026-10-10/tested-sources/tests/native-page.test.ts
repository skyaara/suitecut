import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'

function source() {
  let fail: (error: Error) => void = () => undefined
  let close = (): void => undefined
  const observeFailure = vi.fn((listener: (error: Error) => void) => {
    fail = listener
    return () => undefined
  })
  const observeClose = vi.fn((listener: () => void) => {
    close = listener
    return () => undefined
  })
  const browser: NativeBrowser = {
    state: 'running',
    width: 2,
    height: 2,
    pixelFormat: 'bgra',
    framesPerSecond: 30,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    onFailure: observeFailure,
    onClose: observeClose,
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({ result: { value: 42 } }),
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    close: () => Promise.resolve(),
  }
  return {
    browser,
    observeFailure,
    observeClose,
    fail: (error: Error) => fail(error),
    close: () => close(),
  }
}

describe('native page source lifetime', () => {
  it('bounds lifetime observers across thousands of wrappers and aborts on close', async () => {
    const s = source()
    const pages = Array.from({ length: 2000 }, () => createNativePage(s.browser))
    expect(s.observeFailure).toHaveBeenCalledOnce()
    expect(s.observeClose).toHaveBeenCalledOnce()
    const waiting = expect(pages[1999]?.waitForTimeout(5000)).rejects.toThrow()
    s.close()
    await waiting
    expect(() => pages[0]?.goto('https://example.com')).toThrow('Native source closed')
  })

  it('keeps caller cancellation local while sharing source failure', async () => {
    const s = source(),
      controller = new AbortController()
    const first = createNativePage(s.browser, controller.signal)
    const second = createNativePage(s.browser)
    controller.abort(new Error('Caller cancelled'))
    expect(() => first.goto('https://example.com')).toThrow('Caller cancelled')
    await expect(second.evaluate(() => 42)).resolves.toBe(42)
    const waiting = expect(second.waitForTimeout(5000)).rejects.toThrow()
    s.fail(new Error('Renderer stopped'))
    await waiting
    expect(() => second.goto('https://example.com')).toThrow('Renderer stopped')
  })
})
