// SPDX-License-Identifier: MIT
// Private native-admitted browser callback operation continuations. No clocks,
// retries, visibility policy or physical retirement decisions live here.
import { AsyncLocalStorage } from 'node:async_hooks'
import { isPromise } from 'node:util/types'

import { type UntrustedInput } from './untrusted.js'

export interface NativeBrowserCallbackScope {
  accepting: boolean
  retired: boolean
  readonly signal: AbortSignal
  readonly transport: object
  readonly origin: { page: number; target: number; generation: number; token: number }
  readonly operations: Set<Operation>
  begin(): Promise<number>
  end(token: number): Promise<void>
  drained(): void
  failed(error: UntrustedInput): void
}
interface Operation {
  readonly scope: NativeBrowserCallbackScope
  readonly token: Promise<number>
  live: boolean
  references: number
}
const scope = new AsyncLocalStorage<NativeBrowserCallbackScope>()
const operation = new AsyncLocalStorage<Operation>()
const cleanup = new AsyncLocalStorage<Operation>()
const control = new AsyncLocalStorage<boolean>()
// eslint-disable-next-line @typescript-eslint/unbound-method
const intrinsicThen = Promise.prototype.then
export function nativeBrowserCallbackActive(): boolean {
  return scope.getStore() !== undefined && !control.getStore()
}
export function nativeBrowserCallbackScope(): NativeBrowserCallbackScope | undefined {
  return scope.getStore()
}
export function runNativeBrowserCallback<T>(
  actual: NativeBrowserCallbackScope,
  callback: () => T,
): T {
  return scope.run(actual, callback)
}
export function withoutNativeBrowserOperation<T>(callback: () => T): T {
  return operation.getStore() ? cleanup.exit(() => operation.exit(callback)) : callback()
}
export function runNativeBrowserCallbackControl<T>(callback: () => T): T {
  return control.run(true, () => scope.exit(() => operation.exit(callback)))
}
function currentOperation(): Operation | undefined {
  if (control.getStore()) return undefined
  const actual = scope.getStore()
  if (!actual) return undefined
  const admitted = operation.getStore()
  if (admitted?.scope === actual) {
    if (!admitted.live || admitted.references <= 0 || actual.retired)
      throw new Error('Native browser operation continuation is no longer live')
    return admitted
  }
  if (!actual.accepting || actual.retired || actual.signal.aborted)
    throw new Error('Native locator handler no longer admits authored work')
  return undefined
}
export function assertNativeBrowserCallbackAdmission(): void {
  currentOperation()
}
function release(admitted: Operation): void {
  if (!admitted.live || --admitted.references) return
  // A synchronous operation may finish before its actual native Begin ACK.
  // Keep it charged until End can be enqueued before RAW/operations-drained.
  void admitted.token.then(
    (token) => {
      if (!admitted.live) return
      admitted.live = false
      admitted.scope.operations.delete(admitted)
      let ending: Promise<void>
      try {
        ending = runNativeBrowserCallbackControl(() => admitted.scope.end(token))
      } catch (error) {
        admitted.scope.failed(error as UntrustedInput)
        return
      }
      void ending.catch((error) => admitted.scope.failed(error as UntrustedInput))
      if (!admitted.scope.operations.size && !admitted.scope.accepting) admitted.scope.drained()
    },
    (error) => {
      admitted.live = false
      admitted.scope.operations.delete(admitted)
      admitted.scope.failed(error as UntrustedInput)
      if (!admitted.scope.operations.size && !admitted.scope.accepting) admitted.scope.drained()
    },
  )
}
/** Preserve the actual built-in return value/Promise identity. */
export function runNativeBrowserOperation<T>(callback: () => T): T {
  if (control.getStore()) return callback()
  const actual = scope.getStore()
  if (!actual) return callback()
  const prior = currentOperation()
  if (prior) return callback()
  if (actual.operations.size >= 128)
    throw new Error('Native locator handler operation capacity exceeded')
  const token = runNativeBrowserCallbackControl(() => actual.begin())
  const admitted: Operation = { scope: actual, token, live: true, references: 1 }
  actual.operations.add(admitted)
  let result: T
  try {
    result = operation.run(admitted, callback)
  } catch (error) {
    release(admitted)
    throw error
  }
  if (isPromise(result)) {
    try {
      void Reflect.apply(intrinsicThen, result, [() => release(admitted), () => release(admitted)])
    } catch (error) {
      actual.failed(error as UntrustedInput)
    } // Actual Promise remains owned.
  } else release(admitted)
  return result
}
export function retainNativeBrowserOperation<T>(task: Promise<T>): void {
  const admitted = currentOperation()
  if (!admitted) return
  ++admitted.references
  try {
    void Reflect.apply(intrinsicThen, task, [() => release(admitted), () => release(admitted)])
  } catch (error) {
    admitted.scope.failed(error as UntrustedInput)
  }
}
export function bindNativeBrowserContinuation<A extends readonly UntrustedInput[], R>(
  callback: (...args: A) => R,
): (...args: A) => R {
  const admitted = currentOperation()
  if (!admitted) return callback
  return (...args) =>
    scope.run(admitted.scope, () =>
      operation.run(admitted, () => {
        if (!admitted.live || admitted.scope.retired)
          throw new Error('Native browser continuation outlived its admitted operation')
        const result = callback(...args)
        if (isPromise(result)) retainNativeBrowserOperation(result)
        return result
      }),
    )
}
export function runNativeBrowserCleanup<T>(callback: () => T): T {
  const admitted = currentOperation()
  return admitted ? cleanup.run(admitted, callback) : callback()
}
export function nativeBrowserOperationRequest(transport: object):
  | {
      readonly origin: NativeBrowserCallbackScope['origin']
      readonly token: Promise<number>
      readonly signal?: AbortSignal
    }
  | undefined {
  const admitted = currentOperation()
  if (!admitted) return undefined
  if (admitted.scope.transport !== transport)
    throw new Error('Native locator handler cannot borrow another browser process')
  if (cleanup.getStore() === admitted) return undefined
  return { origin: admitted.scope.origin, token: admitted.token, signal: admitted.scope.signal }
}
