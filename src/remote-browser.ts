// SPDX-License-Identifier: MIT
import { writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import { setTimeout as delay } from 'node:timers/promises'

import {
  browserDOM,
  type BrowserDOMOperation,
  type BrowserDOMQuery,
  type BrowserDOMValue,
  type BrowserSelectOption,
} from './browser-dom.js'
import { launchProtocolProcess } from './browser-process.js'
import { type ProtocolDialog } from './browser-protocol-dialog.js'
import { type ProtocolDownload } from './browser-protocol-download.js'
import {
  type BrowserCookie,
  type BrowserProtocolDriver,
  type BrowserScreenshotOptions,
  type BrowserViewport,
  type ConnectBrowserOptions,
  type DriverClickPoint,
  type DriverPage,
  type LaunchProtocolBrowserOptions,
} from './browser-protocol-types.js'
import { BrowserWire, protocolRecord, protocolString } from './browser-wire.js'
import { type UntrustedInput, toError } from './untrusted.js'

export { ProtocolDownload } from './browser-protocol-download.js'
export type { ProtocolDialog } from './browser-protocol-dialog.js'
export type { BrowserSelectOption } from './browser-dom.js'
export { BrowserProtocolError } from './browser-wire.js'
export type {
  BrowserProtocol,
  BrowserProtocolCapabilities,
  BrowserCookie,
  BrowserViewport,
  BrowserScreenshotOptions,
  ConnectBrowserOptions,
  LaunchProtocolBrowserOptions,
} from './browser-protocol-types.js'

export interface BrowserNavigationOptions {
  waitUntil?: 'commit' | 'domcontentloaded' | 'load'
}
export interface BrowserTextOptions {
  exact?: boolean
}
export type BrowserRoleOptions = BrowserTextOptions & { name?: string }
const domSource = browserDOM.toString()

function viewportSize(size: BrowserViewport): void {
  if (
    !Number.isInteger(size.width) ||
    !Number.isInteger(size.height) ||
    size.width < 1 ||
    size.height < 1 ||
    size.width > 16384 ||
    size.height > 16384 ||
    size.width * size.height > 64 * 1024 * 1024
  )
    throw new Error(
      'Browser viewport must use positive integer dimensions within 16384 and a 64-megapixel area',
    )
}

function argumentSource(value: UntrustedInput): string {
  if (value === undefined) return 'undefined'
  return JSON.stringify(value, (_key, item: UntrustedInput) => {
    if (
      typeof item === 'bigint' ||
      typeof item === 'function' ||
      typeof item === 'symbol' ||
      (typeof item === 'number' && !Number.isFinite(item))
    )
      throw new Error('Cross-browser evaluation arguments must contain JSON-serializable data')
    return item
  })
}

/** Lightweight control over a browser using its own CDP or WebDriver BiDi endpoint. */
export class ProtocolBrowser {
  private readonly pageObjects = new Map<string, ProtocolPage>()
  private readonly contextObjects = new Map<string, ProtocolBrowserContext>()
  private closing: Promise<void> | undefined
  private stopping = false
  private readonly initialContextId: string
  private readonly detachEvents: (() => void)[] = []

  constructor(
    private readonly driver: BrowserProtocolDriver,
    private readonly shutdownBrowser = false,
    private readonly ownedProcess?: Awaited<ReturnType<typeof launchProtocolProcess>>,
    private readonly defaultViewport?: BrowserViewport,
  ) {
    this.initialContextId = driver.backend === 'cdp' ? '' : 'default'
    const retire = (id: string): void => {
      this.pageObjects.get(id)?.retire()
      this.pageObjects.delete(id)
    }
    this.detachEvents.push(
      driver.wire.on('Target.targetCreated', (params) => {
        const info = params.targetInfo
        if (!info || typeof info !== 'object' || !('openerId' in info)) return
        const target = info as Record<string, UntrustedInput>
        const parent =
          typeof target.openerId === 'string' ? this.pageObjects.get(target.openerId) : undefined
        if (!parent || target.type !== 'page' || typeof target.targetId !== 'string') return
        const popup = this.page({
          id: target.targetId,
          contextId: typeof target.browserContextId === 'string' ? target.browserContextId : '',
          url: typeof target.url === 'string' ? target.url : '',
        })
        void popup
          .prepare()
          .then(() => parent.emitEvent('popup', popup))
          .catch(() => undefined)
      }),
      driver.wire.on('Target.targetDestroyed', (params) => {
        if (typeof params.targetId === 'string') retire(params.targetId)
      }),
      driver.wire.on('browsingContext.contextDestroyed', (params) => {
        if (typeof params.context === 'string') retire(params.context)
      }),
    )
    void driver.wire.closed.then(() => {
      for (const page of this.pageObjects.values()) page.retire()
      this.pageObjects.clear()
      this.contextObjects.clear()
      for (const detach of this.detachEvents) detach()
      this.detachEvents.length = 0
      // An unexpectedly disconnected owned browser must not become an orphan.
      if (!this.stopping) void ownedProcess?.close().catch(() => undefined)
    })
  }

  get backend() {
    return this.driver.backend
  }
  version(): string {
    return this.driver.version
  }
  capabilities() {
    return this.driver.capabilities
  }
  isConnected(): boolean {
    return !this.stopping && this.driver.wire.isConnected()
  }
  get closed(): Promise<void> {
    return this.ownedProcess?.closed ?? this.driver.wire.closed
  }
  connectionStats() {
    return this.driver.wire.stats()
  }

  /** Escape hatch for protocol-specific features; no silent CEF feature emulation. */
  sendCommand(
    method: string,
    params: Record<string, UntrustedInput> = {},
    options: {
      sessionId?: string
      signal?: AbortSignal
      timeoutMs?: number
    } = {},
  ): Promise<Record<string, UntrustedInput>> {
    this.assertConnected()
    return this.driver.wire.request(method, params, options)
  }

  private assertConnected(): void {
    if (!this.isConnected()) throw new Error('Browser is closed or disconnected')
  }

  private context(id: string): ProtocolBrowserContext {
    let context = this.contextObjects.get(id)
    if (!context) {
      context = new ProtocolBrowserContext(this, this.driver, id)
      this.contextObjects.set(id, context)
    }
    return context
  }

  private page(info: DriverPage): ProtocolPage {
    let page = this.pageObjects.get(info.id)
    if (!page) {
      page = new ProtocolPage(this.driver, info)
      this.pageObjects.set(info.id, page)
    }
    return page
  }

  defaultContext(): ProtocolBrowserContext {
    this.assertConnected()
    return this.context(this.initialContextId)
  }

  async contexts(): Promise<ProtocolBrowserContext[]> {
    this.assertConnected()
    const ids = await this.driver.contexts()
    const live = new Set(ids)
    for (const [id, context] of this.contextObjects)
      if (!live.has(id)) {
        context.retire()
        this.contextObjects.delete(id)
        for (const [pageId, page] of this.pageObjects)
          if (page.contextId === id) {
            page.retire()
            this.pageObjects.delete(pageId)
          }
      }
    return ids.map((id) => this.context(id))
  }

  async newContext(): Promise<ProtocolBrowserContext> {
    this.assertConnected()
    return this.context(await this.driver.newContext())
  }

  async pages(): Promise<ProtocolPage[]> {
    this.assertConnected()
    const pages = await this.driver.pages()
    const live = new Set(pages.map((page) => page.id))
    for (const [id, page] of this.pageObjects)
      if (!live.has(id)) {
        page.retire()
        this.pageObjects.delete(id)
      }
    const wrappers = pages.map((info) => this.page(info))
    await Promise.all(wrappers.map((page) => page.prepare()))
    return wrappers
  }

  async newPage(): Promise<ProtocolPage> {
    return this.createPage()
  }

  /**
   * Internal lifecycle operation.
   * @internal
   */
  async createPage(contextId?: string): Promise<ProtocolPage> {
    this.assertConnected()
    const page = this.page(await this.driver.newPage(contextId))
    try {
      await page.prepare()
      if (this.defaultViewport) await page.setViewportSize(this.defaultViewport)
      return page
    } catch (error) {
      await page.close()
      throw error
    }
  }

  /**
   * Internal lifecycle operation.
   * @internal
   */
  async closeContext(id: string): Promise<void> {
    if (id === this.initialContextId)
      throw new Error('The default browser context cannot be closed; close the browser instead')
    this.assertConnected()
    await this.driver.closeContext(id)
    this.contextObjects.delete(id)
    for (const [pageId, page] of this.pageObjects)
      if (page.contextId === id) {
        page.retire()
        this.pageObjects.delete(pageId)
      }
  }

  close(): Promise<void> {
    if (this.closing) return this.closing
    this.stopping = true
    return (this.closing = (async () => {
      const errors: Error[] = []
      try {
        await this.driver.disposeResources?.()
      } catch (error) {
        errors.push(toError(error as UntrustedInput))
      }

      if (this.shutdownBrowser && this.driver.wire.isConnected()) {
        try {
          await this.driver.closeBrowser()
        } catch (error) {
          // CDP Browser.close may disconnect before its acknowledgement arrives.
          if (this.driver.wire.isConnected()) errors.push(toError(error as UntrustedInput))
        }
      }
      if (!this.shutdownBrowser && this.driver.wire.isConnected()) {
        try {
          await this.driver.disposeSession?.()
        } catch (error) {
          errors.push(toError(error as UntrustedInput))
        }
      }
      try {
        await this.driver.wire.close()
      } catch (error) {
        errors.push(toError(error as UntrustedInput))
      }
      if (this.ownedProcess && this.shutdownBrowser && errors.length === 0) {
        let timer: ReturnType<typeof setTimeout> | undefined
        try {
          // A protocol close acknowledgement can precede actual browser exit.
          // Observe exit immediately; terminate only if its grace period expires.
          await Promise.race([
            this.ownedProcess.closed,
            new Promise<void>((resolve) => {
              timer = setTimeout(resolve, 1000)
            }),
          ])
        } catch (error) {
          errors.push(toError(error as UntrustedInput))
        } finally {
          clearTimeout(timer)
        }
      }
      try {
        await this.ownedProcess?.close()
      } catch (error) {
        errors.push(toError(error as UntrustedInput))
      }
      if (errors.length) throw new AggregateError(errors, 'Browser cleanup failed')
    })())
  }
}

export class ProtocolBrowserContext {
  private closed = false
  private closing: Promise<void> | undefined
  constructor(
    private readonly browser: ProtocolBrowser,
    private readonly driver: BrowserProtocolDriver,
    readonly id: string,
  ) {}
  /**
   * Retires an externally closed context wrapper.
   * @internal
   */
  retire(): void {
    this.closed = true
  }
  private assertOpen(): void {
    if (this.closed || !this.browser.isConnected()) throw new Error('Browser context is closed')
  }
  async newPage(): Promise<ProtocolPage> {
    this.assertOpen()
    return this.browser.createPage(this.id)
  }
  async pages(): Promise<ProtocolPage[]> {
    this.assertOpen()
    return (await this.browser.pages()).filter((page) => page.contextId === this.id)
  }
  cookies(): Promise<BrowserCookie[]> {
    this.assertOpen()
    return this.driver.cookies(this.id)
  }
  addCookies(cookies: BrowserCookie[]): Promise<void> {
    this.assertOpen()
    if (!Array.isArray(cookies) || cookies.length > 2048)
      throw new Error('Invalid browser cookie array')
    for (const cookie of cookies) {
      if (
        typeof cookie.name !== 'string' ||
        typeof cookie.value !== 'string' ||
        typeof cookie.domain !== 'string' ||
        cookie.name.length === 0 ||
        cookie.domain.length === 0 ||
        (cookie.expires !== undefined && !Number.isFinite(cookie.expires))
      )
        throw new Error('Invalid browser cookie')
    }
    return this.driver.setCookies(this.id, cookies)
  }
  close(): Promise<void> {
    if (this.closing) return this.closing
    const closing = this.browser.closeContext(this.id).then(
      () => {
        this.closed = true
      },
      (error: UntrustedInput) => {
        this.closing = undefined
        throw error
      },
    )
    this.closing = closing
    return closing
  }
}

interface PageEvents {
  download: ProtocolDownload
  dialog: ProtocolDialog
  popup: ProtocolPage
}
type PageEvent = keyof PageEvents
interface DOMOwner {
  page(): ProtocolPage
  coordinateFrame(): Promise<string | undefined>
  perform<T>(action: () => Promise<T>): Promise<T>
  query(
    query: BrowserDOMQuery,
    operation: BrowserDOMOperation,
    value?: BrowserDOMValue,
    wait?: boolean,
  ): Promise<UntrustedInput>
  clickQuery(query: BrowserDOMQuery): Promise<void>
  fillQuery(query: BrowserDOMQuery, text: string): Promise<void>
  pressQuery(query: BrowserDOMQuery, key: string): Promise<void>
  uploadQuery(query: BrowserDOMQuery, files: string[]): Promise<void>
  hoverQuery(query: BrowserDOMQuery): Promise<void>
}

export class ProtocolPage implements DOMOwner {
  readonly id: string
  readonly contextId: string
  private closed = false
  private closing: Promise<void> | undefined
  private timeoutMs = 30_000
  private actionTail: Promise<void> = Promise.resolve()
  private queuedActions = 0
  private pointer = { x: 0, y: 0 }
  private viewport: BrowserViewport | null = null
  private preparing: Promise<void> | undefined
  private downloadReady: Promise<void> = Promise.resolve()
  private downloadSubscription: Promise<void> | undefined
  private readonly detach: (() => void)[] = []
  private readonly listeners = new Map<PageEvent, Set<(value: never) => void>>()
  private readonly waiters = new Set<(error: Error) => void>()
  private readonly frameObjects = new Map<string, ProtocolFrame>()

  /**
   * Initializes protocol event domains before actions.
   * @internal
   */
  prepare(): Promise<void> {
    if (this.preparing) return this.preparing
    let detachDialog: (() => void) | undefined
    this.preparing = (async () => {
      if (this.driver.subscribeDialogs)
        detachDialog = this.driver.subscribeDialogs(this.id, (dialog) => {
          if (!this.listeners.get('dialog')?.size) void dialog.dismiss().catch(() => undefined)
          else this.emitEvent('dialog', dialog)
        })
      await this.driver.preparePage?.(this.id)
      if (detachDialog) {
        if (this.closed) detachDialog()
        else this.detach.push(detachDialog)
      }
    })().catch((error: UntrustedInput) => {
      detachDialog?.()
      this.preparing = undefined
      throw error
    })
    return this.preparing
  }
  page(): ProtocolPage {
    return this
  }
  coordinateFrame(): Promise<undefined> {
    return Promise.resolve(undefined)
  }
  on<K extends PageEvent>(event: K, listener: (value: PageEvents[K]) => void): this {
    this.assertOpen()
    let group = this.listeners.get(event)
    if (!group) this.listeners.set(event, (group = new Set()))
    group.add(listener)
    if (event === 'download' && !this.downloadSubscription) {
      this.downloadReady = this.downloadSubscription = (async () => {
        if (!this.driver.subscribeDownloads)
          throw new Error('Downloads are unavailable for this backend')
        const detach = await this.driver.subscribeDownloads(this.id, (download) =>
          this.emitEvent('download', download),
        )
        if (this.closed) detach()
        else this.detach.push(detach)
      })()
      const subscription = this.downloadSubscription
      void subscription.catch(() => {
        if (this.downloadSubscription === subscription) {
          this.downloadSubscription = undefined
          this.downloadReady = Promise.resolve()
        }
      })
    }
    return this
  }
  once<K extends PageEvent>(event: K, listener: (value: PageEvents[K]) => void): this {
    const receive = (value: PageEvents[K]) => {
      this.off(event, receive)
      return listener(value)
    }
    return this.on(event, receive)
  }
  off<K extends PageEvent>(event: K, listener: (value: PageEvents[K]) => void): this {
    this.listeners.get(event)?.delete(listener)
    return this
  }
  /**
   * Dispatches browser events without breaking the protocol reader.
   * @internal
   */
  emitEvent<K extends PageEvent>(event: K, value: PageEvents[K]): void {
    for (const listener of [...(this.listeners.get(event) ?? [])]) {
      const failed = () => {
        if (event === 'dialog') void (value as ProtocolDialog).dismiss().catch(() => undefined)
      }
      try {
        const result = listener(value as never) as UntrustedInput
        if (result && typeof result === 'object' && 'then' in result)
          void Promise.resolve(result).catch(failed)
      } catch {
        failed()
      }
    }
  }
  waitForEvent<K extends PageEvent>(
    event: K,
    options: { timeout?: number } = {},
  ): Promise<PageEvents[K]> {
    const timeout = options.timeout ?? this.timeoutMs
    if (!Number.isFinite(timeout) || timeout < 0 || timeout > 2147483647)
      return Promise.reject(new Error('Invalid browser event timeout'))
    return new Promise((resolve, reject) => {
      let timer: ReturnType<typeof setTimeout> | undefined
      const cleanup = () => {
        clearTimeout(timer)
        this.off(event, receive)
        this.waiters.delete(fail)
      }
      const fail = (error: Error) => {
        cleanup()
        reject(error)
      }
      const receive = (value: PageEvents[K]) => {
        cleanup()
        resolve(value)
      }
      try {
        this.on(event, receive)
      } catch (error) {
        fail(toError(error as UntrustedInput))
        return
      }
      this.waiters.add(fail)
      if (timeout)
        timer = setTimeout(() => fail(new Error(`Browser ${event} event timed out`)), timeout)
      if (event === 'download')
        void this.downloadReady.catch((error: UntrustedInput) => fail(toError(error)))
    })
  }
  bringToFront(): Promise<void> {
    return this.perform(async () => {
      await this.driver.activatePage?.(this.id)
    })
  }
  async frames(): Promise<ProtocolFrame[]> {
    await this.prepare()
    if (!this.driver.frames) throw new Error('Frames are unavailable for this backend')
    const frames = await this.driver.frames(this.id)
    const live = new Set(frames.map((frame) => frame.id))
    for (const id of this.frameObjects.keys()) if (!live.has(id)) this.frameObjects.delete(id)
    return frames.map((info) => {
      let frame = this.frameObjects.get(info.id)
      if (!frame)
        this.frameObjects.set(info.id, (frame = new ProtocolFrame(this, this.driver, info.id)))
      return frame
    })
  }
  frameLocator(selector: string): ProtocolFrameLocator {
    return new ProtocolFrameLocator(this, this.driver, [selector])
  }

  readonly keyboard = {
    insertText: (text: string): Promise<void> =>
      this.perform(() => this.driver.insertText(this.id, text)),
    type: (text: string): Promise<void> =>
      this.perform(() => this.driver.insertText(this.id, text)),
    press: (key: string): Promise<void> => this.perform(() => this.driver.press(this.id, key)),
  }
  readonly mouse = {
    click: (
      x: number,
      y: number,
      options: { button?: 'left' | 'right' | 'middle'; clickCount?: number } = {},
    ): Promise<void> =>
      this.perform(async () => {
        if (!Number.isFinite(x) || !Number.isFinite(y))
          throw new Error('Invalid browser pointer coordinates')
        const count = options.clickCount ?? 1
        if (!Number.isInteger(count) || count < 1 || count > 3)
          throw new Error('Browser clickCount must be between 1 and 3')
        await this.driver.click(this.id, x, y, options.button ?? 'left', count)
        this.pointer = { x, y }
      }),
    wheel: (deltaX: number, deltaY: number): Promise<void> =>
      this.perform(() => {
        if (!Number.isFinite(deltaX) || !Number.isFinite(deltaY))
          throw new Error('Invalid browser wheel delta')
        return this.driver.scroll(this.id, this.pointer.x, this.pointer.y, deltaX, deltaY)
      }),
  }

  constructor(
    private readonly driver: BrowserProtocolDriver,
    info: DriverPage,
  ) {
    this.id = info.id
    this.contextId = info.contextId
  }
  isClosed(): boolean {
    return this.closed || !this.driver.wire.isConnected()
  }
  /**
   * Internal lifecycle operation.
   * @internal
   */
  retire(): void {
    if (this.closed) return
    this.closed = true
    for (const detach of this.detach) detach()
    this.detach.length = 0
    for (const fail of [...this.waiters]) fail(new Error('Browser page is closed'))
    this.listeners.clear()
    this.frameObjects.clear()
  }
  private assertOpen(): void {
    if (this.isClosed()) throw new Error('Browser page is closed')
  }
  /**
   * Internal lifecycle operation.
   * @internal
   */
  perform<T>(action: () => Promise<T>): Promise<T> {
    if (this.queuedActions >= 256)
      return Promise.reject(new Error('Browser page action queue limit reached'))
    this.queuedActions++
    const work = this.actionTail.then(async () => {
      this.assertOpen()
      await this.prepare()
      await this.downloadReady
      return action()
    })
    this.actionTail = work.then(
      () => {
        this.queuedActions--
      },
      () => {
        this.queuedActions--
      },
    )
    return work
  }
  /** Sets locator retry time; protocol commands and navigation use the connection timeout. */
  setDefaultTimeout(timeout: number): void {
    if (!Number.isFinite(timeout) || timeout < 0 || timeout > 2_147_483_647)
      throw new Error('Invalid browser timeout')
    this.timeoutMs = timeout
  }

  goto(url: string, options: BrowserNavigationOptions = {}): Promise<void> {
    return this.perform(() => {
      const parsed = new URL(url)
      if (!['http:', 'https:', 'about:', 'data:', 'file:'].includes(parsed.protocol))
        throw new Error('Unsupported browser navigation scheme')
      const wait =
        options.waitUntil === 'commit'
          ? 'none'
          : options.waitUntil === 'domcontentloaded'
            ? 'interactive'
            : 'complete'
      return this.driver.navigate(this.id, parsed.href, wait)
    })
  }
  async reload(): Promise<void> {
    await this.goto(await this.url())
  }
  async url(): Promise<string> {
    this.assertOpen()
    const info = this.driver.pageInfo?.(this.id)
    return info ? Promise.resolve(info.url) : this.evaluate(() => location.href)
  }
  async title(): Promise<string> {
    this.assertOpen()
    return this.driver.pageTitle
      ? this.driver.pageTitle(this.id)
      : this.evaluate(() => document.title)
  }
  evaluate<R>(fn: string | (() => R | Promise<R>)): Promise<R>
  evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg: A): Promise<R>
  async evaluate<R, A>(fn: string | ((arg: A) => R | Promise<R>), arg?: A): Promise<R> {
    this.assertOpen()
    await this.prepare()
    const expression =
      typeof fn === 'string' ? fn : `(${fn.toString()})(${argumentSource(arg as UntrustedInput)})`
    return (await this.driver.evaluate(this.id, expression)) as R
  }
  setContent(html: string): Promise<void> {
    return this.perform(async () => {
      await this.evaluate((content) => {
        document.open()
        document.write(content)
        document.close()
      }, html)
    })
  }
  setViewportSize(size: BrowserViewport): Promise<void> {
    viewportSize(size)
    return this.perform(async () => {
      await this.driver.setViewport(this.id, size)
      this.viewport = { ...size }
    })
  }
  viewportSize(): BrowserViewport | null {
    return this.viewport ? { ...this.viewport } : null
  }
  async screenshot(options: BrowserScreenshotOptions & { path?: string } = {}): Promise<Buffer> {
    this.assertOpen()
    if (options.type !== undefined && options.type !== 'png' && options.type !== 'jpeg')
      throw new Error('Unsupported browser screenshot type')
    if (
      options.quality !== undefined &&
      (!Number.isInteger(options.quality) ||
        options.quality < 0 ||
        options.quality > 100 ||
        options.type !== 'jpeg')
    )
      throw new Error('Browser screenshot quality requires JPEG and an integer from 0 to 100')
    const bytes = await this.driver.screenshot(this.id, options)
    if (options.path !== undefined) await writeFile(options.path, bytes)
    return bytes
  }
  locator(selector: string): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'css', value: selector } })
  }
  getByRole(role: string, options: BrowserRoleOptions = {}): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'role', value: role, ...options } })
  }
  getByText(text: string, options: BrowserTextOptions = {}): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'text', value: text, ...options } })
  }
  getByLabel(text: string, options: BrowserTextOptions = {}): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'label', value: text, ...options } })
  }
  getByPlaceholder(value: string, options: BrowserTextOptions = {}): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'placeholder', value, ...options } })
  }
  getByTestId(value: string): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'testId', value } })
  }
  click(selector: string): Promise<void> {
    return this.locator(selector).click()
  }
  fill(selector: string, value: string): Promise<void> {
    return this.locator(selector).fill(value)
  }
  press(selector: string, key: string): Promise<void> {
    return this.locator(selector).press(key)
  }

  /**
   * Internal lifecycle operation.
   * @internal
   */
  async query(
    query: BrowserDOMQuery,
    operation: BrowserDOMOperation,
    value?: BrowserDOMValue,
    wait = true,
    frameId?: string,
  ): Promise<UntrustedInput> {
    this.assertOpen()
    await this.prepare()
    const deadline = this.timeoutMs === 0 ? Infinity : performance.now() + this.timeoutMs
    for (;;) {
      const result = await this.driver.evaluate(
        this.id,
        `(${domSource})(${JSON.stringify(query)},${JSON.stringify(operation)},${argumentSource(value)})`,
        frameId,
      )
      if (result === null || typeof result !== 'object' || !('retry' in result)) return result
      const reason = protocolString(protocolRecord(result, 'DOM result').retry, 'DOM retry reason')
      if (!wait || performance.now() >= deadline)
        throw new Error(`Browser locator ${operation} timed out: ${reason}`)
      this.assertOpen()
      await delay(Math.min(25, Math.max(1, deadline - performance.now())))
    }
  }
  /**
   * Internal lifecycle operation.
   * @internal
   */
  async clickQuery(query: BrowserDOMQuery, frameId?: string): Promise<void> {
    if (this.driver.clickLocator) {
      this.assertOpen()
      await this.prepare()
      const deadline = this.timeoutMs === 0 ? Infinity : performance.now() + this.timeoutMs
      let reason = 'Input preparation exceeded its deadline'
      const timeoutError = () => new Error(`Browser locator prepareClick timed out: ${reason}`)
      const assertReady = (): void => {
        this.assertOpen()
        if (performance.now() >= deadline) throw timeoutError()
      }
      const prepare = async (): Promise<DriverClickPoint> => {
        assertReady()
        const point = protocolRecord(
          await this.driver.evaluate(
            this.id,
            `(${domSource})(${JSON.stringify(query)},"prepareClick")`,
            frameId,
          ),
          'click point',
        )
        assertReady()
        if (typeof point.retry === 'string') return { retry: point.retry }
        if (
          typeof point.x !== 'number' ||
          typeof point.y !== 'number' ||
          !Number.isFinite(point.x) ||
          !Number.isFinite(point.y)
        )
          throw new Error('Invalid browser click point')
        return { x: point.x, y: point.y }
      }
      for (;;) {
        assertReady()
        // Waiting for an absent/disabled locator must not own browser-wide input focus.
        let result = await prepare()
        if (!('retry' in result))
          result = await this.driver.clickLocator(this.id, prepare, frameId, assertReady)
        if (!('retry' in result)) {
          this.pointer = result
          return
        }
        reason = result.retry
        assertReady()
        await delay(Math.min(25, Math.max(1, deadline - performance.now())))
      }
    }
    const point = protocolRecord(
      await this.query(query, 'prepareClick', undefined, true, frameId),
      'click point',
    )
    if (typeof point.x !== 'number' || typeof point.y !== 'number')
      throw new Error('Invalid browser click point')
    await this.driver.click(this.id, point.x, point.y, 'left', 1, frameId)
    this.pointer = { x: point.x, y: point.y }
  }
  /**
   * Internal lifecycle operation.
   * @internal
   */
  async fillQuery(query: BrowserDOMQuery, text: string, frameId?: string): Promise<void> {
    const prepared = await this.query(query, 'prepareFill', text, true, frameId)
    if (prepared !== null && typeof prepared === 'object' && 'filled' in prepared) return
    if (prepared !== null && typeof prepared === 'object' && 'selectAll' in prepared)
      await this.driver.press(this.id, 'ControlOrMeta+A')
    if (text.length === 0) await this.driver.press(this.id, 'Backspace')
    else await this.driver.insertText(this.id, text)
  }
  /**
   * Internal lifecycle operation.
   * @internal
   */
  async pressQuery(query: BrowserDOMQuery, key: string, frameId?: string): Promise<void> {
    await this.query(query, 'focus', undefined, true, frameId)
    await this.driver.press(this.id, key)
  }
  /**
   * Resolves file input in its document without serializing the element.
   * @internal
   */
  async uploadQuery(query: BrowserDOMQuery, files: string[], frameId?: string): Promise<void> {
    if (!this.driver.setInputFiles) throw new Error('File uploads are unavailable for this backend')
    await this.driver.setInputFiles(
      this.id,
      `(${domSource})(${JSON.stringify(query)},"prepareUpload")`,
      files,
      frameId,
    )
  }
  /**
   * Uses native pointer input after element actionability checks.
   * @internal
   */
  async hoverQuery(query: BrowserDOMQuery, frameId?: string): Promise<void> {
    if (!this.driver.moveMouse) throw new Error('Pointer movement is unavailable for this backend')
    const point = protocolRecord(
      await this.query(query, 'prepareHover', undefined, true, frameId),
      'hover point',
    )
    if (typeof point.x !== 'number' || typeof point.y !== 'number')
      throw new Error('Invalid browser hover point')
    await this.driver.moveMouse(this.id, point.x, point.y, frameId)
  }
  /**
   * Dispatches a drag in the shared page input stream.
   * @internal
   */
  async dragPoints(
    from: { x: number; y: number },
    to: { x: number; y: number },
    steps: number,
    frameId?: string,
  ): Promise<void> {
    if (!this.driver.dragMouse) throw new Error('Dragging is unavailable for this backend')
    await this.driver.dragMouse(this.id, from, to, steps, frameId)
  }
  /**
   * Evaluates in a frame using its current execution context.
   * @internal
   */
  async evaluateFrame<R, A>(
    frameId: string,
    fn: string | ((arg: A) => R | Promise<R>),
    arg?: A,
  ): Promise<R> {
    this.assertOpen()
    await this.prepare()
    return (await this.driver.evaluate(
      this.id,
      typeof fn === 'string' ? fn : `(${fn.toString()})(${argumentSource(arg as UntrustedInput)})`,
      frameId,
    )) as R
  }
  close(): Promise<void> {
    if (this.closing) return this.closing
    if (this.isClosed()) return Promise.resolve()
    this.closing = this.driver.closePage(this.id).then(
      () => {
        this.retire()
      },
      (error: UntrustedInput) => {
        this.closing = undefined
        throw error
      },
    )
    return this.closing
  }
}

export class ProtocolLocator {
  constructor(
    private readonly owner: DOMOwner,
    private readonly query: BrowserDOMQuery,
  ) {}
  page(): ProtocolPage {
    return this.owner.page()
  }
  nth(index: number): ProtocolLocator {
    if (!Number.isInteger(index) || index < 0)
      throw new Error('Browser locator index must be a non-negative integer')
    return new ProtocolLocator(this.owner, { ...this.query, index })
  }
  first(): ProtocolLocator {
    return this.nth(0)
  }
  last(): ProtocolLocator {
    return new ProtocolLocator(this.owner, { ...this.query, index: -1 })
  }
  async count(): Promise<number> {
    return (await this.owner.query(this.query, 'count', undefined, false)) as number
  }
  async textContent(): Promise<string | null> {
    return (await this.owner.query(this.query, 'textContent')) as string | null
  }
  async innerText(): Promise<string> {
    return (await this.owner.query(this.query, 'innerText')) as string
  }
  async getAttribute(name: string): Promise<string | null> {
    return (await this.owner.query(this.query, 'getAttribute', name)) as string | null
  }
  async isVisible(): Promise<boolean> {
    return (await this.owner.query(this.query, 'visible', undefined, false)) as boolean
  }
  async isEnabled(): Promise<boolean> {
    return (await this.owner.query(this.query, 'enabled', undefined, false)) as boolean
  }
  async isChecked(): Promise<boolean> {
    return (await this.owner.query(this.query, 'checked')) as boolean
  }
  async boundingBox(): Promise<{ x: number; y: number; width: number; height: number } | null> {
    return (await this.owner.query(this.query, 'box')) as {
      x: number
      y: number
      width: number
      height: number
    } | null
  }
  click(): Promise<void> {
    return this.owner.perform(() => this.owner.clickQuery(this.query))
  }
  fill(text: string): Promise<void> {
    return this.owner.perform(() => this.owner.fillQuery(this.query, text))
  }
  press(key: string): Promise<void> {
    return this.owner.perform(() => this.owner.pressQuery(this.query, key))
  }
  selectOption(value: BrowserSelectOption | BrowserSelectOption[]): Promise<string[]> {
    return this.owner.perform(async () => {
      return (await this.owner.query(this.query, 'selectOption', value)) as string[]
    })
  }
  setInputFiles(files: string | string[]): Promise<void> {
    const paths = (Array.isArray(files) ? files : [files]).map((file) => resolve(file))
    return this.owner.perform(() => this.owner.uploadQuery(this.query, paths))
  }
  hover(): Promise<void> {
    return this.owner.perform(() => this.owner.hoverQuery(this.query))
  }
  scrollIntoViewIfNeeded(): Promise<void> {
    return this.owner.perform(async () => {
      await this.owner.query(this.query, 'scroll')
    })
  }
  dragTo(target: ProtocolLocator, options: { steps?: number } = {}): Promise<void> {
    const steps = options.steps ?? 10
    if (!Number.isInteger(steps) || steps < 1 || steps > 1000)
      return Promise.reject(new Error('Drag steps must be between 1 and 1000'))
    if (this.page() !== target.page())
      return Promise.reject(new Error('Drag target belongs to another page'))
    return this.owner.perform(async () => {
      const frameId = await this.owner.coordinateFrame()
      if (frameId !== (await target.owner.coordinateFrame()))
        throw new Error('Drag target belongs to another frame')
      const from = protocolRecord(await this.owner.query(this.query, 'prepareHover'), 'drag source')
      const to = protocolRecord(
        await target.owner.query(target.query, 'prepareHover'),
        'drag destination',
      )
      if (
        typeof from.x !== 'number' ||
        typeof from.y !== 'number' ||
        typeof to.x !== 'number' ||
        typeof to.y !== 'number'
      )
        throw new Error('Invalid drag coordinates')
      await this.page().dragPoints({ x: from.x, y: from.y }, { x: to.x, y: to.y }, steps, frameId)
    })
  }
  setChecked(checked: boolean): Promise<void> {
    return this.owner.perform(async () => {
      if ((await this.isChecked()) !== checked) await this.owner.clickQuery(this.query)
      if ((await this.isChecked()) !== checked)
        throw new Error('Browser checkbox did not reach requested state')
    })
  }
  check(): Promise<void> {
    return this.setChecked(true)
  }
  uncheck(): Promise<void> {
    return this.setChecked(false)
  }
}

/** A document scope shares its page action queue and protocol connection. */
abstract class ProtocolDOMScope implements DOMOwner {
  constructor(
    protected readonly parentPage: ProtocolPage,
    protected readonly driver: BrowserProtocolDriver,
  ) {}
  page(): ProtocolPage {
    return this.parentPage
  }
  abstract coordinateFrame(): Promise<string>
  perform<T>(action: () => Promise<T>): Promise<T> {
    return this.parentPage.perform(action)
  }
  async query(
    query: BrowserDOMQuery,
    operation: BrowserDOMOperation,
    value?: BrowserDOMValue,
    wait = true,
  ): Promise<UntrustedInput> {
    return this.parentPage.query(query, operation, value, wait, await this.coordinateFrame())
  }
  async clickQuery(query: BrowserDOMQuery): Promise<void> {
    return this.parentPage.clickQuery(query, await this.coordinateFrame())
  }
  async fillQuery(query: BrowserDOMQuery, text: string): Promise<void> {
    return this.parentPage.fillQuery(query, text, await this.coordinateFrame())
  }
  async pressQuery(query: BrowserDOMQuery, key: string): Promise<void> {
    return this.parentPage.pressQuery(query, key, await this.coordinateFrame())
  }
  async uploadQuery(query: BrowserDOMQuery, files: string[]): Promise<void> {
    return this.parentPage.uploadQuery(query, files, await this.coordinateFrame())
  }
  async hoverQuery(query: BrowserDOMQuery): Promise<void> {
    return this.parentPage.hoverQuery(query, await this.coordinateFrame())
  }
  locator(value: string): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'css', value } })
  }
  getByRole(value: string, options: BrowserRoleOptions = {}): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'role', value, ...options } })
  }
  getByText(value: string, options: BrowserTextOptions = {}): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'text', value, ...options } })
  }
  getByLabel(value: string, options: BrowserTextOptions = {}): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'label', value, ...options } })
  }
  getByPlaceholder(value: string, options: BrowserTextOptions = {}): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'placeholder', value, ...options } })
  }
  getByTestId(value: string): ProtocolLocator {
    return new ProtocolLocator(this, { selector: { kind: 'testId', value } })
  }
  evaluate<R>(fn: string | (() => R | Promise<R>)): Promise<R>
  evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg: A): Promise<R>
  async evaluate<R, A>(fn: string | ((arg: A) => R | Promise<R>), arg?: A): Promise<R> {
    return this.parentPage.evaluateFrame(await this.coordinateFrame(), fn, arg)
  }
}

/** A live frame wrapper. Navigation retains identity; detachment invalidates it. */
export class ProtocolFrame extends ProtocolDOMScope {
  constructor(
    page: ProtocolPage,
    driver: BrowserProtocolDriver,
    readonly id: string,
  ) {
    super(page, driver)
  }
  coordinateFrame(): Promise<string> {
    if (this.isDetached()) throw new Error('Browser frame is detached')
    return Promise.resolve(this.id)
  }
  isDetached(): boolean {
    return this.parentPage.isClosed() || !this.driver.frameInfo?.(this.parentPage.id, this.id)
  }
  url(): Promise<string> {
    return Promise.resolve(this.driver.frameInfo?.(this.parentPage.id, this.id)?.url ?? '')
  }
  name(): string {
    return this.driver.frameInfo?.(this.parentPage.id, this.id)?.name ?? ''
  }
  frameLocator(selector: string): ProtocolFrameLocator {
    return new ProtocolFrameLocator(this.parentPage, this.driver, [selector], this.id)
  }
}

/** Resolves iframe elements afresh so replacements do not retain stale execution contexts. */
export class ProtocolFrameLocator extends ProtocolDOMScope {
  constructor(
    page: ProtocolPage,
    driver: BrowserProtocolDriver,
    private readonly selectors: string[],
    private readonly parentId?: string,
  ) {
    super(page, driver)
  }
  async coordinateFrame(): Promise<string> {
    await this.parentPage.prepare()
    if (!this.driver.resolveFrame) throw new Error('Frames are unavailable for this backend')
    let frameId = this.parentId
    for (const selector of this.selectors)
      frameId = await this.driver.resolveFrame(
        this.parentPage.id,
        `(${domSource})(${JSON.stringify({ selector: { kind: 'css', value: selector } })},"element")`,
        frameId,
      )
    if (!frameId) throw new Error('Frame locator is empty')
    return frameId
  }
  frameLocator(selector: string): ProtocolFrameLocator {
    return new ProtocolFrameLocator(
      this.parentPage,
      this.driver,
      [...this.selectors, selector],
      this.parentId,
    )
  }
}

async function endpointURL(options: ConnectBrowserOptions): Promise<string> {
  const url = new URL(options.endpointURL)
  if (url.protocol === 'ws:' || url.protocol === 'wss:') return url.href
  if (options.backend !== 'cdp' || !['http:', 'https:'].includes(url.protocol))
    throw new Error('BiDi requires a WebSocket URL; CDP accepts a WebSocket URL or HTTP origin')
  url.pathname = `${url.pathname.replace(/\/$/u, '')}/json/version`
  const signal = AbortSignal.any([
    AbortSignal.timeout(options.timeoutMs ?? 30_000),
    ...(options.signal ? [options.signal] : []),
  ])
  const response = await fetch(url, { signal })
  if (!response.ok || !response.body) {
    await response.body?.cancel()
    throw new Error(`CDP endpoint discovery failed: HTTP ${response.status}`)
  }
  const reader = response.body.getReader()
  const buffers: Uint8Array[] = []
  let total = 0
  try {
    for (;;) {
      const next = await reader.read()
      if (next.done) break
      total += next.value.byteLength
      if (total > 64 * 1024) throw new Error('CDP endpoint discovery byte budget exceeded')
      buffers.push(next.value)
    }
  } finally {
    await reader.cancel()
    reader.releaseLock()
  }
  const data = protocolRecord(
    JSON.parse(Buffer.concat(buffers).toString('utf8')) as UntrustedInput,
    'CDP endpoint discovery',
  )
  return protocolString(data.webSocketDebuggerUrl, 'browser WebSocket endpoint')
}

/** Attaches to an existing browser. Closing disconnects unless explicitly configured otherwise. */
export async function connectBrowser(options: ConnectBrowserOptions): Promise<ProtocolBrowser> {
  if (options.backend !== 'cdp' && options.backend !== 'bidi')
    throw new Error('Browser backend must be cdp or bidi')
  const wire = await BrowserWire.connect(await endpointURL(options), options)
  let driver: BrowserProtocolDriver | undefined
  try {
    driver =
      options.backend === 'cdp'
        ? (await import('./browser-cdp.js')).createCDPDriver(wire)
        : (await import('./browser-bidi.js')).createBiDiDriver(wire)
    await driver.initialize()
    return new ProtocolBrowser(driver, options.closeBrowserOnClose)
  } catch (error) {
    const failures: UntrustedInput[] = [error as UntrustedInput]
    try {
      if (wire.isConnected()) await driver?.disposeSession?.()
    } catch (cleanupError) {
      failures.push(cleanupError as UntrustedInput)
    }
    try {
      await wire.close()
    } catch (cleanupError) {
      failures.push(cleanupError as UntrustedInput)
    }
    try {
      await driver?.disposeResources?.()
    } catch (cleanupError) {
      failures.push(cleanupError as UntrustedInput)
    }
    if (failures.length > 1)
      throw new AggregateError(failures, 'Browser connection initialization and cleanup failed', {
        cause: error,
      })
    throw error
  }
}

/** Owns a browser process and its isolated temporary profile until physical exit. */
export async function launchProtocolBrowser(
  options: LaunchProtocolBrowserOptions,
): Promise<ProtocolBrowser> {
  if (options.viewport) viewportSize(options.viewport)
  const process = await launchProtocolProcess(options)
  let wire: BrowserWire | undefined
  let driver: BrowserProtocolDriver | undefined
  try {
    wire = await BrowserWire.connect(process.endpointURL, options)
    driver =
      options.backend === 'cdp'
        ? (await import('./browser-cdp.js')).createCDPDriver(wire)
        : (await import('./browser-bidi.js')).createBiDiDriver(wire)
    await driver.initialize()
    const browser = new ProtocolBrowser(
      driver,
      true,
      process,
      options.viewport ? { ...options.viewport } : undefined,
    )
    return browser
  } catch (error) {
    const failures: UntrustedInput[] = [error as UntrustedInput]
    try {
      if (wire?.isConnected()) await driver?.disposeSession?.()
    } catch (cleanupError) {
      failures.push(cleanupError as UntrustedInput)
    }
    try {
      await wire?.close()
    } catch (cleanupError) {
      failures.push(cleanupError as UntrustedInput)
    }
    try {
      await process.close()
    } catch (cleanupError) {
      failures.push(cleanupError as UntrustedInput)
    }
    try {
      await driver?.disposeResources?.()
    } catch (cleanupError) {
      failures.push(cleanupError as UntrustedInput)
    }
    if (failures.length > 1)
      throw new AggregateError(failures, 'Browser launch initialization and cleanup failed', {
        cause: error,
      })
    throw error
  }
}
