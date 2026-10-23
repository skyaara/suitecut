// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'

import { raceWithAbort } from './abort.js'
import { type NativeBrowser } from './native-browser.js'
import {
  bindNativeRetryContinuation,
  retainNativeRetryOperation,
} from './native-callback-operations.js'
import { type NativeActionStage } from './native-protocol.js'
import { runNativeMatcherUserPromise } from './native-soft-assertions.js'

export type NativeBeforeStableHook = () => void | Promise<void>
const physicalCounts = new WeakMap<AbortSignal, { pending: number }>()
let physicalPending = 0
const physicalCapacity = 64

/** Actual user closures remain pending even when their native owner is cancelled. */
export function nativeActionHookPhysicalCount(source: NativeBrowser): number {
  return physicalCounts.get(source.processSignal ?? source.signal)?.pending ?? 0
}

/** Pauses are created by the native action scheduler, after DOM resolution. */
export function subscribeNativeActionHook(
  source: NativeBrowser,
  callback: NativeBeforeStableHook,
  action: AbortController,
): { clientToken: string; dispose(): void } {
  if (!source.onActionStage) throw new Error('Native action-stage events are unavailable')
  source.signal.throwIfAborted()
  const physicalSignal = source.processSignal ?? source.signal
  let count = physicalCounts.get(physicalSignal)
  if (!count) {
    count = { pending: 0 }
    physicalCounts.set(physicalSignal, count)
  }
  const physical = count
  const clientToken = randomUUID()
  const lifetime = new AbortController()
  const signal = AbortSignal.any([source.signal, action.signal, lifetime.signal])
  let disposed = false
  let pendingToken = 0
  let lastToken = 0
  const run = bindNativeRetryContinuation(async (stage: NativeActionStage): Promise<void> => {
    if (physicalPending >= physicalCapacity) {
      action.abort(new Error('Native action hook physical callback capacity exceeded'))
      return
    }
    ++physicalPending
    ++physical.pending
    const raw = runNativeMatcherUserPromise(callback)
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
      await raceWithAbort(raw, signal)
      if (disposed) return
      pendingToken = 0
      await source.runAutomation(
        'continueAction',
        {
          target: stage.id,
          generation: stage.generation,
          token: stage.token,
          clientToken,
        },
        signal,
      )
    } catch (error) {
      if (!disposed && !action.signal.aborted) action.abort(error)
    } finally {
      if (pendingToken === stage.token) pendingToken = 0
    }
  })
  const detach = source.onActionStage((stage) => {
    if (disposed || stage.clientToken !== clientToken || stage.token <= lastToken) return
    if (pendingToken) {
      action.abort(new Error('A native action emitted overlapping pause stages'))
      return
    }
    lastToken = stage.token
    pendingToken = stage.token
    void run(stage)
  })
  return {
    clientToken,
    dispose() {
      if (disposed) return
      disposed = true
      detach()
      lifetime.abort(new Error('Native action hook completed'))
    },
  }
}
