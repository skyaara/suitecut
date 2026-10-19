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

export interface NativeURLPredicateSnapshot {
  id: number
  generation: number
  token: number
  clientToken: string
  url: string
}

export type NativeURLPredicate = (url: URL) => boolean | Promise<boolean>

export interface NativeURLPattern {
  test(url: string): boolean
}

const snapshots = z.object({ matches: z.boolean(), url: z.string().max(8192) })
const physicalCallbacks = new WeakMap<AbortSignal, { pending: number }>()
const physicalCapacity = 64
let totalPhysicalCallbacks = 0

/** Actual outstanding user closures; IPC cancellation does not decrease this count. */
export function nativeURLPredicatePhysicalCount(source: NativeBrowser): number {
  return physicalCallbacks.get(source.processSignal ?? source.signal)?.pending ?? 0
}

/** Host callbacks use a physical process-wide latch, separate from cancellation. */
export async function runNativeURLPredicate(
  source: NativeBrowser,
  predicate: NativeURLPredicate,
  options: {
    timeout?: number | undefined
    isNot?: boolean | undefined
    waitForURL?: boolean
    waitUntil?: string
    api?: string
    frame?: string
    signal?: AbortSignal
  } = {},
): Promise<{ matches: boolean; url: string }> {
  return runNativeRetryOperation(async () => {
    if (!source.onURLPredicate) throw new Error('Native URL predicate events are unavailable')
    source.signal.throwIfAborted()
    const timeout = z
      .number()
      .int()
      .min(0)
      .max(120_000)
      .parse(options.timeout ?? 5000)
    const physicalSignal = source.processSignal ?? source.signal
    let state = physicalCallbacks.get(physicalSignal)
    if (!state) {
      state = { pending: 0 }
      physicalCallbacks.set(physicalSignal, state)
    }
    const physical = state
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
    const compare = bindNativeRetryContinuation(
      async (snapshot: NativeURLPredicateSnapshot): Promise<void> => {
        if (totalPhysicalCallbacks >= physicalCapacity) {
          action.abort(new Error('Native URL host predicate physical callback capacity exceeded'))
          return
        }
        ++physical.pending
        ++totalPhysicalCallbacks
        // Observe the original promise independently: cancellation ends the IPC
        // owner, but cannot pretend to stop an arbitrary user's async closure.
        const raw = runNativeMatcherUserPromise(() => predicate(new URL(snapshot.url)))
        retainNativeRetryOperation(raw)
        void raw.then(
          () => {
            --physical.pending
            --totalPhysicalCallbacks
          },
          () => {
            --physical.pending
            --totalPhysicalCallbacks
          },
        )
        try {
          const matches = z.boolean().parse(await raceWithAbort(raw, signal))
          if (disposed || signal.aborted) return
          pendingToken = 0
          await source.runAutomation(
            'continueURLPredicate',
            {
              target: snapshot.id,
              generation: snapshot.generation,
              token: snapshot.token,
              clientToken,
              matches,
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
    const detach = source.onURLPredicate((snapshot: NativeURLPredicateSnapshot) => {
      if (disposed || snapshot.clientToken !== clientToken || snapshot.token <= lastToken) return
      if (pendingToken) {
        action.abort(new Error('A native URL predicate emitted overlapping host observations'))
        return
      }
      lastToken = snapshot.token
      pendingToken = snapshot.token
      void compare(snapshot)
    })
    try {
      return snapshots.parse(
        await source.runAutomation(
          'urlPredicate',
          {
            clientToken,
            isNot: options.isNot ?? false,
            timeoutMs: timeout,
            ...(options.waitForURL
              ? {
                  waitForURL: true,
                  waitUntil: options.waitUntil,
                  api: options.api,
                  ...(options.frame ? { frame: options.frame } : {}),
                }
              : {}),
          },
          signal,
        ),
      )
    } finally {
      disposed = true
      detach()
      lifetime.abort(new Error('Native URL predicate completed'))
    }
  })
}

/** Only actual platform URLPattern instances are accepted as pattern objects. */
export function isNativeURLPattern(value: UntrustedInput): value is NativeURLPattern {
  const constructor: UntrustedInput = Reflect.get(globalThis, 'URLPattern')
  return typeof constructor === 'function' && value instanceof constructor
}
