// SPDX-License-Identifier: MIT
// Actual function invocation and receipts only; native code schedules all runs.
import { AsyncLocalStorage } from 'node:async_hooks'
import { isPromise } from 'node:util/types'

import * as z from 'zod'

import { type NativeRetryCallback, type NativeToPassOptions } from './native-callback-assertions.js'
import {
  retireNativeRetryInvocation,
  runNativeRetryInvocation,
  type NativeRetryInvocation,
} from './native-callback-operations.js'
import { type NativeStepOwner } from './native-runner-steps.js'
import { runNativeMatcherUserCallback } from './native-soft-assertions.js'
import { type NativeTestAnnotation } from './native-suite.js'
import { toError, type UntrustedInput } from './untrusted.js'

const UInt = z
  .string()
  .regex(/^[1-9]\d{0,19}$/u)
  .refine((v) => BigInt(v) <= 18446744073709551615n)
const Token = z.union([z.literal('0'), UInt])
const Owner = z.strictObject({ token: Token, scope: UInt, generation: UInt })
const locationSchema = z.strictObject({
  file: z.string().min(1).max(4096),
  line: z.number().int().positive(),
  column: z.number().int().positive(),
})
const encode = (value: string) => Buffer.from(value, 'utf16le').toString('hex') || '-'
const decode = (wire: string) => {
  const text = z
    .string()
    .max(32768)
    .regex(/^(?:-|(?:[a-f\d]{4})+)$/u)
    .parse(wire)
  return text === '-' ? '' : Buffer.from(text, 'hex').toString('utf16le')
}
const key = (owner: NativeStepOwner) => `${owner.token}:${owner.scope}:${owner.generation}`
const IntrinsicPromise = Promise
// eslint-disable-next-line @typescript-eslint/unbound-method
const intrinsicThen = Promise.prototype.then
interface Context {
  readonly owner: NativeStepOwner
  readonly test: string
  readonly attempt: string
}
interface Iteration {
  readonly actual: NativeRetryInvocation
  rawReturned: boolean
  failure?: { readonly nonce: string; readonly error: UntrustedInput }
}
interface Pending {
  readonly owner: NativeStepOwner
  readonly context: Context
  readonly callback: NativeRetryCallback
  readonly resolve: () => void
  readonly reject: (error: UntrustedInput) => void
  id?: string
  iteration?: Iteration
  lastNonce: bigint
  result: boolean
  lastFailed?: { readonly nonce: string; readonly error: UntrustedInput }
  retiredFailure?: { readonly nonce: string; readonly error: UntrustedInput }
  sourceIntent?: boolean
  settlement?: () => void
  infrastructure?: { readonly nonce: string; readonly error: UntrustedInput }
}
export interface NativeCallbackRetrySteps {
  current(owner: NativeStepOwner): string
  runAdmitted<T>(
    owner: NativeStepOwner,
    id: string,
    parent: string,
    active: () => boolean,
    callback: (signal: AbortSignal) => T,
  ): T
  abortAdmitted(owner: NativeStepOwner, id: string, error: Error): void
  closeAdmitted(owner: NativeStepOwner, id: string): void
  retireAdmitted(owner: NativeStepOwner, id: string): void
  originating(owner: NativeStepOwner, error: UntrustedInput): string | undefined
}
export class NativeCallbackRetryCallbacks {
  private readonly author = new AsyncLocalStorage<{
    readonly owner: NativeStepOwner
    readonly active: () => boolean
  }>()
  private readonly contexts = new Map<string, Context>()
  private readonly requests = new Map<string, Pending>()
  private readonly retiredContexts = new Set<string>()
  private readonly retirements = new Map<string, () => void>()
  private nonce = 0n
  constructor(
    private readonly send: (...fields: (string | number)[]) => Promise<void>,
    private readonly fatal: (error: Error) => void,
    private readonly changed: () => void,
    private readonly describe: (error: UntrustedInput) => string,
    private readonly steps: NativeCallbackRetrySteps,
    private readonly identity: { readonly workerIndex: number; readonly pid: number },
    private readonly acceptedControl?: (
      owner: NativeStepOwner,
    ) => Promise<UntrustedInput | undefined>,
  ) {}
  get pending(): number {
    return this.requests.size
  }
  get idleReady(): boolean {
    return (
      !this.requests.size &&
      !this.contexts.size &&
      !this.retirements.size &&
      !this.retiredContexts.size
    )
  }
  run<T>(owner: NativeStepOwner, active: () => boolean, callback: () => T): T {
    return this.author.run({ owner: Owner.parse(owner), active }, callback)
  }
  begin(
    callback: NativeRetryCallback,
    options: NativeToPassOptions,
    location?: NativeTestAnnotation['location'],
  ): Promise<void> {
    const author = this.author.getStore(),
      owner = author?.owner,
      context = owner && this.contexts.get(key(owner))
    if (!author?.active() || !owner || !context)
      throw new Error('Native toPass requires an active body/beforeEach/afterEach callback')
    const step = Token.parse(this.steps.current(owner)),
      actualLocation = locationSchema.optional().parse(location)
    if (this.requests.size >= 128 || this.nonce === 18446744073709551615n) {
      const error = new Error('Native step admission request capacity exceeded')
      this.fatal(error)
      throw error
    }
    const request = String(this.nonce + 1n)
    const fields = [
      'TOPASS_BEGIN',
      request,
      owner.token,
      owner.scope,
      owner.generation,
      step,
      options.timeout === undefined ? '-' : String(options.timeout),
      options.intervals?.join(',') ?? '-',
      actualLocation ? encode(actualLocation.file) : '-',
      actualLocation?.line ?? 0,
      actualLocation?.column ?? 0,
      actualLocation ? 1 : 0,
    ]
    if (Buffer.byteLength(fields.join(' ')) > 65536)
      throw new Error('Native toPass metadata exceeds original control budget')
    ++this.nonce
    return new IntrinsicPromise<void>((resolve, reject) => {
      this.requests.set(request, {
        owner,
        context,
        callback,
        resolve,
        reject,
        lastNonce: 0n,
        result: false,
      })
      void this.send(...fields).catch((error: UntrustedInput) => this.fatal(toError(error)))
    })
  }
  retireCallback(owner: NativeStepOwner, release: () => void): void {
    const identity = key(owner)
    if (!this.contexts.has(identity)) {
      this.retiredContexts.delete(identity)
      release()
      return
    }
    if (this.retirements.has(identity))
      throw new Error('Native toPass callback retirement replayed')
    this.retirements.set(identity, release)
  }
  packet(fields: readonly string[]): boolean {
    if (fields[0] === 'TOPASS_CONTEXT') {
      const [token, scope, generation, test, attempt, worker, pid, kind] = z
        .tuple([
          Token,
          UInt,
          UInt,
          UInt,
          UInt,
          z.coerce.number().int().positive(),
          z.coerce.number().int().positive(),
          z.enum(['body', 'beforeEach', 'afterEach']),
        ])
        .parse(fields.slice(1))
      const owner = Object.freeze({ token, scope, generation })
      if (
        worker !== this.identity.workerIndex ||
        pid !== this.identity.pid ||
        this.contexts.has(key(owner)) ||
        this.retiredContexts.has(key(owner)) ||
        (kind !== 'body' && token === '0')
      )
        throw new Error('Native toPass context has a foreign actual owner')
      this.contexts.set(key(owner), { owner, test, attempt })
      return true
    }
    if (fields[0] === 'TOPASS_CONTEXT_RETIRED') {
      const [token, scope, generation] = z.tuple([Token, UInt, UInt]).parse(fields.slice(1)),
        owner = { token, scope, generation },
        identity = key(owner)
      const release = this.retirements.get(identity)
      if (
        !this.contexts.has(identity) ||
        [...this.requests.values()].some((p) => key(p.owner) === identity)
      )
        throw new Error('Native toPass callback retirement lacks physical receipts')
      this.contexts.delete(identity)
      if (release) {
        this.retirements.delete(identity)
        release()
      } else this.retiredContexts.add(identity)
      this.changed()
      return true
    }
    if (!fields[0]?.startsWith('TOPASS_')) return false
    const request = UInt.parse(fields[1]),
      pending = this.requests.get(request)
    if (
      !pending ||
      key(pending.owner) !==
        key(Owner.parse({ token: fields[2], scope: fields[3], generation: fields[4] }))
    )
      throw new Error('Native toPass receipt has a foreign request/owner')
    if (fields[0] === 'TOPASS_ADMITTED') {
      const [id, ok, error] = z
        .tuple([Token, z.enum(['0', '1']), z.string()])
        .parse(fields.slice(5))
      if (pending.id || (ok === '1') !== (id !== '0'))
        throw new Error('Native toPass admission replayed or malformed')
      if (ok === '1') {
        if (error !== '-') throw new Error('Native toPass success admission has an error')
        pending.id = id
      } else {
        this.requests.delete(request)
        pending.reject(new Error(decode(error)))
      }
    } else {
      const id = UInt.parse(fields[5])
      if (pending.id !== id)
        throw new Error('Native toPass receipt has a foreign native assertion ID')
      if (fields[0] === 'TOPASS_RUN') {
        const [nonce, step, parent, remaining] = z
          .tuple([UInt, UInt, Token, z.coerce.number().int().min(0).max(Number.MAX_SAFE_INTEGER)])
          .parse(fields.slice(6))
        if (pending.iteration || pending.result || BigInt(nonce) <= pending.lastNonce)
          throw new Error('Native toPass run overlaps or replays an actual invocation')
        if (pending.retiredFailure) {
          pending.lastFailed = pending.retiredFailure
          delete pending.retiredFailure
        }
        pending.lastNonce = BigInt(nonce)
        this.invoke(request, pending, nonce, step, parent, remaining)
      } else if (fields[0] === 'TOPASS_ITERATION_RETIRED') {
        const [nonce, step] = z.tuple([UInt, UInt]).parse(fields.slice(6)),
          iteration = pending.iteration
        if (
          !iteration?.rawReturned ||
          iteration.actual.invocationNonce !== nonce ||
          iteration.actual.step !== step
        )
          throw new Error('Native toPass retirement lacks actual raw callback receipt')
        pending.sourceIntent ||= iteration.actual.sourceIntent === true
        if (iteration.failure) pending.retiredFailure = iteration.failure
        this.steps.retireAdmitted(pending.owner, step)
        retireNativeRetryInvocation(iteration.actual)
        delete pending.iteration
      } else if (fields[0] === 'TOPASS_RESULT') {
        const [status, error, lastInvocation] = z
          .tuple([z.enum(['passed', 'timedOut', 'canceled', 'infrastructure']), z.string(), Token])
          .parse(fields.slice(6))
        if (pending.result) throw new Error('Native toPass logical result was replayed')
        pending.result = true
        if (status !== 'passed' && pending.iteration) {
          pending.iteration.actual.accepting = false
          this.steps.abortAdmitted(
            pending.owner,
            pending.iteration.actual.step,
            new Error(decode(error)),
          )
        }
        const sourceIntent =
          pending.sourceIntent === true || pending.iteration?.actual.sourceIntent === true
        if (status === 'passed') {
          if (error !== '-') throw new Error('Native toPass pass has an error')
          if (sourceIntent) pending.settlement = pending.resolve
          else pending.resolve()
        } else {
          const last = [
            pending.iteration?.failure,
            pending.retiredFailure,
            pending.lastFailed,
          ].find((value) => value?.nonce === lastInvocation)
          // Native identifies which genuine author receipt caused exhaustion.
          // A native hard/canceled outcome never becomes a retryable mismatch.
          const failure =
            status === 'timedOut' && last
              ? last.error
              : status === 'infrastructure' && pending.infrastructure?.nonce === lastInvocation
                ? pending.infrastructure.error
                : new Error(decode(error))
          const settle = (): void => {
            if (status === 'canceled' && this.acceptedControl)
              void this.acceptedControl(pending.owner).then((control) => {
                pending.reject(control === undefined ? failure : control)
              }, pending.reject)
            else pending.reject(failure)
          }
          if (sourceIntent) pending.settlement = settle
          else settle()
        }
      } else if (fields[0] === 'TOPASS_RETIRED') {
        z.tuple([]).parse(fields.slice(6))
        if (!pending.result || pending.iteration)
          throw new Error('Native toPass final retirement is premature')
        this.requests.delete(request)
        pending.settlement?.()
      } else throw new Error('Unknown native toPass receipt')
    }
    this.changed()
    return true
  }
  private invoke(
    request: string,
    pending: Pending,
    nonce: string,
    step: string,
    parent: string,
    remaining: number,
  ): void {
    const id = pending.id
    if (!id) throw new Error('Native toPass run lacks native admission')
    this.steps.runAdmitted(
      pending.owner,
      step,
      parent,
      () => pending.iteration?.actual.accepting === true,
      (signal) => {
        const actual: NativeRetryInvocation = {
          owner: pending.owner,
          assertionID: id,
          invocationNonce: nonce,
          step,
          signal,
          timeoutMs: remaining,
          accepting: true,
          retired: false,
          releases: new Map(),
          operations: new Set(),
          operationsDrained: () => {
            if (!iteration.rawReturned) return
            void this.send(
              'TOPASS_OPERATIONS_RETIRED',
              request,
              pending.owner.token,
              pending.owner.scope,
              pending.owner.generation,
              id,
              nonce,
              step,
              actual.sourceTicket ?? '0',
            ).catch((error: UntrustedInput) => this.fatal(toError(error)))
          },
          producerFailure: (error) => {
            pending.infrastructure ??= { nonce, error }
            const message = runNativeRetryInvocation(actual, () =>
              runNativeMatcherUserCallback(() => this.describe(error)),
            )
            if (message.length > 8192) {
              this.fatal(new Error('Native toPass producer error exceeds original scalar budget'))
              return
            }
            void this.send(
              'TOPASS_PRODUCER_FAILURE',
              request,
              pending.owner.token,
              pending.owner.scope,
              pending.owner.generation,
              id,
              nonce,
              step,
              actual.sourceTicket ?? '0',
              encode(message),
            ).catch((failure: UntrustedInput) => this.fatal(toError(failure)))
          },
        }
        const iteration: Iteration = { actual, rawReturned: false }
        pending.iteration = iteration
        let raw: UntrustedInput | void
        try {
          raw = runNativeRetryInvocation(actual, () =>
            runNativeMatcherUserCallback(pending.callback),
          )
        } catch (error) {
          this.raw(request, pending, iteration, false, error as UntrustedInput)
          return
        }
        // A genuine Promise's own then cannot invent an earlier physical return.
        let promise: Promise<UntrustedInput>
        if (isPromise(raw)) {
          promise = new IntrinsicPromise<UntrustedInput>((resolve, reject) => {
            try {
              runNativeRetryInvocation(actual, () =>
                runNativeMatcherUserCallback(() => {
                  void Reflect.apply(intrinsicThen, raw, [resolve, reject])
                }),
              )
            } catch (error) {
              this.fatal(toError(error as UntrustedInput))
            } // Keep unobservable genuine work charged.
          })
        } else
          promise = runNativeRetryInvocation(actual, () =>
            IntrinsicPromise.resolve(raw as UntrustedInput),
          )
        void promise
          .then(
            () => this.raw(request, pending, iteration, true, undefined),
            (error: UntrustedInput) => this.raw(request, pending, iteration, false, error),
          )
          .catch((error: UntrustedInput) => this.fatal(toError(error)))
      },
    )
  }
  private raw(
    request: string,
    pending: Pending,
    iteration: Iteration,
    ok: boolean,
    error: UntrustedInput,
  ): void {
    if (pending.iteration !== iteration || iteration.rawReturned)
      throw new Error('Native toPass raw callback receipt was replayed')
    const actual = iteration.actual
    actual.accepting = false
    iteration.rawReturned = true
    this.steps.closeAdmitted(pending.owner, actual.step)
    if (!ok && !actual.infrastructure) iteration.failure = { nonce: actual.invocationNonce, error }
    const failure = actual.infrastructure ? actual.infrastructure.error : error
    const message =
      ok && !actual.infrastructure
        ? ''
        : runNativeRetryInvocation(actual, () =>
            runNativeMatcherUserCallback(() => this.describe(failure)),
          )
    if (message.length > 8192) {
      this.fatal(new Error('Native toPass raw error exceeds original scalar budget'))
      return
    }
    const source = !ok ? (this.steps.originating(pending.owner, error) ?? '0') : '0'
    void this.send(
      'TOPASS_RAW',
      request,
      pending.owner.token,
      pending.owner.scope,
      pending.owner.generation,
      pending.id ?? '0',
      actual.invocationNonce,
      actual.step,
      actual.infrastructure ? 'I' : ok ? 'P' : 'E',
      encode(message),
      source,
      actual.sourceTicket ?? '0',
      actual.operations.size,
    ).catch((failure: UntrustedInput) => this.fatal(toError(failure)))
  }
}
