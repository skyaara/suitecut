// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeBrowserContexts } from '../src/native-context.js'
import { type NativeContextEvent } from '../src/native-protocol.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const signal = new AbortController().signal
  const sources = new Map<number, NativeBrowser>()
  let receive: (event: NativeContextEvent) => void = () => undefined
  const source = (id: number): NativeBrowser => {
    let current = sources.get(id)
    if (!current) {
      current = {
        signal,
        pageId: id,
        runAutomation: () => Promise.resolve(undefined),
      } as UntrustedInput as NativeBrowser
      sources.set(id, current)
    }
    return current
  }
  const context = new NativeBrowserContexts({
    signal,
    pageSource: source,
    runContext: () => Promise.resolve(undefined),
    onContextEvent: (listener) => {
      receive = listener
      return () => undefined
    },
  }).defaultContext()
  const created = (id: number, openerId = 1) =>
    receive({ event: 'page-created', contextId: context.id, pageId: id, openerId })
  const closed = (id: number) =>
    receive({ event: 'page-closed', contextId: context.id, pageId: id })
  return { context, created, closed }
}

describe('actual Page announcement after canonical owner adoption', () => {
  it('emits the real later Page and popup announcements once using the already adopted identity', async () => {
    const f = fixture(),
      opener = f.context.pages()[0]!
    const announced = vi.fn(),
      popup = vi.fn()
    f.context.on('page', announced)
    opener.on('popup', popup)
    const page = f.context.adoptPage(7)
    expect(announced).not.toHaveBeenCalled()
    expect(popup).not.toHaveBeenCalled()
    expect(await page.opener()).toBeNull()
    f.created(7)
    expect(f.context.pageById(7)).toBe(page)
    expect(announced).toHaveBeenCalledExactlyOnceWith(page)
    expect(popup).toHaveBeenCalledExactlyOnceWith(page)
    expect(await page.opener()).toBe(opener)
    f.created(7)
    expect(announced).toHaveBeenCalledOnce()
    expect(popup).toHaveBeenCalledOnce()
  })

  it('marks announcement before reentrant delivery and rejects conflicting real opener metadata', async () => {
    const f = fixture(),
      announced = vi.fn(() => f.created(9))
    f.context.on('page', announced)
    const page = f.context.adoptPage(9)
    f.created(9)
    expect(announced).toHaveBeenCalledExactlyOnceWith(page)
    f.context.adoptPage(8)
    expect(() => f.created(9, 8)).toThrow('different actual opener')
    expect(await page.opener()).toBe(f.context.pages()[0])
    expect(announced).toHaveBeenCalledOnce()
  })

  it('does not resurrect or announce a genuinely closed canonical Page', () => {
    const f = fixture(),
      announced = vi.fn(),
      page = f.context.adoptPage(10)
    f.context.on('page', announced)
    f.closed(10)
    expect(page.isClosed()).toBe(true)
    expect(f.context.pageById(10)).toBe(page)
    expect(() => f.created(10)).toThrow('already closed')
    expect(announced).not.toHaveBeenCalled()
    expect(f.context.pages()).not.toContain(page)
  })
})
