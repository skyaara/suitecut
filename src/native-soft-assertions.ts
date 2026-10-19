// SPDX-License-Identifier: MIT
// Thin author invocation/transport bridge. Native C++ owns every soft outcome.
import { AsyncLocalStorage } from 'node:async_hooks'
import { isPromise } from 'node:util/types'

import * as z from 'zod'

import {
  nativeRetryInfrastructureFailure,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import {
  closeNativeMatcherInvocation,
  createNativeMatcherInvocation,
  runNativeMatcherInvocation,
  takeNativeMatcherFailure,
  getNativeMatcherFailureKind,
  withoutNativeMatcherInvocation,
  type NativeMatcherFailureReceipt,
} from './native-matcher-failure.js'
import { nativeRunnerCallsites } from './native-runner-location.js'
import { type UntrustedInput } from './untrusted.js'

// Keep browser-only consumers from evaluating the test-suite module.
// eslint-disable-next-line import-x/consistent-type-specifier-style
import type { NativeTestAnnotation } from './native-suite.js'

export type NativeSoftAssertionSettlement =
  | { readonly kind: 'passed' }
  | { readonly kind: 'softFailure'; readonly receipt: NativeMatcherFailureReceipt }
  | { readonly kind: 'assertionDeadline'; readonly receipt: NativeMatcherFailureReceipt }
  | { readonly kind: 'otherFailure'; readonly error: UntrustedInput }
export interface NativeSoftAssertionLease {
  readonly signal: AbortSignal
  /** Remaining original budget supplied by native admission; 0 is unbounded. */
  readonly timeoutMs: number
  settle(result: NativeSoftAssertionSettlement): Promise<void>
}
type Begin = (location?: NativeTestAnnotation['location']) => Promise<NativeSoftAssertionLease>
let bridge: Begin | undefined
const invocation = new AsyncLocalStorage<{
  readonly lease: NativeSoftAssertionLease
  active: boolean
}>()
const Timeout = z.number().int().min(0).max(120_000)

/** Installed by the real native runner host, never by an external expect adapter. */
export function installNativeSoftAssertionBridge(begin: Begin): void {
  if (bridge || typeof begin !== 'function')
    throw new Error('Native soft assertion bridge is already installed or invalid')
  bridge = begin
}
/** One actual matcher invocation settles one native-admitted physical receipt. */
export async function runNativeSoftAssertion(callback: () => Promise<void>): Promise<void> {
  if (!bridge) throw new Error('Native soft assertions require an active native test callback')
  // begin registers its real request synchronously, before the first await.
  const lease = await bridge(nativeRunnerCallsites()[0])
  if (!(lease.signal instanceof AbortSignal) || typeof lease.settle !== 'function')
    throw new Error('Native soft assertion admission returned an invalid lease')
  Timeout.parse(lease.timeoutMs)
  const ticket = createNativeMatcherInvocation()
  const context = { lease, active: true }
  let result: NativeSoftAssertionSettlement
  try {
    lease.signal.throwIfAborted()
    await invocation.run(context, () => runNativeMatcherInvocation(ticket, callback))
    result = { kind: 'passed' }
  } catch (error) {
    const receipt = takeNativeMatcherFailure(ticket, error as UntrustedInput)
    result = receipt
      ? {
          kind:
            getNativeMatcherFailureKind(receipt) === 'assertionDeadline'
              ? 'assertionDeadline'
              : 'softFailure',
          receipt,
        }
      : { kind: 'otherFailure', error: error as UntrustedInput }
  } finally {
    context.active = false
    closeNativeMatcherInvocation(ticket)
  }
  // An ACK failure is never caught/softened as a second matcher settlement.
  await lease.settle(result)
  // Preserve the actual author-thrown value, including primitives.
  // eslint-disable-next-line @typescript-eslint/only-throw-error
  if (result.kind === 'otherFailure') throw result.error
}

/**
 * Suspend both private receipts and lease inheritance across authored callbacks.
 * Async descendants remain outside; explicit nested soft calls admit new leases.
 */
export function runNativeMatcherUserCallback<T>(callback: () => T): T {
  return withoutNativeRetryOperation(() =>
    invocation.exit(() => withoutNativeMatcherInvocation(callback)),
  )
}

const IntrinsicPromise = Promise
// eslint-disable-next-line @typescript-eslint/unbound-method
const intrinsicThen = Promise.prototype.then
/** Normalize author returns while suspension still owns thenable assimilation. */
export function runNativeMatcherUserPromise<T>(callback: () => T): Promise<Awaited<T>> {
  return runNativeMatcherUserCallback(() => {
    let raw: T
    try {
      raw = callback()
    } catch (error) {
      // Preserve the actual author-thrown value, including non-Errors.
      // eslint-disable-next-line @typescript-eslint/prefer-promise-reject-errors
      return IntrinsicPromise.reject(error)
    }
    if (!isPromise(raw)) return IntrinsicPromise.resolve(raw)
    return new IntrinsicPromise<Awaited<T>>((resolve, reject) => {
      try {
        void Reflect.apply(intrinsicThen, raw, [resolve, reject])
      } catch (error) {
        // A failed observer cannot retire the real unobserved Promise.
        nativeRetryInfrastructureFailure(error as UntrustedInput)
      }
    })
  })
}

/** No host clock or retry scheduler: clamp to the actual native admission budget. */
export function nativeSoftOperationOptions(
  timeout: UntrustedInput,
  signal?: AbortSignal,
): { timeout: number; signal: AbortSignal } | undefined {
  const context = invocation.getStore()
  if (!context?.active) return undefined
  const lease = context.lease
  const requested = Timeout.parse(timeout ?? 5000)
  return {
    timeout:
      lease.timeoutMs === 0
        ? requested
        : requested === 0
          ? lease.timeoutMs
          : Math.min(requested, lease.timeoutMs),
    signal:
      signal && signal !== lease.signal ? AbortSignal.any([signal, lease.signal]) : lease.signal,
  }
}
