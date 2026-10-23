// SPDX-License-Identifier: MIT
/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase module edges and avoid lifecycle initialization cycles. */
import { resolveNativeURLGlob } from './native-url-glob.js'

import type { NativeBrowser } from './native-browser.js'
import type { NativeBrowserContext } from './native-context.js'
import type { NativePage } from './native-page.js'
import type { NativeVideo } from './native-video.js'

type PageListener = (page: NativePage) => void
export interface NativeContextPageBinding {
  id: number
  context: NativeBrowserContext
  openerId?: number
  closed: boolean
  announced?: boolean
  closePromise?: Promise<void>
  videoId?: string
  video?: NativeVideo
  listeners: Map<'popup' | 'close', PageListener[]>
}
export const nativeContextPageBindings = new WeakMap<NativePage, NativeContextPageBinding>()

export function nativeContextPageBinding(page: NativePage): NativeContextPageBinding {
  const result = nativeContextPageBindings.get(page)
  if (!result) throw new Error('This page was created outside a native browser context')
  return result
}
export function getNativePageContext(page: NativePage): NativeBrowserContext {
  return nativeContextPageBinding(page).context
}
export function isNativeContextPageClosed(page: NativePage): boolean {
  const entry = nativeContextPageBindings.get(page)
  return entry?.closed ?? (page.source.pageSignal ?? page.source.signal).aborted
}
export function nativePageOpener(page: NativePage): Promise<NativePage | null> {
  const entry = nativeContextPageBinding(page)
  return Promise.resolve(
    entry.openerId === undefined ? null : entry.context.pageById(entry.openerId),
  )
}
export function closeNativeContextPage(
  page: NativePage,
  options: { reason?: string; runBeforeUnload?: boolean } = {},
): Promise<void> {
  const entry = nativeContextPageBinding(page)
  if (entry.closePromise) return entry.closePromise
  if (entry.closed) return Promise.resolve()
  // This only acknowledges actual CEF dispatch. A beforeunload dismissal keeps
  // the page live, and must not cache a completed irreversible-close promise.
  if (options.runBeforeUnload) return entry.context.closePage(entry.id, options)
  entry.closePromise = entry.context.closePage(entry.id, options)
  return entry.closePromise
}
export function onNativeContextPageEvent(
  page: NativePage,
  event: 'popup' | 'close',
  listener: PageListener,
  prepend = false,
): void {
  const entry = nativeContextPageBinding(page)
  let listeners = entry.listeners.get(event)
  if (!listeners) {
    listeners = []
    entry.listeners.set(event, listeners)
  }
  if (prepend) listeners.unshift(listener)
  else listeners.push(listener)
}
export function offNativeContextPageEvent(
  page: NativePage,
  event: 'popup' | 'close',
  listener: PageListener,
): void {
  const listeners = nativeContextPageBinding(page).listeners.get(event)
  const index = listeners?.lastIndexOf(listener) ?? -1
  if (index >= 0) listeners?.splice(index, 1)
}
export function emitNativeContextPage(
  page: NativePage,
  event: 'popup' | 'close',
  value: NativePage,
): void {
  dispatchNativeContextPageListeners(
    nativeContextPageBinding(page).listeners.get(event) ?? [],
    value,
  )
}

/** Finish actual lifecycle delivery before exposing original user exceptions. */
export function dispatchNativeContextPageListeners(
  listeners: readonly PageListener[],
  value: NativePage,
): void {
  for (const listener of [...listeners]) {
    try {
      listener(value)
    } catch (error) {
      // Do not turn an author listener failure into a protocol/decoder failure,
      // or leave later actual event observers and close cleanup undelivered.
      queueMicrotask(() => {
        throw error
      })
    }
  }
}

type NativeContextPageFactory = (
  source: NativeBrowser,
  signal?: AbortSignal,
) => NativePage | undefined
let contextPageFactory: NativeContextPageFactory | undefined
export function registerNativeContextPageFactory(factory: NativeContextPageFactory): void {
  contextPageFactory = factory
}
export function createNativeContextBoundPage(
  source: NativeBrowser,
  signal?: AbortSignal,
): NativePage | undefined {
  return contextPageFactory?.(source, signal)
}

export function waitNativeContextPageEvent(
  page: NativePage,
  event: 'popup' | 'close',
  options: { timeout?: number | undefined } = {},
): Promise<NativePage> {
  const entry = nativeContextPageBinding(page)
  return entry.context.waitForPageEvent(entry.id, event, options)
}
export function resolveNativeContextUrl(page: NativePage, url: string): string {
  const baseURL = nativeContextPageBindings.get(page)?.context.baseURL
  return baseURL === undefined ? url : new URL(url, baseURL).href
}

export function resolveNativeContextUrlGlob(page: NativePage, pattern: string): string {
  return resolveNativeURLGlob(pattern, nativeContextPageBindings.get(page)?.context.baseURL)
}
