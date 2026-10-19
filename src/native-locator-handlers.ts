// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'

import * as z from 'zod'

import {
  nativeBrowserCallbackScope,
  runNativeBrowserCallback,
  runNativeBrowserCallbackControl,
  type NativeBrowserCallbackScope,
} from './native-browser-operations.js'
import { type NativeBrowser } from './native-browser.js'
import {
  bindNativeRetryContinuation,
  retainNativeRetryOperation,
  runNativeRetryCleanup,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import { type NativeElementHandle, type NativeLocator } from './native-page.js'
import { type NativeLocatorHandlerStage } from './native-protocol.js'
import {
  runNativeMatcherUserCallback,
  runNativeMatcherUserPromise,
} from './native-soft-assertions.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeLocatorHandlerOptions {
  times?: number
  noWaitAfter?: boolean
}
type HandlerTarget = NativeLocator | NativeElementHandle
export type NativeLocatorHandler<T extends HandlerTarget = NativeLocator> = (
  locator: T,
) => void | Promise<void>
const optionsSchema = z.strictObject({
  times: z.number().int().positive().optional(),
  noWaitAfter: z.boolean().optional(),
})
interface Registration {
  readonly locator: HandlerTarget
  readonly callback: () => void | Promise<void>
  readonly clientToken: string
  readonly admitted: Promise<void>
  id?: number
  removing?: Promise<void>
}
interface Invocation {
  readonly event: NativeLocatorHandlerStage
  readonly registration: Registration
  readonly scope: NativeBrowserCallbackScope
  readonly abort: AbortController
  raw: boolean
  operationsReceipt: boolean
  cancelSent: boolean
  failure?: UntrustedInput
}
function describe(error: UntrustedInput): string {
  return runNativeMatcherUserCallback(() => {
    // Authored coercion is intentionally observed with its private waiver suspended.
    try {
      // eslint-disable-next-line @typescript-eslint/no-base-to-string -- Preserve authored error coercion within the callback boundary.
      return (error instanceof Error ? error.message : String(error)).slice(0, 4096)
    } catch {
      return 'Native locator handler callback failed'
    }
  })
}
/** Thin callback registry; native owns invocation, trigger policy and retirement. */
export class NativeLocatorHandlers {
  private readonly registrations = new Map<HandlerTarget, Registration>()
  private readonly registrationsByToken = new Map<string, Registration>()
  private readonly invocations = new Map<number, Invocation>()
  private detach: (() => void) | undefined
  private readonly subscribe: (listener: (event: NativeLocatorHandlerStage) => void) => () => void
  private sourceClosed = false
  constructor(private readonly source: NativeBrowser) {
    if (!source.processClosed)
      throw new Error('Native locator handler process lifetime is unavailable')
    if (!source.onLocatorHandlerStage)
      throw new Error('Native locator handler transport is unavailable')
    const subscribe = source.onLocatorHandlerStage.bind(source)
    this.subscribe = (listener) => subscribe(listener)
    const closed = (): void => {
      for (const active of this.invocations.values()) {
        active.scope.accepting = false
        active.abort.abort(new Error('Native locator handler page closed'))
      }
      // Actual process death is the physical domain receipt, not page closure.
      if (!this.invocations.size) {
        this.clearRegistrations()
        this.releaseIfIdle()
      }
    }
    const pageSignal = source.pageSignal ?? source.signal
    pageSignal.addEventListener('abort', closed, { once: true })
    if (pageSignal.aborted) closed()
    const weak = new WeakRef(this)
    void source.processClosed.then(() => weak.deref()?.physicallyClosed())
  }
  private clearRegistrations(): void {
    this.registrations.clear()
    this.registrationsByToken.clear()
  }
  private forgetRegistration(registration: Registration): void {
    if (this.registrations.get(registration.locator) === registration)
      this.registrations.delete(registration.locator)
    if (this.registrationsByToken.get(registration.clientToken) === registration)
      this.registrationsByToken.delete(registration.clientToken)
  }
  private releaseIfIdle(): void {
    if (this.registrations.size || this.invocations.size) return
    this.detach?.()
    this.detach = undefined
  }
  private physicallyClosed(): void {
    this.sourceClosed = true
    for (const active of this.invocations.values()) {
      active.scope.accepting = false
      active.abort.abort(new Error('Actual native browser process closed'))
      this.finishClosed(active)
    }
    this.clearRegistrations()
    this.releaseIfIdle()
  }
  private finishClosed(active: Invocation): void {
    // Source child death does not settle a callback executing in this Node.
    if (!this.sourceClosed || !active.raw || active.scope.operations.size) return
    active.scope.retired = true
    this.invocations.delete(active.event.token)
    this.releaseIfIdle()
  }
  async add<T extends HandlerTarget>(
    locator: T,
    target: Record<string, UntrustedInput>,
    callback: NativeLocatorHandler<T>,
    options: NativeLocatorHandlerOptions,
  ): Promise<void> {
    const config = withoutNativeRetryOperation(() => optionsSchema.parse(options))
    if (typeof callback !== 'function')
      throw new TypeError('Native locator handler must be a function')
    if (this.registrations.has(locator))
      throw new Error('Native locator already has a registered handler; remove it first')
    this.detach ??= this.subscribe((event) => this.receive(event))
    let accept!: () => void
    let reject!: (error: UntrustedInput) => void
    const admitted = new Promise<void>((resolve, fail) => {
      accept = resolve
      reject = fail
    })
    void admitted.catch(() => undefined)
    const registration: Registration = {
      locator,
      callback: () => callback(locator),
      clientToken: randomUUID(),
      admitted,
    }
    this.registrations.set(locator, registration)
    this.registrationsByToken.set(registration.clientToken, registration)
    try {
      const reply = await this.source.runAutomation('locatorHandlerRegister', {
        clientToken: registration.clientToken,
        target,
        ...config,
      })
      registration.id = z
        .strictObject({ handlerId: z.number().int().positive() })
        .parse(reply).handlerId
      accept()
    } catch (error) {
      reject(error as UntrustedInput)
      this.forgetRegistration(registration)
      this.releaseIfIdle()
      throw error
    }
  }
  remove(locator: HandlerTarget): Promise<void> {
    const registration = this.registrations.get(locator)
    if (!registration) return Promise.resolve()
    if (registration.id === undefined)
      return Promise.reject(new Error('Native locator handler registration is still pending'))
    const current = nativeBrowserCallbackScope()
    const invocation =
      current === undefined ? undefined : this.invocations.get(current.origin.token)
    const active =
      invocation?.registration === registration && invocation.scope === current
        ? invocation
        : undefined
    if (!active && registration.removing) return registration.removing
    const removal = this.source
      .runAutomation('locatorHandlerRemove', {
        handlerId: registration.id,
        clientToken: registration.clientToken,
        ...(active === undefined ? {} : { invocationToken: active.event.token }),
      })
      .then(
        () => {
          this.forgetRegistration(registration)
          this.releaseIfIdle()
        },
        (error) => {
          if (!active) delete registration.removing
          throw error
        },
      )
    if (!active) registration.removing = removal
    return removal
  }
  private receive(event: NativeLocatorHandlerStage): void {
    const registration = this.registrationsByToken.get(event.clientToken)
    if (!registration || (registration.id !== undefined && registration.id !== event.handlerId))
      throw new Error('Native locator handler event has no exact registration')
    const prior = this.invocations.get(event.token)
    if (event.stage !== 'locatorHandlerRun') {
      if (
        prior?.event.id !== event.id ||
        prior.event.generation !== event.generation ||
        prior.registration !== registration
      )
        throw new Error('Native locator handler lifecycle identity mismatch')
      prior.scope.accepting = false
      if (event.stage === 'locatorHandlerAbort')
        prior.abort.abort(new Error('Native locator handler owner was canceled'))
      else {
        if (!prior.raw || prior.scope.operations.size)
          throw new Error('Native locator handler retired before actual callback/operations')
        prior.scope.retired = true
        this.invocations.delete(event.token)
        if (event.removed) this.forgetRegistration(registration)
        this.releaseIfIdle()
        if (this.source.pageSignal?.aborted && !this.invocations.size) {
          this.clearRegistrations()
          this.releaseIfIdle()
        }
      }
      return
    }
    if (prior || this.invocations.size >= 64)
      throw new Error('Native locator handler overlapping/replayed callback')
    const send = bindNativeRetryContinuation(
      (
        command: Parameters<NativeBrowser['runAutomation']>[0],
        fields: Record<string, UntrustedInput>,
      ) =>
        runNativeBrowserCallbackControl(() =>
          runNativeRetryCleanup(() => this.source.runAutomation(command, fields)),
        ),
    )
    const identity = {
      target: event.id,
      generation: event.generation,
      token: event.token,
      handlerId: event.handlerId,
      clientToken: event.clientToken,
    }
    const abort = new AbortController()
    const failed = (error: UntrustedInput): void => {
      if (actual.scope.retired) return
      actual.failure = error
      if (this.sourceClosed) {
        this.finishClosed(actual)
        return
      }
      actual.scope.accepting = false
      abort.abort(error)
      if (!actual.cancelSent) {
        actual.cancelSent = true
        void send('cancel', { target: event.id }).catch(() => undefined)
      }
    }
    const drained = (): void => {
      if (this.sourceClosed) {
        this.finishClosed(actual)
        return
      }
      if (
        !actual.raw ||
        actual.scope.operations.size ||
        actual.operationsReceipt ||
        actual.scope.retired
      )
        return
      actual.operationsReceipt = true
      void send('locatorHandlerOperationsRetired', identity).catch((error) =>
        failed(error as UntrustedInput),
      )
    }
    const scope: NativeBrowserCallbackScope = {
      accepting: true,
      retired: false,
      signal: abort.signal,
      transport: this.source.processSignal ?? this.source.signal,
      origin: {
        page: this.source.pageId ?? 1,
        target: event.id,
        generation: event.generation,
        token: event.token,
      },
      operations: new Set(),
      begin: async () =>
        z
          .strictObject({ operationToken: z.number().int().positive() })
          .parse(await send('locatorHandlerOperationBegin', identity)).operationToken,
      end: async (operationToken) => {
        await send('locatorHandlerOperationEnd', { ...identity, operationToken })
      },
      drained,
      failed,
    }
    const actual: Invocation = {
      event,
      registration,
      scope,
      abort,
      raw: false,
      operationsReceipt: false,
      cancelSent: false,
    }
    this.invocations.set(event.token, actual)
    // The triggering real command's captured ALS is active here. User callback
    // invocation AND return assimilation suspend private continuation waivers.
    const raw = runNativeBrowserCallback(scope, () =>
      runNativeMatcherUserPromise(async () => {
        await registration.admitted
        if (registration.id !== event.handlerId)
          throw new Error('Native locator handler registration ACK mismatch')
        return await runNativeMatcherUserPromise(() => registration.callback())
      }),
    )
    retainNativeRetryOperation(raw)
    const settled = (ok: boolean, error?: UntrustedInput): void => {
      actual.raw = true
      scope.accepting = false
      if (this.sourceClosed) {
        this.finishClosed(actual)
        return
      }
      const pending = scope.operations.size > 0
      actual.operationsReceipt = !pending
      void send('locatorHandlerRaw', {
        ...identity,
        ok,
        ...(ok
          ? {}
          : { errorJSON: runNativeMatcherUserCallback(() => JSON.stringify(describe(error))) }),
        operationsPending: pending,
      }).catch((failure) => failed(failure as UntrustedInput))
    }
    void raw.then(
      () => runNativeBrowserCallback(scope, () => settled(true)),
      (error) => runNativeBrowserCallback(scope, () => settled(false, error as UntrustedInput)),
    )
  }
}
