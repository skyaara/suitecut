// SPDX-License-Identifier: MIT
// Private native-admitted iteration provenance, never a retry/outcome policy.
import { AsyncLocalStorage } from 'node:async_hooks'
import { isPromise } from 'node:util/types'

import {
  bindNativeBrowserContinuation,
  nativeBrowserCallbackActive,
  retainNativeBrowserOperation,
  runNativeBrowserCleanup,
  runNativeBrowserOperation,
  withoutNativeBrowserOperation,
} from './native-browser-operations.js'
import { type NativeStepOwner } from './native-runner-steps.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeRetryInvocation {
  readonly owner: NativeStepOwner
  readonly assertionID: string
  readonly invocationNonce: string
  readonly step: string
  readonly signal: AbortSignal
  readonly timeoutMs: number
  accepting: boolean
  retired: boolean
  readonly releases: Map<object, () => void>
  readonly operations: Set<object>
  readonly operationsDrained: () => void
  sourceIntent?: boolean
  sourceTicket?: string
  infrastructure?: { readonly error: UntrustedInput }
  readonly producerFailure: (error: UntrustedInput) => void
}
const current = new AsyncLocalStorage<NativeRetryInvocation>()
interface Operation {
  readonly invocation: NativeRetryInvocation
  live: boolean
  references: number
}
const cleanupOperation = new AsyncLocalStorage<Operation>()
const operation = new AsyncLocalStorage<Operation>()
// Captured callable is invoked only with the actual Promise as receiver.
// eslint-disable-next-line @typescript-eslint/unbound-method
const intrinsicThen = Promise.prototype.then
const sameOwner = (a: NativeStepOwner, b: NativeStepOwner) =>
  a.token === b.token && a.scope === b.scope && a.generation === b.generation
export function nativeRetryInvocation(): NativeRetryInvocation | undefined {
  return current.getStore()
}
export function runNativeRetryInvocation<T>(
  invocation: NativeRetryInvocation,
  callback: () => T,
): T {
  return current.run(invocation, callback)
}
export function nativeRetryInfrastructureFailure(
  error: UntrustedInput,
  invocation = current.getStore(),
): void {
  if (invocation && !invocation.retired && !invocation.infrastructure) {
    invocation.infrastructure = { error }
    invocation.producerFailure(error)
  }
}
/** Absence differs from an inherited stale iteration: stale never falls back. */
export function currentNativeRetryInvocation(
  owner: NativeStepOwner,
): NativeRetryInvocation | undefined {
  const actual = current.getStore()
  if (!actual) return undefined
  if (
    !sameOwner(actual.owner, owner) ||
    !actual.accepting ||
    actual.retired ||
    actual.signal.aborted
  ) {
    const error = new Error('Native retry iteration no longer admits authored work')
    nativeRetryInfrastructureFailure(error, actual)
    throw error
  }
  return actual
}
/** Called only at the real NativeBrowser producer, with its actual registration. */
export function observeNativeRetrySource(
  source: object,
  ticket: string | undefined,
  subscribe: (failed: (error: Error) => void) => () => void,
): NativeRetryInvocation | undefined {
  const actual = current.getStore()
  if (!actual) return undefined
  const continuation = operation.getStore()
  if (!(continuation?.live && continuation.invocation === actual))
    currentNativeRetryInvocation(actual.owner)
  if (!ticket || (actual.sourceTicket !== undefined && actual.sourceTicket !== ticket)) {
    const error = new Error('Native callback retry requires one genuinely registered Source')
    nativeRetryInfrastructureFailure(error, actual)
    throw error
  }
  actual.sourceIntent = true
  actual.sourceTicket = ticket
  if (!actual.releases.has(source))
    actual.releases.set(
      source,
      subscribe((error) => nativeRetryInfrastructureFailure(error, actual)),
    )
  return actual
}
/** Only a native iteration-retired ACK may detach genuine producer observers. */
export function retireNativeRetryInvocation(actual: NativeRetryInvocation): void {
  if (actual.operations.size)
    throw new Error('Native retry iteration retains actual SDK operation continuations')
  if (actual.retired) throw new Error('Native retry iteration retirement was replayed')
  actual.accepting = false
  actual.retired = true
  for (const release of actual.releases.values()) release()
  actual.releases.clear()
}

/** Actual built-in operation continuation; never an authored admission waiver. */
export function runNativeRetryOperation<T>(callback: () => T): T {
  if (!nativeBrowserCallbackActive()) return runNativeRetryOperationActual(callback)
  return runNativeBrowserOperation(() => runNativeRetryOperationActual(callback))
}
function runNativeRetryOperationActual<T>(callback: () => T): T {
  const actual = current.getStore()
  if (!actual) return callback()
  const prior = operation.getStore()
  if (prior?.live && prior.invocation === actual) return callback()
  currentNativeRetryInvocation(actual.owner)
  actual.sourceIntent = true
  if (actual.operations.size >= 128) {
    const error = new Error('Native Source operation continuation capacity exceeded')
    nativeRetryInfrastructureFailure(error, actual)
    throw error
  }
  const admitted: Operation = { invocation: actual, live: true, references: 1 }
  actual.operations.add(admitted)
  const finish = (): void => {
    if (!admitted.live) return
    if (--admitted.references) return
    admitted.live = false
    actual.operations.delete(admitted)
    if (!actual.operations.size && !actual.accepting) actual.operationsDrained()
  }
  let result: T
  try {
    result = operation.run(admitted, callback)
  } catch (error) {
    finish()
    throw error
  }
  if (isPromise(result)) {
    try {
      void Reflect.apply(intrinsicThen, result, [finish, finish])
    } catch (error) {
      nativeRetryInfrastructureFailure(error as UntrustedInput, actual)
    }
  } else finish()
  return result
}
/** User callbacks cannot inherit an internal multi-command admission token. */
export function withoutNativeRetryOperation<T>(callback: () => T): T {
  return withoutNativeBrowserOperation(() =>
    operation.getStore() ? cleanupOperation.exit(() => operation.exit(callback)) : callback(),
  )
}
export function nativeRetryRequestInvocation(): NativeRetryInvocation | undefined {
  const actual = current.getStore()
  if (!actual) return undefined
  const admitted = operation.getStore()
  if (admitted?.live && admitted.invocation === actual && !actual.retired) return actual
  return currentNativeRetryInvocation(actual.owner)
}

/** Retain an actual built-in task that can outlive its caller-facing Promise. */
export function retainNativeRetryOperation<T>(task: Promise<T>): void {
  retainNativeBrowserOperation(task)
  const admitted = operation.getStore()
  if (!admitted?.live) return
  ++admitted.references
  const finish = (): void => {
    if (!admitted.live || --admitted.references) return
    admitted.live = false
    const actual = admitted.invocation
    actual.operations.delete(admitted)
    if (!actual.operations.size && !actual.accepting) actual.operationsDrained()
  }
  try {
    void Reflect.apply(intrinsicThen, task, [finish, finish])
  } catch (error) {
    nativeRetryInfrastructureFailure(error as UntrustedInput, admitted.invocation)
  }
}
/** Only existing built-in cleanup sites call this with a live admitted token. */
export function runNativeRetryCleanup<T>(callback: () => T): T {
  return runNativeBrowserCleanup(() => runNativeRetryCleanupActual(callback))
}
function runNativeRetryCleanupActual<T>(callback: () => T): T {
  const admitted = operation.getStore()
  if (!admitted?.live) return callback()
  return cleanupOperation.run(admitted, callback)
}
export function nativeRetryIsCleanup(actual: NativeRetryInvocation): boolean {
  const admitted = operation.getStore()
  return (
    admitted?.live === true &&
    admitted.invocation === actual &&
    cleanupOperation.getStore() === admitted
  )
}
export function nativeRetryOperationSignal(
  signal: AbortSignal | undefined,
  actual: NativeRetryInvocation,
): AbortSignal | undefined {
  if (nativeRetryIsCleanup(actual)) return signal
  return signal ? AbortSignal.any([signal, actual.signal]) : actual.signal
}

/** Bind an existing built-in continuation to its real preadmitted operation. */
export function bindNativeRetryContinuation<A extends readonly UntrustedInput[], R>(
  callback: (...args: A) => R,
): (...args: A) => R {
  return bindNativeBrowserContinuation(bindNativeRetryContinuationActual(callback))
}
function bindNativeRetryContinuationActual<A extends readonly UntrustedInput[], R>(
  callback: (...args: A) => R,
): (...args: A) => R {
  const admitted = operation.getStore()
  if (!admitted?.live) return callback
  return (...args) =>
    current.run(admitted.invocation, () =>
      operation.run(admitted, () => {
        if (!admitted.live || admitted.invocation.retired) {
          const error = new Error('Native operation continuation outlived its admitted ownership')
          nativeRetryInfrastructureFailure(error, admitted.invocation)
          throw error
        }
        const result = callback(...args)
        if (isPromise(result)) retainNativeRetryOperation(result)
        return result
      }),
    )
}
