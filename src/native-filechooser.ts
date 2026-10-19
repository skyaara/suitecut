// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type NativeBrowser } from './native-browser.js'
import {
  waitNativeEventPredicate,
  type NativeEventPredicate,
  type NativeEventPredicateOptions,
} from './native-event-predicate.js'
import { type NativeFormOptions, type NativeInputFiles } from './native-form.js'
import { type NativeElementHandle, type NativePage } from './native-page.js'
import { NativeFileChooserInfoSchema, type NativeFileChooserInfo } from './native-protocol.js'
import { toError, type UntrustedInput } from './untrusted.js'

export class NativeFileChooser {
  constructor(
    private readonly owner: NativePage,
    private readonly input: NativeElementHandle,
    private readonly multiple: boolean,
  ) {}
  page(): NativePage {
    return this.owner
  }
  element(): NativeElementHandle {
    return this.input
  }
  isMultiple(): boolean {
    return this.multiple
  }
  setFiles(files: NativeInputFiles, options: Omit<NativeFormOptions, 'force'> = {}): Promise<void> {
    return this.input.setInputFiles(files, options)
  }
}

type FileChooserListener = (chooser: NativeFileChooser) => void
const controllers = new WeakMap<NativeBrowser, NativeFileChooserEvents>()

/** Native event journals own chooser waits, timeouts, and genuine node identity. */
export class NativeFileChooserEvents {
  private readonly listeners: FileChooserListener[] = []
  private readonly opened = new Map<string, Promise<NativeFileChooser>>()
  private readonly ready: Promise<UntrustedInput>
  private error: Error | undefined
  private constructor(
    private readonly page: NativePage,
    private readonly createElement: (
      owner: NativePage,
      metadata: UntrustedInput,
    ) => NativeElementHandle,
  ) {
    const source = page.source
    if (!source.onFileChooserMessage) throw new Error('Native file chooser events unavailable')
    const detach = source.onFileChooserMessage((info) => {
      if (!this.listeners.length) return
      const listeners = [...this.listeners]
      void this.adopt(info).then(
        (chooser) => {
          for (const listener of listeners) {
            if (!this.listeners.includes(listener)) continue
            try {
              listener(chooser)
            } catch (error) {
              queueMicrotask(() => {
                throw error
              })
            }
          }
        },
        (error: UntrustedInput) => {
          // The event's own waiter still observes this rejected adoption. A
          // vanished child cannot make later choosers in a live frame fail.
          if (
            !source.signal.aborted &&
            this.page.frames().some((frame) => frame.identity() === info.frame)
          )
            this.error = toError(error)
        },
      )
    })
    ;(source.pageSignal ?? source.signal).addEventListener(
      'abort',
      () => {
        detach()
        this.listeners.length = 0
        this.opened.clear()
      },
      { once: true },
    )
    this.ready = page.runAutomation('form', { action: 'fileChooserSubscribe', enabled: true })
    void this.ready.catch((error: UntrustedInput) => {
      this.error = toError(error)
    })
  }
  static forPage(
    page: NativePage,
    createElement: (owner: NativePage, metadata: UntrustedInput) => NativeElementHandle,
  ): NativeFileChooserEvents {
    let controller = controllers.get(page.source)
    if (!controller) {
      controller = new NativeFileChooserEvents(page, createElement)
      controllers.set(page.source, controller)
    }
    return controller
  }
  on(listener: FileChooserListener, prepend = false): void {
    this.page.source.signal.throwIfAborted()
    if (this.error) throw this.error
    if (prepend) this.listeners.unshift(listener)
    else this.listeners.push(listener)
  }
  off(listener: FileChooserListener): void {
    const index = this.listeners.lastIndexOf(listener)
    if (index >= 0) this.listeners.splice(index, 1)
  }
  private adopt(info: NativeFileChooserInfo): Promise<NativeFileChooser> {
    let pending = this.opened.get(info.id)
    if (pending) return pending
    pending = (async () => {
      const owner = this.page.frames().find((frame) => frame.identity() === info.frame)
      if (!owner) throw new Error('File chooser frame was detached')
      const metadata = z
        .object({ handle: z.number().int().positive(), guid: z.string(), preview: z.string() })
        .parse(
          await owner.runAutomation('form', {
            action: 'fileChooserElement',
            backendNodeId: info.backendNodeId,
          }),
        )
      return new NativeFileChooser(
        this.page,
        this.createElement(owner, { ...metadata, node: true, frame: info.frame }),
        info.multiple,
      )
    })()
    // Keep delivery/wait races bounded; handles retain their normal explicit disposal lifetime.
    if (this.opened.size >= 64) {
      const oldest = this.opened.keys().next().value
      if (oldest !== undefined) this.opened.delete(oldest)
    }
    this.opened.set(info.id, pending)
    return pending
  }
  async wait(
    options: NativeEventPredicateOptions & {
      predicate?: NativeEventPredicate<NativeFileChooser>
    } = {},
  ): Promise<NativeFileChooser> {
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
        predicate: z
          .custom<NativeEventPredicate<NativeFileChooser>>((value) => typeof value === 'function')
          .optional(),
      })
      .parse(options)
    const afterSequence = this.page.source.eventSequence?.()
    const retain = (): void => undefined
    this.on(retain)
    try {
      if (config.predicate)
        return await waitNativeEventPredicate(
          this.page.source,
          'filechooser',
          async (value) => {
            await this.ready
            if (this.error) throw this.error
            return this.adopt(NativeFileChooserInfoSchema.parse(value))
          },
          config.predicate,
          { ...config, afterSequence },
        )
      await this.ready
      if (this.error) throw this.error
      const result = await this.page.runAutomation(
        'waitEvent',
        {
          event: 'filechooser',
          ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
          ...(afterSequence === undefined ? {} : { afterSequence }),
        },
        config.signal,
      )
      return await this.adopt(NativeFileChooserInfoSchema.parse(result))
    } catch (error) {
      if (config.signal?.aborted) {
        const aborted = new Error('Operation was aborted', {
          cause: config.signal.reason as UntrustedInput,
        })
        aborted.name = 'AbortError'
        throw aborted
      }
      throw error
    } finally {
      this.off(retain)
    }
  }
}
