// SPDX-License-Identifier: MIT
import { resolve } from 'node:path'

import { describe, expect, it, vi } from 'vitest'

import { ProtocolDownload } from '../src/browser-protocol-download.js'
import { type BrowserProtocolDriver } from '../src/browser-protocol-types.js'
import { type BrowserWire } from '../src/browser-wire.js'
import { ProtocolBrowser } from '../src/remote-browser.js'
import { type UntrustedInput } from '../src/untrusted.js'

function gate() {
  let release!: () => void
  const promise = new Promise<void>((resolve) => {
    release = resolve
  })
  return { promise, release }
}

function fixture() {
  type Listener = (params: Record<string, UntrustedInput>, sessionId?: string) => void
  const events = new Map<string, Set<Listener>>()
  let connected = true
  let finishClosed!: () => void
  const closed = new Promise<void>((resolve) => {
    finishClosed = resolve
  })
  const wire = {
    timeoutMs: 1000,
    closed,
    isConnected: () => connected,
    on(method, listener) {
      const group = events.get(method) ?? new Set<Listener>()
      events.set(method, group)
      group.add(listener)
      return () => {
        group.delete(listener)
      }
    },
    close: vi.fn(() => {
      connected = false
      finishClosed()
      return closed
    }),
    request: vi.fn<BrowserWire['request']>().mockResolvedValue({}),
    stats: () => ({
      sent: 0,
      received: 0,
      sentBytes: 0,
      receivedBytes: 0,
      pendingPeak: 0,
      pending: 0,
      eventListeners: [...events.values()].reduce((total, group) => total + group.size, 0),
    }),
  } satisfies Pick<
    BrowserWire,
    'timeoutMs' | 'closed' | 'isConnected' | 'on' | 'close' | 'request' | 'stats'
  >
  const driver = {
    backend: 'cdp',
    wire: wire as UntrustedInput as BrowserWire,
    capabilities: {
      backend: 'cdp',
      isolatedContexts: true,
      viewport: true,
      screenshots: true,
      fullPageScreenshot: true,
      cookies: true,
      nativeInput: true,
      rawProtocol: true,
      nativeCapture: false,
      nativeViewer: false,
      downloads: true,
      uploads: true,
    },
    version: 'controls mock',
    initialize: vi.fn<BrowserProtocolDriver['initialize']>().mockResolvedValue(),
    contexts: vi.fn<BrowserProtocolDriver['contexts']>().mockResolvedValue(['']),
    pages: vi
      .fn<BrowserProtocolDriver['pages']>()
      .mockResolvedValue([{ id: 'page', contextId: '', url: 'about:blank' }]),
    newContext: vi.fn<BrowserProtocolDriver['newContext']>().mockResolvedValue('context'),
    closeContext: vi.fn<BrowserProtocolDriver['closeContext']>().mockResolvedValue(),
    newPage: vi
      .fn<BrowserProtocolDriver['newPage']>()
      .mockResolvedValue({ id: 'page', contextId: '', url: 'about:blank' }),
    closePage: vi.fn<BrowserProtocolDriver['closePage']>().mockResolvedValue(),
    navigate: vi.fn<BrowserProtocolDriver['navigate']>().mockResolvedValue(),
    evaluate: vi.fn<BrowserProtocolDriver['evaluate']>().mockResolvedValue({ x: 12, y: 34 }),
    screenshot: vi.fn<BrowserProtocolDriver['screenshot']>().mockResolvedValue(Buffer.alloc(0)),
    setViewport: vi.fn<BrowserProtocolDriver['setViewport']>().mockResolvedValue(),
    click: vi.fn<BrowserProtocolDriver['click']>().mockResolvedValue(),
    insertText: vi.fn<BrowserProtocolDriver['insertText']>().mockResolvedValue(),
    press: vi.fn<BrowserProtocolDriver['press']>().mockResolvedValue(),
    scroll: vi.fn<BrowserProtocolDriver['scroll']>().mockResolvedValue(),
    cookies: vi.fn<BrowserProtocolDriver['cookies']>().mockResolvedValue([]),
    setCookies: vi.fn<BrowserProtocolDriver['setCookies']>().mockResolvedValue(),
    preparePage: vi.fn<NonNullable<BrowserProtocolDriver['preparePage']>>().mockResolvedValue(),
    setInputFiles: vi.fn<NonNullable<BrowserProtocolDriver['setInputFiles']>>().mockResolvedValue(),
    subscribeDownloads: vi
      .fn<NonNullable<BrowserProtocolDriver['subscribeDownloads']>>()
      .mockResolvedValue(() => undefined),
    disposeResources: vi
      .fn<NonNullable<BrowserProtocolDriver['disposeResources']>>()
      .mockResolvedValue(),
    closeBrowser: vi.fn<BrowserProtocolDriver['closeBrowser']>().mockResolvedValue(),
  } satisfies BrowserProtocolDriver
  const browser = new ProtocolBrowser(driver)
  return {
    driver,
    wire,
    browser,
    emit(method: string, params: Record<string, UntrustedInput>) {
      for (const listener of events.get(method) ?? []) listener(params)
    },
  }
}

describe('external browser upload/download facade ownership', () => {
  it('awaits download registration before executing a concurrently requested export click', async () => {
    const test = fixture()
    const registered = gate()
    const download = new ProtocolDownload(
      'https://example.test/export',
      'report.csv',
      '/unused',
      () => Promise.resolve(),
      () => true,
    )
    let deliver!: (value: ProtocolDownload) => void
    test.driver.subscribeDownloads.mockImplementation((_id, listener) => {
      deliver = listener
      return registered.promise.then(() => () => undefined)
    })
    test.driver.click.mockImplementation(() => {
      deliver(download)
      return Promise.resolve()
    })
    const page = await test.browser.newPage()
    const waiting = page.waitForEvent('download', { timeout: 1000 })
    const clicking = page.locator('#export').click()
    await vi.waitFor(() => expect(test.driver.subscribeDownloads).toHaveBeenCalledTimes(1))
    expect(test.driver.click).not.toHaveBeenCalled()
    expect(test.driver.evaluate).not.toHaveBeenCalled()
    registered.release()
    await clicking
    expect(await waiting).toBe(download)
    expect(test.driver.click).toHaveBeenCalledWith('page', 12, 34, 'left', 1, undefined)
    await test.browser.close()
  })

  it('rejects event waiters on explicit page close and detaches a late download subscription', async () => {
    const test = fixture()
    const registered = gate()
    const detach = vi.fn()
    test.driver.subscribeDownloads.mockReturnValue(registered.promise.then(() => detach))
    const page = await test.browser.newPage()
    const downloadWait = expect(page.waitForEvent('download', { timeout: 0 })).rejects.toThrow(
      'page is closed',
    )
    const popupWait = expect(page.waitForEvent('popup', { timeout: 0 })).rejects.toThrow(
      'page is closed',
    )
    await page.close()
    await Promise.all([downloadWait, popupWait])
    registered.release()
    await vi.waitFor(() => expect(detach).toHaveBeenCalledTimes(1))
    await test.browser.close()
  })

  it('rejects event waiters when the browser reports external page destruction', async () => {
    const test = fixture()
    const page = await test.browser.newPage()
    const waiting = expect(page.waitForEvent('popup', { timeout: 0 })).rejects.toThrow(
      'page is closed',
    )
    test.emit('Target.targetDestroyed', { targetId: 'page' })
    await waiting
    expect(page.isClosed()).toBe(true)
    await test.browser.close()
  })

  it('surfaces subscription failure to event consumers and contains fire-and-forget registration rejection', async () => {
    const test = fixture()
    test.driver.subscribeDownloads.mockRejectedValue(new Error('download policy rejected'))
    const page = await test.browser.newPage()
    await expect(page.waitForEvent('download')).rejects.toThrow('download policy rejected')
    page.on('download', () => undefined)
    await new Promise<void>((resolve) => setImmediate(resolve))
    expect(test.driver.subscribeDownloads).toHaveBeenCalledTimes(2)
    await test.browser.close()
  })

  it('resolves relative upload paths and preserves the explicit empty list used to clear inputs', async () => {
    const test = fixture()
    const page = await test.browser.newPage()
    await page.locator('#upload').setInputFiles(['fixtures/first.csv', './fixtures/second.txt'])
    expect(test.driver.setInputFiles).toHaveBeenCalledWith(
      'page',
      expect.stringContaining('prepareUpload'),
      [resolve('fixtures/first.csv'), resolve('./fixtures/second.txt')],
      undefined,
    )
    await page.locator('#upload').setInputFiles([])
    expect(test.driver.setInputFiles.mock.calls[1]?.[2]).toEqual([])
    await test.browser.close()
  })

  it('awaits resource disposal with attached Chrome still connected before completing close', async () => {
    const test = fixture()
    const disposed = gate()
    let finished = false
    let connectedAtDisposal = false
    test.driver.disposeResources.mockImplementation(() => {
      connectedAtDisposal = test.wire.isConnected()
      return disposed.promise
    })
    const closing = test.browser.close().then(() => {
      finished = true
    })
    await vi.waitFor(() => expect(test.driver.disposeResources).toHaveBeenCalledTimes(1))
    const before = {
      finished,
      connectedAtDisposal,
      wireCloseCalls: test.wire.close.mock.calls.length,
    }
    disposed.release()
    await closing
    expect(before).toEqual({ finished: false, connectedAtDisposal: true, wireCloseCalls: 0 })
    expect(test.wire.close).toHaveBeenCalledTimes(1)
  })
})
