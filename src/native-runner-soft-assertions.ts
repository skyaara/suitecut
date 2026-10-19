// SPDX-License-Identifier: MIT
// Author scope and native ACK marshaling only. No host verdict, clock or retry.
import { AsyncLocalStorage } from 'node:async_hooks'
import { Buffer } from 'node:buffer'

import * as z from 'zod'

import {
  getNativeAssertionDeadlineFailure,
  getNativeMatcherFailure,
  getNativeMatcherFailureKind,
} from './native-matcher-failure.js'
import { type NativeStepOwner } from './native-runner-steps.js'
import {
  type NativeSoftAssertionLease,
  type NativeSoftAssertionSettlement,
} from './native-soft-assertions.js'
import { type NativeTestAnnotation } from './native-suite.js'
import { toError, type UntrustedInput } from './untrusted.js'

const Integer = z
  .string()
  .regex(/^[1-9]\d{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const Token = z.union([z.literal('0'), Integer])
const Owner = z.strictObject({ token: Token, scope: Integer, generation: Integer })
const Location = z.strictObject({
  file: z.string().min(1).max(4096),
  line: z.number().int().min(1).max(2147483647),
  column: z.number().int().min(1).max(2147483647),
})
const encode = (value: string): string => Buffer.from(value, 'utf16le').toString('hex') || '-'
const decode = (value: string): string => {
  const actual = z
    .string()
    .max(32768)
    .regex(/^(?:-|(?:[a-f\d]{4})+)$/u)
    .parse(value)
  return actual === '-' ? '' : Buffer.from(actual, 'hex').toString('utf16le')
}
const key = (owner: NativeStepOwner): string => `${owner.token}:${owner.scope}:${owner.generation}`
interface Context {
  readonly owner: NativeStepOwner
  readonly attempt: string
}
interface Pending {
  readonly owner: NativeStepOwner
  readonly context: Context
  readonly step: string
  readonly abort: AbortController
  readonly resolve: (lease: NativeSoftAssertionLease) => void
  readonly reject: (error: Error) => void
  id?: string
  settled: boolean
  error?: Error
  settlement?: { readonly resolve: () => void; readonly reject: (error: Error) => void }
}
export class NativeSoftAssertionCallbacks {
  private readonly author = new AsyncLocalStorage<{
    readonly owner: NativeStepOwner
    readonly active: () => boolean
  }>()
  private readonly contexts = new Map<string, Context>()
  private readonly attempts = new Map<string, Error[]>()
  private readonly pending = new Map<string, Pending>()
  private readonly retirements = new Map<string, () => void>()
  private nonce = 0n
  constructor(
    private readonly send: (...fields: (string | number)[]) => Promise<void>,
    private readonly fatal: (error: Error) => void,
    private readonly changed: () => void,
    private readonly describe: (error: UntrustedInput) => string,
    private readonly currentStep: (owner: NativeStepOwner) => string,
    private readonly identity: { readonly workerIndex: number; readonly pid: number },
  ) {}
  get physical(): number {
    return this.pending.size
  }
  get contextCount(): number {
    return this.contexts.size
  }
  get retirementPending(): number {
    return this.retirements.size
  }
  get idleReady(): boolean {
    return !this.physical && !this.contextCount && !this.retirementPending
  }
  run<T>(owner: NativeStepOwner, active: () => boolean, callback: () => T): T {
    // Unsupported all-hook callbacks still execute normally. Actual begin/read
    // requires a native-provided supported context; no context is fabricated.
    return this.author.run({ owner: Object.freeze(Owner.parse(owner)), active }, callback)
  }
  errors(owner: NativeStepOwner): readonly Error[] {
    const context = this.contexts.get(key(Owner.parse(owner)))
    if (!context) throw new Error('Native soft errors require a supported live callback context')
    const errors = this.attempts.get(context.attempt)
    if (!errors) throw new Error('Native soft errors lost the actual attempt owner')
    return Object.freeze([...errors])
  }
  begin(location?: NativeTestAnnotation['location']): Promise<NativeSoftAssertionLease> {
    const author = this.author.getStore()
    if (!author?.active())
      throw new Error('Native soft assertions require an active supported callback')
    const owner = author.owner,
      context = this.contexts.get(key(owner))
    if (!context)
      throw new Error('Native soft assertions support actual body/beforeEach/afterEach only')
    const step = z.union([z.literal('0'), Integer]).parse(this.currentStep(owner))
    const actualLocation = Location.optional().parse(location)
    if (this.pending.size >= 64 || this.nonce === 18446744073709551615n) {
      const failure = new Error('Native soft host request capacity exceeded')
      this.fatal(failure)
      throw failure
    }
    const request = String(++this.nonce)
    return new Promise<NativeSoftAssertionLease>((resolve, reject) => {
      this.pending.set(request, {
        owner,
        context,
        step,
        abort: new AbortController(),
        resolve,
        reject,
        settled: false,
      })
      void this.send(
        'SOFT_BEGIN',
        request,
        owner.token,
        owner.scope,
        owner.generation,
        step,
        actualLocation ? encode(actualLocation.file) : '-',
        actualLocation?.line ?? 0,
        actualLocation?.column ?? 0,
        actualLocation ? 1 : 0,
      ).catch((error: UntrustedInput) => this.fatal(toError(error)))
    })
  }
  private settle(
    request: string,
    pending: Pending,
    value: NativeSoftAssertionSettlement,
  ): Promise<void> {
    if (this.pending.get(request) !== pending || !pending.id || pending.settled)
      throw new Error('Native soft settlement is stale, foreign or duplicate')
    let recognized: Error | undefined
    if (value.kind === 'assertionDeadline')
      recognized = getNativeAssertionDeadlineFailure(value.receipt)
    else if (value.kind === 'softFailure') {
      if (getNativeMatcherFailureKind(value.receipt) !== 'mismatch')
        throw new TypeError('Native mismatch settlement has an assertion-deadline receipt')
      recognized = getNativeMatcherFailure(value.receipt)
    }
    const error = recognized ?? (value.kind === 'otherFailure' ? value.error : undefined)
    const text = value.kind === 'passed' ? '' : this.describe(error)
    const actual = z.string().max(8192).parse(text)
    pending.settled = true
    if (recognized) pending.error = recognized
    return new Promise<void>((resolve, reject) => {
      pending.settlement = { resolve, reject }
      void this.send(
        'SOFT_SETTLED',
        request,
        pending.owner.token,
        pending.owner.scope,
        pending.owner.generation,
        pending.id ?? '0',
        value.kind === 'passed'
          ? 'P'
          : value.kind === 'softFailure'
            ? 'S'
            : value.kind === 'assertionDeadline'
              ? 'D'
              : 'E',
        encode(actual),
      ).catch((failure: UntrustedInput) => this.fatal(toError(failure)))
    })
  }
  retireCallback(owner: NativeStepOwner, release: () => void): void {
    const identity = key(Owner.parse(owner))
    if (this.retirements.has(identity) || this.retirements.size >= 64)
      throw new Error('Native soft callback retirement is duplicate or exceeds capacity')
    if (this.contexts.has(identity)) this.retirements.set(identity, release)
    else release()
  }
  packet(fields: readonly string[]): boolean {
    if (fields[0] === 'SOFT_CONTEXT') {
      const [token, scope, generation, test, attempt, worker, pid, kind] = z
        .tuple([
          Token,
          Integer,
          Integer,
          Integer,
          Integer,
          z.coerce.number().int().positive(),
          z.coerce.number().int().positive(),
          z.enum(['body', 'beforeEach', 'afterEach']),
        ])
        .parse(fields.slice(1))
      if (
        worker !== this.identity.workerIndex ||
        pid !== this.identity.pid ||
        this.contexts.size >= 64 ||
        this.contexts.has(key({ token, scope, generation }))
      )
        throw new Error('Native soft context has a foreign or duplicate actual owner')
      const attemptKey = `${test}:${attempt}:${worker}:${pid}`
      if (!this.attempts.has(attemptKey)) {
        if (this.attempts.size >= 8192)
          throw new Error('Native soft attempt catalog capacity exceeded')
        this.attempts.set(attemptKey, [])
      }
      if (kind !== 'body' && token === '0')
        throw new Error('Native soft hook context has no actual callback token')
      this.contexts.set(key({ token, scope, generation }), {
        owner: Object.freeze({ token, scope, generation }),
        attempt: attemptKey,
      })
      return true
    }
    if (fields[0] === 'SOFT_CONTEXT_RETIRED') {
      const [token, scope, generation] = z.tuple([Token, Integer, Integer]).parse(fields.slice(1)),
        identity = key({ token, scope, generation })
      if (
        !this.contexts.has(identity) ||
        [...this.pending.values()].some((p) => key(p.owner) === identity)
      )
        throw new Error('Native soft context retired before actual matcher ACKs')
      this.contexts.delete(identity)
      const release = this.retirements.get(identity)
      if (release) {
        this.retirements.delete(identity)
        release()
      }
      this.changed()
      return true
    }
    if (
      fields[0] !== 'SOFT_ADMITTED' &&
      fields[0] !== 'SOFT_SETTLED_ACK' &&
      fields[0] !== 'SOFT_ABORT'
    )
      return false
    if (fields[0] === 'SOFT_ABORT') {
      const [request, token, scope, generation, id, error] = z
          .tuple([Integer, Token, Integer, Integer, Integer, z.string()])
          .parse(fields.slice(1)),
        pending = this.owned(request, { token, scope, generation })
      if (pending.id !== id) throw new Error('Native soft abort has no exact native admission')
      if (!pending.abort.signal.aborted) pending.abort.abort(new Error(decode(error)))
      return true
    }
    const [request, token, scope, generation, id, ok] = z
        .tuple([
          Integer,
          Token,
          Integer,
          Integer,
          z.union([z.literal('0'), Integer]),
          z.enum(['0', '1']),
        ])
        .parse(fields.slice(1, 7)),
      pending = this.owned(request, { token, scope, generation })
    if (fields[0] === 'SOFT_ADMITTED') {
      const [remaining, error] = z
        .tuple([z.coerce.number().int().min(0).max(120000), z.string()])
        .parse(fields.slice(7))
      if (
        pending.id ||
        pending.settled ||
        (ok === '1') !== (id !== '0') ||
        (ok === '1' && error !== '-')
      )
        throw new Error('Native soft admission ACK is contradictory')
      if (ok === '0') {
        this.pending.delete(request)
        pending.reject(new Error(decode(error)))
        this.changed()
      } else {
        pending.id = id
        pending.resolve(
          Object.freeze({
            signal: pending.abort.signal,
            timeoutMs: remaining,
            settle: (value: NativeSoftAssertionSettlement) => this.settle(request, pending, value),
          }),
        )
      }
    } else {
      const [error] = z.tuple([z.string()]).parse(fields.slice(7))
      if (
        pending.id !== id ||
        !pending.settled ||
        !pending.settlement ||
        (ok === '1' && error !== '-')
      )
        throw new Error('Native soft settlement ACK has no actual raw receipt')
      if (ok === '1' && pending.error) {
        const errors = this.attempts.get(pending.context.attempt)
        if (!errors || errors.length >= 32)
          throw new Error('Native soft accepted errors exceed actual attempt bound')
        errors.push(pending.error)
      }
      this.pending.delete(request)
      if (ok === '1') pending.settlement.resolve()
      else pending.settlement.reject(new Error(decode(error)))
      this.changed()
    }
    return true
  }
  private owned(request: string, owner: NativeStepOwner): Pending {
    const pending = this.pending.get(request)
    if (!pending || key(pending.owner) !== key(owner))
      throw new Error('Native soft receipt has a foreign or retired callback request')
    return pending
  }
}
