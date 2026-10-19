// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import {
  type BrowserProtocol,
  type BrowserProtocolDriver,
  type DriverPage,
} from '../src/browser-protocol-types.js'
import { type BrowserWire } from '../src/browser-wire.js'
import { ProtocolBrowser } from '../src/remote-browser.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fakeDriver(backend: BrowserProtocol = 'bidi') {
  type Listener = (params: Record<string, UntrustedInput>, sessionId?: string) => void
  const listeners = new Map<string, Set<Listener>>()
  const initial = backend === 'bidi' ? 'default' : ''
  const state = {
    contexts: [initial, 'private'],
    pages: [{ id: 'page', contextId: initial, url: 'about:blank' }] as DriverPage[],
    nextPage: 0,
  }
  let connected = true
  let finishClosed!: () => void
  const closed = new Promise<void>((resolve) => {
    finishClosed = resolve
  })
  const closeWire = vi.fn(() => {
    connected = false
    finishClosed()
    return closed
  })
  const wire = {
    timeoutMs: 30_000,
    closed,
    isConnected: () => connected,
    on(method, listener) {
      const set = listeners.get(method) ?? new Set<Listener>()
      set.add(listener)
      listeners.set(method, set)
      return () => {
        set.delete(listener)
      }
    },
    close: closeWire,
    request: vi.fn<BrowserWire['request']>().mockResolvedValue({}),
    stats() {
      let eventListeners = 0
      for (const set of listeners.values()) eventListeners += set.size
      return {
        sent: 0,
        received: 0,
        sentBytes: 0,
        receivedBytes: 0,
        pendingPeak: 0,
        pending: 0,
        eventListeners,
      }
    },
  } satisfies Pick<
    BrowserWire,
    'timeoutMs' | 'closed' | 'isConnected' | 'on' | 'close' | 'request' | 'stats'
  >
  const driver = {
    backend,
    wire: wire as UntrustedInput as BrowserWire,
    capabilities: {
      backend,
      isolatedContexts: true,
      viewport: true,
      screenshots: true,
      fullPageScreenshot: true,
      cookies: true,
      nativeInput: true,
      rawProtocol: true,
      nativeCapture: false,
      nativeViewer: false,
    },
    version: `${backend} test browser`,
    initialize: vi.fn<BrowserProtocolDriver['initialize']>().mockResolvedValue(),
    contexts: vi.fn<BrowserProtocolDriver['contexts']>(() => Promise.resolve([...state.contexts])),
    pages: vi.fn<BrowserProtocolDriver['pages']>(() =>
      Promise.resolve(state.pages.map((page) => ({ ...page }))),
    ),
    newContext: vi.fn<BrowserProtocolDriver['newContext']>().mockResolvedValue('private'),
    closeContext: vi.fn<BrowserProtocolDriver['closeContext']>((id) => {
      state.contexts = state.contexts.filter((value) => value !== id)
      state.pages = state.pages.filter((page) => page.contextId !== id)
      return Promise.resolve()
    }),
    newPage: vi.fn<BrowserProtocolDriver['newPage']>((contextId = initial) => {
      const page = { id: `new-${++state.nextPage}`, contextId, url: 'about:blank' }
      state.pages.push(page)
      return Promise.resolve(page)
    }),
    closePage: vi.fn<BrowserProtocolDriver['closePage']>((id) => {
      state.pages = state.pages.filter((page) => page.id !== id)
      return Promise.resolve()
    }),
    navigate: vi.fn<BrowserProtocolDriver['navigate']>().mockResolvedValue(),
    evaluate: vi.fn<BrowserProtocolDriver['evaluate']>().mockResolvedValue('test page'),
    screenshot: vi.fn<BrowserProtocolDriver['screenshot']>().mockResolvedValue(Buffer.from('png')),
    setViewport: vi.fn<BrowserProtocolDriver['setViewport']>().mockResolvedValue(),
    click: vi.fn<BrowserProtocolDriver['click']>().mockResolvedValue(),
    insertText: vi.fn<BrowserProtocolDriver['insertText']>().mockResolvedValue(),
    press: vi.fn<BrowserProtocolDriver['press']>().mockResolvedValue(),
    scroll: vi.fn<BrowserProtocolDriver['scroll']>().mockResolvedValue(),
    cookies: vi.fn<BrowserProtocolDriver['cookies']>().mockResolvedValue([]),
    setCookies: vi.fn<BrowserProtocolDriver['setCookies']>().mockResolvedValue(),
    closeBrowser: vi.fn<BrowserProtocolDriver['closeBrowser']>().mockResolvedValue(),
  } satisfies BrowserProtocolDriver
  return {
    driver,
    state,
    wire,
    closeWire,
    emit(this: void, method: string, params: Record<string, UntrustedInput>) {
      for (const listener of listeners.get(method) ?? []) listener(params)
    },
  }
}

function gate() {
  let release!: () => void
  const promise = new Promise<void>((resolve) => {
    release = resolve
  })
  return { promise, release }
}

describe('cross-browser facade lifecycle and bounded actions', () => {
  it('allows a failed page close to be retried and shares concurrent successful closes', async () => {
    const { driver } = fakeDriver()
    const browser = new ProtocolBrowser(driver)
    const [page] = await browser.pages()
    expect(page).toBeDefined()
    driver.closePage.mockRejectedValueOnce(new Error('temporary page close failure'))
    await expect(page!.close()).rejects.toThrow('temporary page close failure')
    expect(page!.isClosed()).toBe(false)
    await expect(page!.title()).resolves.toBe('test page')
    const closing = page!.close()
    expect(page!.close()).toBe(closing)
    await closing
    expect(driver.closePage).toHaveBeenCalledTimes(2)
    expect(page!.isClosed()).toBe(true)
    await expect(page!.title()).rejects.toThrow('page is closed')
    await browser.close()
  })

  it('allows a failed context close to be retried and retires its page wrappers after success', async () => {
    const { driver } = fakeDriver()
    const browser = new ProtocolBrowser(driver)
    const context = await browser.newContext()
    const page = await context.newPage()
    driver.closeContext.mockRejectedValueOnce(new Error('temporary context close failure'))
    await expect(context.close()).rejects.toThrow('temporary context close failure')
    await expect(context.cookies()).resolves.toEqual([])
    expect(page.isClosed()).toBe(false)
    const closing = context.close()
    expect(context.close()).toBe(closing)
    await closing
    expect(driver.closeContext).toHaveBeenCalledTimes(2)
    expect(page.isClosed()).toBe(true)
    await expect(context.newPage()).rejects.toThrow('context is closed')
    await browser.close()
  })

  it('rejects closing either protocol default context while leaving its browser usable', async () => {
    for (const backend of ['cdp', 'bidi'] as const) {
      const { driver } = fakeDriver(backend)
      const browser = new ProtocolBrowser(driver)
      await expect(browser.defaultContext().close()).rejects.toThrow(
        'default browser context cannot be closed',
      )
      expect(browser.isConnected()).toBe(true)
      expect(driver.closeContext).not.toHaveBeenCalled()
      const page = await browser.defaultContext().newPage()
      await expect(page.title()).resolves.toBe('test page')
      await browser.close()
    }
  })

  it('applies the owned default viewport to pages created through browser and context factories', async () => {
    const { driver } = fakeDriver()
    const viewport = { width: 1440, height: 900 }
    const browser = new ProtocolBrowser(driver, false, undefined, viewport)
    const direct = await browser.newPage()
    const defaultPage = await browser.defaultContext().newPage()
    const context = await browser.newContext()
    const privatePage = await context.newPage()
    expect(driver.newPage.mock.calls).toEqual([[undefined], ['default'], ['private']])
    expect(driver.setViewport.mock.calls).toEqual([
      [direct.id, viewport],
      [defaultPage.id, viewport],
      [privatePage.id, viewport],
    ])
    expect(direct.viewportSize()).toEqual(viewport)
    expect((await context.pages()).map((page) => page.id)).toEqual([privatePage.id])
    await browser.close()
  })

  it('cleans up a newly created page if its default viewport cannot be applied', async () => {
    const { driver } = fakeDriver()
    const browser = new ProtocolBrowser(driver, false, undefined, { width: 1280, height: 720 })
    driver.setViewport.mockRejectedValueOnce(new Error('viewport rejected'))
    await expect(browser.newPage()).rejects.toThrow('viewport rejected')
    expect(driver.closePage).toHaveBeenCalledWith('new-1')
    expect((await browser.pages()).map((page) => page.id)).toEqual(['page'])
    await browser.close()
  })

  it('serializes page actions and recovers the queue after an action rejects', async () => {
    const { driver } = fakeDriver()
    const browser = new ProtocolBrowser(driver)
    const page = await browser.newPage()
    const blocked = gate()
    const events: string[] = []
    driver.insertText.mockImplementation((_, text) => {
      events.push(text)
      return blocked.promise
    })
    driver.press.mockImplementation(() => {
      events.push('failed press')
      return Promise.reject(new Error('injected input failure'))
    })
    driver.click.mockImplementation(() => {
      events.push('click')
      return Promise.resolve()
    })
    const first = page.keyboard.type('first')
    const second = page.keyboard.press('Enter')
    const failed = expect(second).rejects.toThrow('injected input failure')
    const third = page.mouse.click(12, 34)
    await vi.waitFor(() => expect(events).toEqual(['first']))
    blocked.release()
    await Promise.all([first, failed, third])
    expect(events).toEqual(['first', 'failed press', 'click'])
    await expect(page.keyboard.type('recovered')).resolves.toBeUndefined()
    expect(events).toEqual(['first', 'failed press', 'click', 'recovered'])
    await browser.close()
  })

  it('uses driver-owned locator preparation and leaves absent-locator waits outside input focus', async () => {
    const { driver } = fakeDriver('cdp')
    const clickLocator = vi
      .fn<NonNullable<BrowserProtocolDriver['clickLocator']>>()
      .mockImplementation((_id, prepare) => prepare())
    const enhanced = { ...driver, clickLocator }
    const browser = new ProtocolBrowser(enhanced)
    const page = await browser.newPage()
    driver.evaluate.mockResolvedValue({ x: 10, y: 20 })
    await page.locator('button').click()
    expect(clickLocator).toHaveBeenCalledTimes(1)
    expect(driver.click).not.toHaveBeenCalled()
    driver.evaluate.mockResolvedValue({ retry: 'Element is hidden' })
    page.setDefaultTimeout(5)
    await expect(page.locator('#missing').click()).rejects.toThrow('Element is hidden')
    expect(clickLocator).toHaveBeenCalledTimes(1)
    await browser.close()
  })

  it('expires queued locator preparation before it can send a pointer press', async () => {
    const { driver } = fakeDriver('cdp')
    const blocked = gate()
    const entered = gate()
    const clickLocator = vi
      .fn<NonNullable<BrowserProtocolDriver['clickLocator']>>()
      .mockImplementation(async (_id, prepare) => {
        entered.release()
        await blocked.promise
        return prepare()
      })
    const browser = new ProtocolBrowser({ ...driver, clickLocator })
    const page = await browser.newPage()
    driver.evaluate.mockResolvedValue({ x: 10, y: 20 })
    page.setDefaultTimeout(5)
    const failure = expect(page.locator('button').click()).rejects.toThrow('prepareClick timed out')
    await entered.promise
    await new Promise((resolve) => globalThis.setTimeout(resolve, 15))
    blocked.release()
    await failure
    expect(driver.evaluate).toHaveBeenCalledTimes(1)
    await browser.close()
  })

  it('cancels prepared locator input when its page closes before down', async () => {
    const { driver } = fakeDriver('cdp')
    const blocked = gate()
    const entered = gate()
    const clickLocator = vi
      .fn<NonNullable<BrowserProtocolDriver['clickLocator']>>()
      .mockImplementation(async (_id, prepare, _frameId, beforeDown) => {
        const point = await prepare()
        entered.release()
        await blocked.promise
        beforeDown?.()
        return point
      })
    const browser = new ProtocolBrowser({ ...driver, clickLocator })
    const page = await browser.newPage()
    driver.evaluate.mockResolvedValue({ x: 10, y: 20 })
    const failure = expect(page.locator('button').click()).rejects.toThrow('page is closed')
    await entered.promise
    await page.close()
    blocked.release()
    await failure
    await browser.close()
  })

  it('bounds pending page actions at 256 and admits work again after the backlog drains', async () => {
    const { driver } = fakeDriver()
    const browser = new ProtocolBrowser(driver)
    const page = await browser.newPage()
    const blocked = gate()
    driver.insertText.mockImplementationOnce(() => blocked.promise)
    const accepted = Array.from({ length: 256 }, (_, i) => page.keyboard.type(`item-${i}`))
    await expect(page.keyboard.type('overflow')).rejects.toThrow('action queue limit reached')
    await vi.waitFor(() => expect(driver.insertText).toHaveBeenCalledTimes(1))
    blocked.release()
    await Promise.all(accepted)
    expect(driver.insertText).toHaveBeenCalledTimes(256)
    await expect(page.keyboard.type('after drain')).resolves.toBeUndefined()
    expect(driver.insertText).toHaveBeenCalledTimes(257)
    await browser.close()
  })

  it('retires stale page wrappers on external listings and returns fresh wrappers for reused ids', async () => {
    const { driver, state } = fakeDriver()
    const browser = new ProtocolBrowser(driver)
    const original = (await browser.pages())[0]!
    expect((await browser.pages())[0]).toBe(original)
    state.pages = []
    expect(await browser.pages()).toEqual([])
    expect(original.isClosed()).toBe(true)
    await expect(original.title()).rejects.toThrow('page is closed')
    state.pages = [{ id: 'page', contextId: 'default', url: 'about:blank' }]
    const replacement = (await browser.pages())[0]!
    expect(replacement).not.toBe(original)
    expect(replacement.isClosed()).toBe(false)
    await browser.close()
  })

  it('retires externally removed contexts and their pages when refreshing contexts', async () => {
    const { driver, state } = fakeDriver()
    const browser = new ProtocolBrowser(driver)
    const context = await browser.newContext()
    const page = await context.newPage()
    state.contexts = ['default']
    state.pages = state.pages.filter((value) => value.contextId !== 'private')
    expect((await browser.contexts()).map((value) => value.id)).toEqual(['default'])
    await expect(context.newPage()).rejects.toThrow('context is closed')
    expect(() => context.cookies()).toThrow('context is closed')
    expect(page.isClosed()).toBe(true)
    expect(driver.closeContext).not.toHaveBeenCalled()
    await browser.close()
  })

  it('retires pages immediately on CDP and BiDi target-destroyed events', async () => {
    for (const backend of ['cdp', 'bidi'] as const) {
      const { driver, emit } = fakeDriver(backend)
      const browser = new ProtocolBrowser(driver)
      const page = await browser.newPage()
      if (backend === 'cdp') emit('Target.targetDestroyed', { targetId: page.id })
      else emit('browsingContext.contextDestroyed', { context: page.id })
      expect(page.isClosed()).toBe(true)
      await expect(page.title()).rejects.toThrow('page is closed')
      await browser.close()
    }
  })

  it('disconnects attachments exactly once without closing the browser and removes listener ownership', async () => {
    const { driver, closeWire } = fakeDriver()
    const browser = new ProtocolBrowser(driver)
    const page = await browser.newPage()
    expect(browser.connectionStats().eventListeners).toBe(3)
    const closing = browser.close()
    expect(browser.close()).toBe(closing)
    await closing
    await browser.closed
    expect(driver.closeBrowser).not.toHaveBeenCalled()
    expect(closeWire).toHaveBeenCalledTimes(1)
    expect(browser.isConnected()).toBe(false)
    expect(page.isClosed()).toBe(true)
    expect(browser.connectionStats()).toMatchObject({ pending: 0, eventListeners: 0 })
    await expect(browser.newPage()).rejects.toThrow('closed or disconnected')
  })

  it('shuts down the browser only when explicitly requested and still disconnects on shutdown failure', async () => {
    for (const fails of [false, true]) {
      const { driver, closeWire } = fakeDriver()
      const browser = new ProtocolBrowser(driver, true)
      if (fails) driver.closeBrowser.mockRejectedValueOnce(new Error('shutdown rejected'))
      const closing = browser.close()
      expect(browser.close()).toBe(closing)
      if (fails) await expect(closing).rejects.toThrow('Browser cleanup failed')
      else await expect(closing).resolves.toBeUndefined()
      expect(driver.closeBrowser).toHaveBeenCalledTimes(1)
      expect(closeWire).toHaveBeenCalledTimes(1)
      expect(browser.connectionStats()).toMatchObject({ pending: 0, eventListeners: 0 })
    }
  })

  it('ends only its SDK-owned automation session before disconnecting an attachment', async () => {
    const { driver, closeWire } = fakeDriver()
    const order: string[] = []
    const disposeSession = vi.fn(() => {
      order.push('session')
      expect(closeWire).not.toHaveBeenCalled()
      return Promise.resolve()
    })
    const browser = new ProtocolBrowser({ ...driver, disposeSession })
    await browser.close()
    expect(disposeSession).toHaveBeenCalledTimes(1)
    expect(driver.closeBrowser).not.toHaveBeenCalled()
    expect(closeWire).toHaveBeenCalledTimes(1)
    expect(order).toEqual(['session'])
  })

  it('still releases the connection when automation session cleanup fails', async () => {
    const { driver, closeWire } = fakeDriver()
    const disposeSession = vi.fn(() => Promise.reject(new Error('session end failed')))
    const browser = new ProtocolBrowser({ ...driver, disposeSession })
    await expect(browser.close()).rejects.toThrow('Browser cleanup failed')
    expect(closeWire).toHaveBeenCalledTimes(1)
    expect(browser.connectionStats()).toMatchObject({ pending: 0, eventListeners: 0 })
  })
})
