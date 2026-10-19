import { randomUUID } from 'node:crypto'

import * as z from 'zod'

import { raceWithAbort } from './abort.js'
import { type NativeBrowser } from './native-browser.js'
import {
  bindNativeRetryContinuation,
  retainNativeRetryOperation,
  runNativeRetryOperation,
} from './native-callback-operations.js'
import { runNativeMatcherUserPromise } from './native-soft-assertions.js'
import { type UntrustedInput } from './untrusted.js'

export type NativeEventPredicateSource = Pick<
  NativeBrowser,
  'signal' | 'processSignal' | 'runAutomation' | 'onEventPredicate' | 'eventSequence'
>

export interface NativeEventPredicateSnapshot {
  id: number
  generation: number
  token: number
  clientToken: string
  event: string
  sequence: number
  value: UntrustedInput
}

export type NativeEventPredicate<T> = (value: T) => boolean | Promise<boolean>
export interface NativeEventPredicateOptions {
  timeout?: number | undefined
  signal?: AbortSignal | undefined
}

const counts = new WeakMap<AbortSignal, { pending: number }>()
let physicalPending = 0
const capacity = 64

/** Counts actual outstanding host closures; cancelling IPC cannot settle them. */
export function nativeEventPredicatePhysicalCount(source: NativeEventPredicateSource): number {
  return counts.get(source.processSignal ?? source.signal)?.pending ?? 0
}

/** One native journal cursor/deadline survives every rejected host candidate. */
export async function waitNativeEventPredicate<T>(
  source: NativeEventPredicateSource,
  event: string,
  decode: (value: UntrustedInput) => T | Promise<T>,
  predicate: NativeEventPredicate<T>,
  options: NativeEventPredicateOptions & { afterSequence?: number | undefined } = {},
  target?: { command: 'websocket'; fields: Record<string, UntrustedInput> },
): Promise<T> {
  return runNativeRetryOperation(async () => {
    if (!source.onEventPredicate) throw new Error('Native event predicate messages are unavailable')
    source.signal.throwIfAborted()
    options.signal?.throwIfAborted()
    const timeout = z
      .number()
      .int()
      .min(0)
      .max(target ? 2_147_483_647 : 120_000)
      .parse(options.timeout ?? 30_000)
    const cursor = options.afterSequence ?? source.eventSequence?.()
    const key = source.processSignal ?? source.signal
    let count = counts.get(key)
    if (!count) {
      count = { pending: 0 }
      counts.set(key, count)
    }
    const physical = count
    const clientToken = randomUUID()
    const action = new AbortController()
    const lifetime = new AbortController()
    const signal = AbortSignal.any([
      source.signal,
      action.signal,
      lifetime.signal,
      ...(options.signal ? [options.signal] : []),
    ])
    let pendingToken = 0,
      lastToken = 0,
      disposed = false
    let accepted: { value: T } | undefined
    const compare = bindNativeRetryContinuation(
      async (snapshot: NativeEventPredicateSnapshot): Promise<void> => {
        if (physicalPending >= capacity) {
          action.abort(new Error('Native event predicate physical host callback capacity exceeded'))
          return
        }
        ++physicalPending
        ++physical.pending
        const raw = Promise.resolve().then(async () => {
          const value = await decode(snapshot.value)
          return {
            value,
            matches: z.boolean().parse(await runNativeMatcherUserPromise(() => predicate(value))),
          }
        })
        retainNativeRetryOperation(raw)
        void raw.then(
          () => {
            --physicalPending
            --physical.pending
          },
          () => {
            --physicalPending
            --physical.pending
          },
        )
        try {
          const compared = await raceWithAbort(raw, signal)
          if (disposed || signal.aborted) return
          if (compared.matches) accepted = { value: compared.value }
          pendingToken = 0
          await source.runAutomation(
            'continueEventPredicate',
            {
              target: snapshot.id,
              generation: snapshot.generation,
              token: snapshot.token,
              clientToken,
              matches: compared.matches,
            },
            signal,
          )
        } catch (error) {
          if (!disposed && !action.signal.aborted) action.abort(error)
        } finally {
          if (pendingToken === snapshot.token) pendingToken = 0
        }
      },
    )
    const detach = source.onEventPredicate((snapshot: NativeEventPredicateSnapshot) => {
      if (disposed || snapshot.clientToken !== clientToken || snapshot.token <= lastToken) return
      if (snapshot.event !== event || pendingToken) {
        action.abort(
          new Error('Native event predicate emitted an invalid or overlapping candidate'),
        )
        return
      }
      lastToken = snapshot.token
      pendingToken = snapshot.token
      void compare(snapshot)
    })
    try {
      await source.runAutomation(
        target?.command ?? 'waitEventPredicate',
        {
          ...target?.fields,
          event,
          clientToken,
          timeoutMs: timeout,
          ...(cursor === undefined ? {} : { afterSequence: cursor }),
        },
        signal,
      )
      if (!accepted)
        throw new Error('Native event predicate completed without an accepted host candidate')
      return accepted.value
    } finally {
      disposed = true
      detach()
      lifetime.abort(new Error('Native event predicate completed'))
    }
  })
}
