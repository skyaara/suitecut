// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type NativeBrowser } from './native-browser.js'
import {
  waitNativeEventPredicate,
  type NativeEventPredicate,
  type NativeEventPredicateOptions,
} from './native-event-predicate.js'
import { type NativePage } from './native-page.js'
import { NativeDialogInfoSchema, type NativeDialogInfo } from './native-protocol.js'
import { nativeTraceStack } from './native-trace-metadata.js'
import { toError, type UntrustedInput } from './untrusted.js'

export class NativeDialog {
  private handled = false
  constructor(
    private readonly owner: NativePage,
    private readonly info: NativeDialogInfo,
  ) {}
  type(): NativeDialogInfo['type'] {
    return this.info.type
  }
  message(): string {
    return this.info.message
  }
  defaultValue(): string {
    return this.info.defaultValue
  }
  page(): NativePage {
    return this.owner
  }
  accept(promptText = ''): Promise<void> {
    return this.answer(true, promptText)
  }
  dismiss(): Promise<void> {
    return this.answer(false, '')
  }
  private async answer(accept: boolean, promptText: string): Promise<void> {
    z.string().parse(promptText)
    if (this.handled) throw new Error('Dialog is already handled')
    this.handled = true
    await this.owner.source.runAutomation('dialog', {
      action: 'answer',
      traceStack: nativeTraceStack(),
      dialogId: this.info.id,
      accept,
      promptText,
    })
  }
}

type DialogListener = (dialog: NativeDialog) => void
const controllers = new WeakMap<NativeBrowser, NativeDialogEvents>()

/** Native waits own deadlines; the SDK only delivers real CEF dialog events. */
export class NativeDialogEvents {
  private readonly listeners: DialogListener[] = []
  private readonly closedListeners: DialogListener[] = []
  private readonly opened = new Map<string, NativeDialog>()
  private readonly ready: Promise<UntrustedInput>
  private error: Error | undefined
  private constructor(private readonly page: NativePage) {
    const source = page.source
    if (!source.onDialogMessage) throw new Error('Native dialog events unavailable')
    const detach = source.onDialogMessage((info) => {
      const dialog = this.opened.get(info.id) ?? new NativeDialog(page, info)
      if (info.state === 'closed') this.opened.delete(info.id)
      else {
        if (this.opened.size >= 64) throw new Error('Native dialog registry capacity exceeded')
        this.opened.set(info.id, dialog)
      }
      const listeners = [...(info.state === 'closed' ? this.closedListeners : this.listeners)]
      if (listeners.length) for (const listener of listeners) listener(dialog)
      else if (info.state !== 'closed')
        void (dialog.type() === 'beforeunload' ? dialog.accept() : dialog.dismiss()).catch(
          (error: UntrustedInput) => {
            if (!source.signal.aborted) this.error = toError(error)
          },
        )
    })
    ;(source.pageSignal ?? source.signal).addEventListener(
      'abort',
      () => {
        detach()
        this.listeners.length = 0
        this.closedListeners.length = 0
        this.opened.clear()
      },
      { once: true },
    )
    this.ready = source.runAutomation('dialog', { action: 'subscribe', enabled: true })
    void this.ready.catch((error: UntrustedInput) => {
      this.error = toError(error)
    })
  }
  static forPage(page: NativePage): NativeDialogEvents {
    let controller = controllers.get(page.source)
    if (!controller) {
      controller = new NativeDialogEvents(page)
      controllers.set(page.source, controller)
    }
    return controller
  }
  on(listener: DialogListener, closed = false, prepend = false): void {
    this.page.source.signal.throwIfAborted()
    if (this.error) throw this.error
    const listeners = closed ? this.closedListeners : this.listeners
    if (prepend) listeners.unshift(listener)
    else listeners.push(listener)
  }
  off(listener: DialogListener, closed = false): void {
    const listeners = closed ? this.closedListeners : this.listeners
    const index = listeners.lastIndexOf(listener)
    if (index >= 0) listeners.splice(index, 1)
  }
  async wait(
    options: NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativeDialog> },
    closed = false,
  ): Promise<NativeDialog> {
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
        predicate: z
          .custom<NativeEventPredicate<NativeDialog>>((value) => typeof value === 'function')
          .optional(),
      })
      .parse(options)
    const sequence = this.page.source.eventSequence?.()
    const retain = (): void => undefined
    this.on(retain, closed)
    try {
      if (config.predicate)
        return await waitNativeEventPredicate(
          this.page.source,
          closed ? 'dialogclosed' : 'dialog',
          async (value) => {
            await this.ready
            if (this.error) throw this.error
            const info = NativeDialogInfoSchema.parse(value)
            return this.opened.get(info.id) ?? new NativeDialog(this.page, info)
          },
          config.predicate,
          { ...config, afterSequence: sequence },
        )
      await this.ready
      if (this.error) throw this.error
      const result = await this.page.source.runAutomation(
        'waitEvent',
        {
          event: closed ? 'dialogclosed' : 'dialog',
          timeoutMs: config.timeout ?? 30_000,
          ...(sequence === undefined ? {} : { afterSequence: sequence }),
        },
        config.signal,
      )
      return new NativeDialog(this.page, NativeDialogInfoSchema.parse(result))
    } finally {
      this.off(retain, closed)
    }
  }
}
