// SPDX-License-Identifier: MIT
import { toError, type UntrustedInput } from './untrusted.js'

export type ProtocolDialogType = 'alert' | 'confirm' | 'prompt' | 'beforeunload'

/** A real browser dialog. Answering it never evaluates script in the blocked renderer. */
export interface ProtocolDialog {
  type(): ProtocolDialogType
  message(): string
  defaultValue(): string
  accept(promptText?: string): Promise<void>
  dismiss(): Promise<void>
}

export interface ProtocolDialogControl {
  readonly dialog: ProtocolDialog
  isOpen(): boolean
  isHandling(): boolean
  /** A normal dialog-closed event can precede the answer command response. */
  closed(): void
  /** Page/session teardown also rejects any outstanding answer immediately. */
  invalidate(error: Error): void
}

/** Internal lifecycle owner; closed dialog handles retain no transport or page. */
export function createProtocolDialog(
  info: { type: ProtocolDialogType; message: string; defaultValue: string },
  answer: (accept: boolean, promptText: string) => Promise<void>,
): ProtocolDialogControl {
  let send: typeof answer | undefined = answer
  let state: 'open' | 'handling' | 'handled' | 'closed' = 'open'
  let closedError: Error | undefined
  let rejectPending: ((error: Error) => void) | undefined

  function respond(accept: boolean, promptText: string): Promise<void> {
    if (typeof promptText !== 'string')
      return Promise.reject(new TypeError('Dialog prompt text must be a string'))
    if (state !== 'open' || !send)
      return Promise.reject(
        closedError ??
          new Error(state === 'closed' ? 'Browser dialog closed' : 'Dialog is already handled'),
      )
    const request = send
    state = 'handling'
    return new Promise<void>((resolve, reject) => {
      rejectPending = reject
      // Promise.resolve also converts a synchronously throwing transport into rejection.
      void Promise.resolve()
        .then(() => {
          if (closedError) throw closedError
          return request(accept, promptText)
        })
        .then(
          () => {
            rejectPending = undefined
            if (closedError) {
              reject(closedError)
              return
            }
            state = 'handled'
            send = undefined
            resolve()
          },
          (error) => {
            rejectPending = undefined
            // A failed command may be retried while the same browser dialog is open.
            if (state === 'handling') state = 'open'
            reject(closedError ?? toError(error as UntrustedInput))
          },
        )
    })
  }

  return {
    dialog: {
      type: () => info.type,
      message: () => info.message,
      defaultValue: () => info.defaultValue,
      accept: (promptText = '') => respond(true, promptText),
      dismiss: () => respond(false, ''),
    },
    isOpen: () => state === 'open' || state === 'handling',
    isHandling: () => state === 'handling',
    closed() {
      if (state !== 'handled') state = 'closed'
      send = undefined
    },
    invalidate(error) {
      state = 'closed'
      closedError = error
      send = undefined
      rejectPending?.(error)
      rejectPending = undefined
    },
  }
}
