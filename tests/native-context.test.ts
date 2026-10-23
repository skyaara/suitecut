// SPDX-License-Identifier: MIT
import { fileURLToPath } from 'node:url'

import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import {
  closeNativeContextPage,
  createNativeContextBoundPage,
  getNativePageContext,
  isNativeContextPageClosed,
  nativePageOpener,
  onNativeContextPageEvent,
} from '../src/native-context-page.js'
import {
  NativeBrowserContexts,
  type NativeContextEvent,
  type NativeContextTransport,
} from '../src/native-context.js'
import { NativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const lifecycle = new AbortController()
  const listeners = new Set<(event: NativeContextEvent) => void>()
  const pages = new Map<number, NativeBrowser>()
  const runContext = vi.fn<NativeContextTransport['runContext']>()
  const transport: NativeContextTransport = {
    signal: lifecycle.signal,
    runContext,
    pageSource(id) {
      let page = pages.get(id)
      if (!page) {
        page = {
          signal: lifecycle.signal,
          runAutomation: vi.fn(() => Promise.resolve(undefined)),
        } as UntrustedInput as NativeBrowser
        pages.set(id, page)
      }
      return page
    },
    onContextEvent(listener) {
      listeners.add(listener)
      return () => listeners.delete(listener)
    },
  }
  const browser = new NativeBrowserContexts(transport)
  return {
    browser,
    runContext,
    lifecycle,
    listeners,
    emit(event: NativeContextEvent) {
      for (const listener of [...listeners]) listener(event)
    },
  }
}

describe('native context lifecycle projection', () => {
  it('projects an unbound real page close from its page signal without closing the process', async () => {
    const process = new AbortController()
    const lifecycle = new AbortController()
    const runContext = vi.fn(() => Promise.resolve(undefined))
    const close = vi.fn(() => Promise.resolve())
    const source = {
      pageId: 1,
      signal: process.signal,
      pageSignal: lifecycle.signal,
      runContext,
      close,
    } as UntrustedInput as NativeBrowser
    const page = new NativePage(source)
    await page.close({ runBeforeUnload: true })
    expect(runContext).toHaveBeenCalledExactlyOnceWith('closePage', {
      pageId: 1,
      runBeforeUnload: true,
    })
    expect(page.isClosed()).toBe(false)
    expect(close).not.toHaveBeenCalled()
    lifecycle.abort(new Error('observed page close'))
    expect(page.isClosed()).toBe(true)
    expect(process.signal.aborted).toBe(false)
  })

  it('keeps beforeunload dispatch live until an actual native close event and permits later force close', async () => {
    const f = fixture()
    const context = f.browser.defaultContext()
    const page = context.pages()[0]!
    const closed = vi.fn()
    onNativeContextPageEvent(page, 'close', closed)
    f.runContext.mockResolvedValue(undefined)
    await page.close({ runBeforeUnload: true, reason: 'authored close' })
    expect(f.runContext).toHaveBeenLastCalledWith(
      'closePage',
      expect.objectContaining({
        pageId: 1,
        runBeforeUnload: true,
        reason: 'authored close',
      }),
    )
    expect(isNativeContextPageClosed(page)).toBe(false)
    expect(context.pages()).toContain(page)
    expect(closed).not.toHaveBeenCalled()
    await page.close({ runBeforeUnload: true })
    expect(f.runContext).toHaveBeenCalledTimes(2)
    // A real native dismissal emits no page-closed event. A later default
    // close must still send its own request and wait for that native lifecycle.
    f.runContext.mockImplementationOnce(() => {
      f.emit({ event: 'page-closed', contextId: 1, pageId: 1 })
      return Promise.resolve(undefined)
    })
    await page.close()
    expect(f.runContext).toHaveBeenCalledTimes(3)
    expect(isNativeContextPageClosed(page)).toBe(true)
    expect(closed).toHaveBeenCalledOnce()
  })

  it('forwards bounded origin-scoped credential arrays and send modes to the real native context', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    const credentials = [
      { username: 'fallback', password: 'secret' },
      {
        username: 'specific',
        password: 'answer',
        origin: 'https://example.test',
        send: 'always' as const,
      },
    ]
    const context = await f.browser.newContext({ httpCredentials: credentials })
    expect(f.runContext).toHaveBeenCalledWith('create', { httpCredentials: credentials })
    expect(() => context.request).not.toThrow()
    f.emit({ event: 'context-closed', contextId: 2 })
    await expect(context.request.get('https://example.test')).rejects.toThrow('closed')
    const invalid = fixture()
    await expect(
      invalid.browser.newContext({
        httpCredentials: Array.from({ length: 65 }, () => credentials[0]!),
      }),
    ).rejects.toThrow()
    expect(invalid.runContext).not.toHaveBeenCalled()
  })

  it('updates native credentials without replacing the canonical API controller', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    const context = await f.browser.newContext()
    const request = context.request
    const credentials = { username: 'changed', password: 'secret', send: 'always' as const }
    f.runContext.mockResolvedValueOnce(undefined)
    await context.setHTTPCredentials(credentials)
    expect(f.runContext).toHaveBeenLastCalledWith('setHTTPCredentials', {
      contextId: 2,
      credentials,
    })
    expect(context.request).toBe(request)
    f.runContext.mockResolvedValueOnce(undefined)
    await context.setHTTPCredentials(null)
    expect(f.runContext).toHaveBeenLastCalledWith('setHTTPCredentials', {
      contextId: 2,
      credentials: null,
    })
    f.emit({ event: 'context-closed', contextId: 2 })
    await expect(context.setHTTPCredentials(credentials)).rejects.toThrow('closed')
  })

  it('preserves page identity across native creation event and command response', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    const context = await f.browser.newContext()
    const created = vi.fn()
    context.on('page', created)
    f.runContext.mockImplementationOnce(() => {
      f.emit({ event: 'page-created', contextId: 2, pageId: 3 })
      return Promise.resolve({ contextId: 2, pageId: 3 })
    })
    const page = await context.newPage()
    expect(created).toHaveBeenCalledOnce()
    expect(created).toHaveBeenCalledWith(page)
    expect(context.pages()).toEqual([page])
    expect(getNativePageContext(page)).toBe(context)
    expect(createNativeContextBoundPage(page.source)).toBe(page)
    expect(f.listeners.size).toBe(1)
    expect(f.browser.contexts()).toHaveLength(2)
  })

  it('delegates owned page/context lifetime to one native command', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2, pageId: 2 })
    const page = await f.browser.newPage({ baseURL: 'https://example.test' })
    expect(getNativePageContext(page).baseURL).toBe('https://example.test')
    f.emit({ event: 'page-closed', contextId: 2, pageId: 2 })
    f.emit({ event: 'context-closed', contextId: 2 })
    expect(isNativeContextPageClosed(page)).toBe(true)
    expect(f.browser.contexts()).toHaveLength(1)
    expect(f.runContext).toHaveBeenCalledExactlyOnceWith('newOwnedPage', {
      baseURL: 'https://example.test',
    })
  })

  it('projects genuine native popup ownership and closes exactly once', async () => {
    const f = fixture()
    const context = f.browser.defaultContext()
    const opener = context.pages()[0]!
    const popups = vi.fn()
    onNativeContextPageEvent(opener, 'popup', popups)
    f.emit({ event: 'page-created', contextId: 1, pageId: 2, openerId: 1 })
    const popup = context.pages()[1]!
    expect(popups).toHaveBeenCalledWith(popup)
    expect(await nativePageOpener(popup)).toBe(opener)
    const closed = vi.fn()
    onNativeContextPageEvent(popup, 'close', closed)
    f.runContext.mockImplementationOnce(() => {
      f.emit({ event: 'page-closed', contextId: 1, pageId: 2, openerId: 1 })
      return Promise.resolve(undefined)
    })
    await Promise.all([closeNativeContextPage(popup), closeNativeContextPage(popup)])
    expect(f.runContext).toHaveBeenCalledExactlyOnceWith('closePage', {
      pageId: 2,
      traceStack: [
        expect.objectContaining({
          file: fileURLToPath(import.meta.url),
          line: expect.any(Number),
          column: expect.any(Number),
        }),
      ],
    })
    expect(closed).toHaveBeenCalledOnce()
    expect(isNativeContextPageClosed(popup)).toBe(true)
    expect(context.pages()).toEqual([opener])
  })

  it('aborts all contexts, releases its subscription, and rejects later native work', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    const context = await f.browser.newContext()
    f.lifecycle.abort(new Error('native process ended'))
    expect(f.listeners.size).toBe(0)
    expect(f.browser.contexts()).toEqual([])
    await expect(context.newPage()).rejects.toThrow('context is closed')
    expect(f.runContext).toHaveBeenCalledTimes(1)
  })

  it('validates cookie options and preserves native cookie values', async () => {
    const f = fixture()
    const context = f.browser.defaultContext()
    const cookie = {
      name: 'whitespace',
      value: ' a ',
      domain: 'example.test',
      path: '/',
      expires: -1,
      httpOnly: true,
      secure: false,
      sameSite: 'Lax',
    }
    f.runContext.mockResolvedValueOnce({ cookies: [cookie] })
    expect(await context.cookies('http://example.test/')).toEqual([cookie])
    expect(f.runContext).toHaveBeenCalledWith('cookies', {
      contextId: 1,
      urls: ['http://example.test/'],
    })
    await context.clearCookies({ name: /x/ })
    expect(f.runContext).toHaveBeenLastCalledWith('clearCookies', {
      contextId: 1,
      filter: { name: { sourceJSON: '"x"', flags: '' } },
    })
    await expect(
      context.clearCookies({ name: 42 } as UntrustedInput as { name: string }),
    ).rejects.toThrow()
    expect(f.runContext).toHaveBeenCalledTimes(2)
  })
  it('keeps the canonical API request available after genuine native close metadata and reports its actual reason asynchronously', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    const context = await f.browser.newContext()
    const api = context.request
    f.runContext.mockResolvedValueOnce({
      contextId: 2,
      closedReason: 'Native physical context close reason',
    })
    await context.close()
    expect(context.request).toBe(api)
    await expect(api.get('https://example.test')).rejects.toThrow(
      'Native physical context close reason',
    )
    expect(f.runContext.mock.calls.map((call) => call[0])).toEqual(['create', 'close'])
  })
  it('creates no native profile when the API controller is first accessed after actual context closure', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    const context = await f.browser.newContext()
    f.emit({ event: 'context-closed', contextId: 2, closedReason: 'Actual native event reason' })
    const api = context.request
    expect(context.request).toBe(api)
    await expect(api.get('https://example.test')).rejects.toThrow('Actual native event reason')
    expect(f.runContext).toHaveBeenCalledOnce()
  })
})
