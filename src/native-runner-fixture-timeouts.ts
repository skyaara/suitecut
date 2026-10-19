// SPDX-License-Identifier: MIT
// Actual fixture author scope and native mutation ACK marshalling only.
import { AsyncLocalStorage } from 'node:async_hooks'
import { Buffer } from 'node:buffer'

import * as z from 'zod'

import { toError, type UntrustedInput } from './untrusted.js'

const Integer = z
  .string()
  .regex(/^[1-9]\d{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const Timeout = z.number().int().min(0).max(120000)
const TimeoutWire = z
  .string()
  .regex(/^(?:0|[1-9]\d{0,5})$/u)
  .transform(Number)
  .pipe(Timeout)
const Owner = z.strictObject({ token: Integer, scope: Integer, generation: Integer })
export type NativeFixtureTimeoutOwner = z.infer<typeof Owner>
export interface NativeFixtureTimeoutInfo {
  readonly timeout: number
  readonly signal: AbortSignal
  readonly setTimeout: (timeout: number) => Promise<void>
}
interface Context {
  readonly owner: NativeFixtureTimeoutOwner
  readonly abort: AbortController
  readonly pending: Set<Promise<void>>
  timeout: number
  phase: 'setup' | 'ready' | 'teardown'
  bound: boolean
  admitting: boolean
  readyClaimed: boolean
  failure?: Error
}
interface Request {
  readonly context: Context
  readonly timeout: number
  readonly resolve: () => void
  readonly reject: (error: Error) => void
}
const same = (left: NativeFixtureTimeoutOwner, right: NativeFixtureTimeoutOwner): boolean =>
  left.token === right.token && left.scope === right.scope && left.generation === right.generation
const decode = (input: string): string => {
  const value = z
    .string()
    .max(32768)
    .regex(/^(?:-|(?:[a-f\d]{4})+)$/u)
    .parse(input)
  return value === '-' ? '' : Buffer.from(value, 'hex').toString('utf16le')
}
/** Private actual-fixture callback ownership and native timeout ACK ledger. */
export class NativeFixtureTimeoutCallbacks {
  private readonly author = new AsyncLocalStorage<Context>()
  private readonly contexts = new Map<string, Context>()
  private readonly requests = new Map<string, Request>()
  private nonce = 0n
  constructor(
    private readonly send: (...fields: (string | number)[]) => Promise<void>,
    private readonly fatal: (error: Error) => void,
    private readonly changed: () => void,
  ) {}
  get physical(): number {
    return this.requests.size
  }
  get contextCount(): number {
    return this.contexts.size
  }
  get idleReady(): boolean {
    return (
      !this.physical &&
      [...this.contexts.values()].every(
        (context) =>
          context.phase === 'ready' &&
          !context.admitting &&
          !context.failure &&
          !context.abort.signal.aborted,
      )
    )
  }
  packet(fields: readonly string[]): boolean {
    const command = fields[0]
    if (
      command !== 'FIXTURE_CLOCK' &&
      command !== 'FIXTURE_TIMEOUT_RESULT' &&
      command !== 'FIXTURE_ABORT'
    )
      return false
    if (command === 'FIXTURE_CLOCK') {
      const [, token, scope, generation, phase, timeout] = z
        .tuple([
          z.literal('FIXTURE_CLOCK'),
          Integer,
          Integer,
          Integer,
          z.enum(['setup', 'teardown']),
          TimeoutWire,
        ])
        .parse(fields)
      const owner = Object.freeze(Owner.parse({ token, scope, generation }))
      const actual = timeout
      const previous = this.contexts.get(token)
      if (phase === 'setup') {
        if (previous || this.contexts.size >= 64)
          throw new Error('Invalid native fixture clock setup sidecar')
        this.contexts.set(token, {
          owner,
          timeout: actual,
          phase,
          bound: false,
          admitting: false,
          readyClaimed: false,
          abort: new AbortController(),
          pending: new Set(),
        })
      } else {
        if (
          !previous ||
          !same(previous.owner, owner) ||
          previous.phase !== 'ready' ||
          !previous.bound ||
          previous.pending.size ||
          previous.failure ||
          previous.abort.signal.aborted
        )
          throw new Error('Stale native fixture teardown clock sidecar')
        previous.phase = phase
        previous.timeout = actual
        previous.admitting = true
      }
    } else if (command === 'FIXTURE_TIMEOUT_RESULT') {
      const [, request, token, scope, generation, ok, value] = z
        .tuple([
          z.literal('FIXTURE_TIMEOUT_RESULT'),
          Integer,
          Integer,
          Integer,
          Integer,
          z.enum(['0', '1']),
          z.string(),
        ])
        .parse(fields)
      const pending = this.requests.get(request)
      if (
        !pending ||
        !same(pending.context.owner, { token, scope, generation }) ||
        this.contexts.get(token) !== pending.context
      )
        throw new Error('Stale native fixture timeout acknowledgment')
      if (ok === '1') {
        const actual = TimeoutWire.parse(value)
        if (actual !== pending.timeout)
          throw new Error('Native fixture timeout ACK differs from admitted request')
        this.requests.delete(request)
        pending.context.timeout = actual
        pending.resolve()
      } else {
        const error = new Error(decode(value))
        this.requests.delete(request)
        pending.context.failure ??= error
        pending.reject(error)
      }
    } else {
      const [, token, scope, generation, reason] = z
        .tuple([z.literal('FIXTURE_ABORT'), Integer, Integer, Integer, z.string()])
        .parse(fields)
      const context = this.contexts.get(token)
      // Local READY publication is not a native admission ACK. The exact
      // callback remains physically live and may receive original-slot/global
      // cancellation before native accepts READY; it must observe that signal.
      if (!context || !same(context.owner, { token, scope, generation }))
        throw new Error('Stale native fixture cancellation')
      context.admitting = false
      if (!context.abort.signal.aborted) context.abort.abort(new Error(decode(reason)))
    }
    this.changed()
    return true
  }
  bind(owner: NativeFixtureTimeoutOwner): NativeFixtureTimeoutInfo {
    const context = this.lookup(owner)
    if (context.bound || context.phase !== 'setup')
      throw new Error('Native fixture timeout context already bound')
    context.bound = true
    context.admitting = true
    return Object.freeze({
      get timeout() {
        return context.timeout
      },
      get signal() {
        return context.abort.signal
      },
      setTimeout: (timeout: number) => this.amend(context, timeout),
    })
  }
  run<T>(owner: NativeFixtureTimeoutOwner, callback: () => T): T {
    const context = this.lookup(owner)
    if (!context.bound || context.phase !== 'setup' || !context.admitting)
      throw new Error('Native fixture author context has no actual setup admission')
    return this.author.run(context, callback)
  }
  amendCurrent(timeout: number): Promise<void> {
    const context = this.author.getStore()
    if (!context) throw new Error('Native fixture timeout requires its actual author context')
    return this.amend(context, timeout)
  }
  private amend(context: Context, input: number): Promise<void> {
    if (
      this.author.getStore() !== context ||
      this.contexts.get(context.owner.token) !== context ||
      !context.admitting ||
      context.phase === 'ready' ||
      context.abort.signal.aborted
    )
      throw new Error('Native fixture timeout context is no longer active')
    const timeout = Timeout.parse(input)
    if (this.requests.size >= 128 || this.nonce === 18446744073709551615n) {
      const error = new Error('Native fixture timeout host request capacity exceeded')
      context.failure ??= error
      this.fatal(error)
      throw error
    }
    const request = String(++this.nonce)
    const promise = new Promise<void>((resolve, reject) => {
      this.requests.set(request, { context, timeout, resolve, reject })
      void this.send(
        'FIXTURE_TIMEOUT',
        request,
        context.owner.token,
        context.owner.scope,
        context.owner.generation,
        timeout,
      ).catch((value: UntrustedInput) => this.fatal(toError(value)))
    })
    // Retain ignored requests before returning to author code. The original
    // promise still rejects; this observer only prevents an unhandled rejection.
    context.pending.add(promise)
    void promise.then(
      () => {
        context.pending.delete(promise)
        this.changed()
      },
      (value: UntrustedInput) => {
        context.failure ??= toError(value)
        context.pending.delete(promise)
        this.changed()
      },
    )
    return promise
  }
  async beforeReady(owner: NativeFixtureTimeoutOwner): Promise<boolean> {
    const context = this.lookup(owner)
    if (
      !context.bound ||
      context.phase !== 'setup' ||
      context.readyClaimed ||
      (!context.admitting && !context.abort.signal.aborted)
    )
      throw new Error('Native fixture readiness lacks original setup ownership')
    context.readyClaimed = true
    context.admitting = false
    await this.drain(context)
    if (context.failure) throw context.failure
    // An exact native cancellation still owns the raw author callback. A late
    // use() may resume its real teardown, but cannot claim native readiness.
    if (context.abort.signal.aborted) return false
    context.phase = 'ready'
    this.changed()
    return true
  }
  async beforeRawSettlement(owner: NativeFixtureTimeoutOwner): Promise<Error | undefined> {
    const context = this.lookup(owner)
    context.admitting = false
    await this.drain(context)
    return context.failure
  }
  closed(owner: NativeFixtureTimeoutOwner): void {
    const context = this.lookup(owner)
    if (
      context.admitting ||
      context.pending.size ||
      [...this.requests.values()].some((request) => request.context === context)
    )
      throw new Error('Native fixture clock closure lacks actual mutation ACKs')
    this.contexts.delete(context.owner.token)
    this.changed()
  }
  private lookup(input: NativeFixtureTimeoutOwner): Context {
    const owner = Owner.parse(input),
      context = this.contexts.get(owner.token)
    if (!context || !same(context.owner, owner))
      throw new Error('Native fixture clock owner is stale or foreign')
    return context
  }
  private async drain(context: Context): Promise<void> {
    while (context.pending.size) await Promise.allSettled([...context.pending])
  }
}
