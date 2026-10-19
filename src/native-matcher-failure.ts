// SPDX-License-Identifier: MIT
// Local Error identity only; the native runner owns soft admission and outcome.
import { AsyncLocalStorage } from 'node:async_hooks'

import { consumeNativeAssertionDeadline } from './native-assertion-deadline.js'
import { type UntrustedInput } from './untrusted.js'

const invocationBrand: unique symbol = Symbol('native matcher invocation')
const receiptBrand: unique symbol = Symbol('native matcher failure receipt')
export interface NativeMatcherInvocation {
  readonly [invocationBrand]: true
}
export interface NativeMatcherFailureReceipt {
  readonly [receiptBrand]: true
}
interface ActualFailure {
  readonly invocation: NativeMatcherInvocation
  readonly error: Error
  readonly receipt: NativeMatcherFailureReceipt
  consumed: boolean
  readonly kind: 'mismatch' | 'assertionDeadline'
}
const active = new AsyncLocalStorage<NativeMatcherInvocation>()
const live = new WeakSet<NativeMatcherInvocation>()
const errors = new WeakMap<Error, ActualFailure>()
const receipts = new WeakMap<NativeMatcherFailureReceipt, ActualFailure>()

/** Authored context is added only when constructing an actual mismatch Error. */
export function nativeMatcherMessage(message: string, customMessage?: string): string {
  return customMessage === undefined || customMessage === ''
    ? message
    : `${customMessage}\n\n${message}`
}

export function createNativeMatcherInvocation(): NativeMatcherInvocation {
  const ticket: NativeMatcherInvocation = Object.freeze({ [invocationBrand]: true as const })
  live.add(ticket)
  return ticket
}
export function closeNativeMatcherInvocation(invocation: NativeMatcherInvocation): void {
  live.delete(invocation)
}
export function runNativeMatcherInvocation<T>(
  invocation: NativeMatcherInvocation,
  callback: () => T,
): T {
  return active.run(invocation, callback)
}
/** Author callbacks cannot issue a mismatch on an enclosing native matcher ticket. */
export function withoutNativeMatcherInvocation<T>(callback: () => T): T {
  return active.exit(callback)
}
/** Mark the original Error at a genuine matcher failure producer, never a catch-all. */
export function nativeMatcherFailure<T extends Error>(error: T): T {
  const invocation = active.getStore()
  if (!invocation || !live.has(invocation) || errors.has(error)) return error
  const receipt: NativeMatcherFailureReceipt = Object.freeze({ [receiptBrand]: true as const })
  const actual: ActualFailure = { invocation, error, receipt, consumed: false, kind: 'mismatch' }
  errors.set(error, actual)
  receipts.set(receipt, actual)
  return error
}
/** A source-issued original deadline is distinct from an observed mismatch. */
export function nativeMatcherAssertionDeadline<T extends Error>(error: T, source: Error): T {
  const observation = consumeNativeAssertionDeadline(source)
  if (!observation) throw new Error('Native assertion deadline has no actual source receipt')
  const invocation = active.getStore()
  if (!invocation || !live.has(invocation) || errors.has(error)) return error
  const receipt: NativeMatcherFailureReceipt = Object.freeze({ [receiptBrand]: true as const })
  const actual: ActualFailure = {
    invocation,
    error,
    receipt,
    consumed: false,
    kind: 'assertionDeadline',
  }
  errors.set(error, actual)
  receipts.set(receipt, actual)
  return error
}
/** Only the invocation that actually produced the Error can settle it as soft. */
export function takeNativeMatcherFailure(
  invocation: NativeMatcherInvocation,
  error: UntrustedInput,
): NativeMatcherFailureReceipt | undefined {
  if (!(error instanceof Error)) return undefined
  const actual = errors.get(error)
  if (actual?.invocation !== invocation || actual.consumed) return undefined
  actual.consumed = true
  return actual.receipt
}
/** The host retains this exact object; no Error is reconstructed from metadata. */
export function getNativeMatcherFailure(receipt: NativeMatcherFailureReceipt): Error {
  const actual = receipts.get(receipt)
  if (!actual?.consumed)
    throw new TypeError('Native matcher failure has no actual invocation receipt')
  return actual.error
}
export function getNativeMatcherFailureKind(
  receipt: NativeMatcherFailureReceipt,
): 'mismatch' | 'assertionDeadline' {
  const actual = receipts.get(receipt)
  if (!actual?.consumed)
    throw new TypeError('Native matcher failure has no actual invocation receipt')
  return actual.kind
}
export function getNativeAssertionDeadlineFailure(receipt: NativeMatcherFailureReceipt): Error {
  if (getNativeMatcherFailureKind(receipt) !== 'assertionDeadline')
    throw new TypeError('Native matcher receipt is not an assertion deadline')
  return getNativeMatcherFailure(receipt)
}
