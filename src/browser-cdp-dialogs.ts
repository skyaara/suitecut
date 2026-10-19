// SPDX-License-Identifier: MIT
import {
  createProtocolDialog,
  type ProtocolDialog,
  type ProtocolDialogControl,
  type ProtocolDialogType,
} from './browser-protocol-dialog.js'
import { type CDPPageRequest } from './browser-protocol-types.js'
import { type BrowserWire } from './browser-wire.js'
import { type UntrustedInput } from './untrusted.js'

type Listener = (dialog: ProtocolDialog) => void | Promise<void>
interface PendingDialog {
  sessionId: string
  control: ProtocolDialogControl
  abort: AbortController
}

function dialogType(value: UntrustedInput): ProtocolDialogType | undefined {
  return value === 'alert' || value === 'confirm' || value === 'prompt' || value === 'beforeunload'
    ? value
    : undefined
}

/** Session-scoped dialog events with one live handle per page and no polling. */
export function createCDPDialogControls(
  wire: BrowserWire,
  _request: CDPPageRequest,
  hooks: { pageForSession(sessionId: string): string | undefined },
) {
  const listeners = new Map<string, Set<Listener>>()
  const pending = new Map<string, PendingDialog>()
  const removers: (() => void)[] = []
  let disposed = false

  function invalidate(entry: PendingDialog, error: Error): void {
    entry.control.invalidate(error)
    entry.abort.abort(error)
  }
  function closePage(pageId: string): void {
    const entry = pending.get(pageId)
    if (entry) invalidate(entry, new Error('Browser page closed'))
    pending.delete(pageId)
    listeners.delete(pageId)
  }
  function detachSession(sessionId: string): void {
    // The hook can already have lost the session; the pending entry owns its origin.
    for (const [pageId, entry] of pending) {
      if (entry.sessionId !== sessionId) continue
      invalidate(entry, new Error('Browser dialog session detached'))
      pending.delete(pageId)
    }
  }
  function dismissFallback(entry: PendingDialog): void {
    if (entry.control.isOpen() && !entry.control.isHandling())
      void entry.control.dialog.dismiss().catch(() => undefined)
  }

  removers.push(
    wire.on('Page.javascriptDialogOpening', (event, sessionId) => {
      if (disposed || !sessionId) return
      const pageId = hooks.pageForSession(sessionId)
      const type = dialogType(event.type)
      if (!pageId || !type || typeof event.message !== 'string') return
      const previous = pending.get(pageId)
      if (previous) invalidate(previous, new Error('Browser dialog was replaced'))
      const abort = new AbortController()
      const control = createProtocolDialog(
        {
          type,
          message: event.message,
          defaultValue: typeof event.defaultPrompt === 'string' ? event.defaultPrompt : '',
        },
        async (accept, promptText) => {
          try {
            await wire.request(
              'Page.handleJavaScriptDialog',
              { accept, promptText },
              { sessionId, signal: abort.signal },
            )
          } finally {
            // A closed notification can precede the command response. Retain ownership
            // until that response so intervening page teardown can still abort it.
            if (pending.get(pageId) === entry && !control.isOpen()) pending.delete(pageId)
          }
        },
      )
      const entry: PendingDialog = { sessionId, control, abort }
      pending.set(pageId, entry)
      const subscribers = [...(listeners.get(pageId) ?? [])]
      if (!subscribers.length) dismissFallback(entry)
      else
        for (const listener of subscribers) {
          try {
            // Async listener rejections need the same safe fallback as thrown listeners.
            const result = listener(control.dialog)
            void Promise.resolve(result).catch(() => dismissFallback(entry))
          } catch {
            dismissFallback(entry)
          }
        }
    }),
  )
  removers.push(
    wire.on('Page.javascriptDialogClosed', (_event, sessionId) => {
      if (!sessionId) return
      const pageId = hooks.pageForSession(sessionId)
      if (!pageId) return
      const entry = pending.get(pageId)
      if (entry?.sessionId !== sessionId) return
      const handling = entry.control.isHandling()
      entry.control.closed()
      if (!handling) pending.delete(pageId)
    }),
  )
  removers.push(
    wire.on('Target.targetDestroyed', (event) => {
      if (typeof event.targetId === 'string') closePage(event.targetId)
    }),
  )
  removers.push(
    wire.on('Target.detachedFromTarget', (event) => {
      if (typeof event.sessionId === 'string') detachSession(event.sessionId)
    }),
  )

  function dispose(): void {
    if (disposed) return
    disposed = true
    for (const remove of removers) remove()
    removers.length = 0
    for (const entry of pending.values())
      invalidate(entry, new Error('Browser dialog connection closed'))
    pending.clear()
    listeners.clear()
  }
  void wire.closed.then(dispose)

  return {
    subscribe(pageId: string, listener: Listener): () => void {
      if (disposed || !wire.isConnected()) throw new Error('Browser connection closed')
      let subscribers = listeners.get(pageId)
      if (!subscribers) {
        subscribers = new Set()
        listeners.set(pageId, subscribers)
      }
      subscribers.add(listener)
      return () => {
        subscribers.delete(listener)
        if (!subscribers.size && listeners.get(pageId) === subscribers) {
          listeners.delete(pageId)
          const entry = pending.get(pageId)
          if (entry) dismissFallback(entry)
        }
      }
    },
    closePage,
    detachSession,
    dispose,
  }
}
