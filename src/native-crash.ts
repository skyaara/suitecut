// SPDX-License-Identifier: MIT
import * as z from 'zod'

import {
  waitNativeEventPredicate,
  type NativeEventPredicate,
  type NativeEventPredicateOptions,
  type NativeEventPredicateSource,
} from './native-event-predicate.js'
import { type NativePage } from './native-page.js'

const owners = new WeakMap<NativePage, NativeCrashEvents>()
type Listener = (page: NativePage) => void

/** Genuine native renderer crashes retain page ownership until actual closure. */
export class NativeCrashEvents {
  static forPage(page: NativePage): NativeCrashEvents {
    let owner = owners.get(page)
    if (!owner) {
      owner = new NativeCrashEvents(page)
      owners.set(page, owner)
    }
    return owner
  }
  private readonly listeners: Listener[] = []
  private detach: (() => void) | undefined
  private constructor(private readonly page: NativePage) {}
  on(listener: Listener, prepend = false): void {
    if (prepend) this.listeners.unshift(listener)
    else this.listeners.push(listener)
    if (this.detach) return
    const source = this.page.source
    if (!source.onCrashMessage || !source.onContextEvent)
      throw new Error('Native renderer crash events are unavailable')
    const signal = source.processSignal ?? source.signal
    signal.throwIfAborted()
    const detachCrash = source.onCrashMessage(() => {
      for (const callback of [...this.listeners]) {
        try {
          callback(this.page)
        } catch (reason) {
          const thrown =
            reason instanceof Error
              ? reason
              : new Error('Native crash listener threw a non-Error value', { cause: reason })
          queueMicrotask(() => {
            throw thrown
          })
        }
      }
    })
    const detachContext = source.onContextEvent((event) => {
      if (event.event === 'page-closed' && event.pageId === source.pageId) dispose()
    })
    const dispose = (): void => {
      this.listeners.length = 0
      this.detach?.()
    }
    signal.addEventListener('abort', dispose, { once: true })
    this.detach = () => {
      detachCrash()
      detachContext()
      signal.removeEventListener('abort', dispose)
      this.detach = undefined
    }
    if (signal.aborted) dispose()
  }
  off(listener: Listener): void {
    const index = this.listeners.lastIndexOf(listener)
    if (index >= 0) this.listeners.splice(index, 1)
    if (!this.listeners.length) this.detach?.()
  }
  wait(
    options: NativeEventPredicateOptions & {
      predicate?: ((page: never) => boolean | Promise<boolean>) | undefined
    },
  ): Promise<NativePage> {
    const source = this.page.source
    const transport: NativeEventPredicateSource = {
      signal: source.processSignal ?? source.signal,
      ...(source.processSignal ? { processSignal: source.processSignal } : {}),
      runAutomation: source.runAutomation.bind(source),
      ...(source.onEventPredicate
        ? { onEventPredicate: source.onEventPredicate.bind(source) }
        : {}),
      ...(source.eventSequence ? { eventSequence: source.eventSequence.bind(source) } : {}),
    }
    return waitNativeEventPredicate(
      transport,
      'crash',
      (value) => {
        z.strictObject({}).parse(value)
        return this.page
      },
      (options.predicate as NativeEventPredicate<NativePage> | undefined) ?? (() => true),
      options,
    )
  }
}
