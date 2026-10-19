// SPDX-License-Identifier: MIT
// Author async scope and real callbacks only; native admission owns step state.
import { AsyncLocalStorage } from 'node:async_hooks'
import { Buffer } from 'node:buffer'

import * as z from 'zod'

import { type NativeTestAttach } from './native-runner-attachment-types.js'
import { nativeRunnerCallsites } from './native-runner-location.js'
import { NativeStepFailureOrigins } from './native-runner-step-origins.js'
import { toError, type UntrustedInput } from './untrusted.js'

export interface NativeTestStepInfo {
  readonly id: string
  readonly parentId?: string
  readonly title: string
  readonly signal: AbortSignal
  readonly attach: NativeTestAttach
  skip(): never
  skip(reason: string): never
  skip(condition: boolean, reason?: string): void
}
export interface NativeTestStepOptions {
  /** Native reports prefer the genuine captured caller for errors; authored stacks stay intact. */
  readonly box?: boolean
  readonly timeout?: number
  /** Author-declared source metadata; omitted values use a genuine runtime callsite when available. */
  readonly location?: { readonly file: string; readonly line: number; readonly column: number }
}
export interface NativeTestStep {
  <T>(
    title: string,
    callback: (info: NativeTestStepInfo) => T | Promise<T>,
    options?: NativeTestStepOptions,
  ): Promise<T>
  skip<T>(
    title: string,
    callback: (info: NativeTestStepInfo) => T | Promise<T>,
    options?: NativeTestStepOptions,
  ): Promise<void>
}
export interface NativeStepOwner {
  readonly token: string
  readonly scope: string
  readonly generation: string
}
interface Admission {
  kind: 'callback' | 'notInvoked'
  omittedCallback?: object
  owner: NativeStepOwner
  admit: (id: string) => void
  reject: (error: Error) => void
}
interface LocalMutation {
  owner: NativeStepOwner
  source: string
  control: Error
  promise: Promise<void>
  resolve: () => void
  reject: (error: Error) => void
  accepted: boolean
  acknowledged: boolean
  users: Set<string>
}
interface Physical {
  owner: NativeStepOwner
  abort: AbortController
  settled: boolean
  rawOK?: boolean
  delivered: boolean
  raw: Promise<void>
  localMutation?: LocalMutation
  localCause?: { request: string; source: string }
  mutationError?: Error
  result: (
    status:
      'passed' | 'failed' | 'timedOut' | 'interrupted' | 'skipped' | 'stepSkipped' | 'softFailed',
    error: string,
    cause?: string,
  ) => void
}
const Integer = z
  .string()
  .regex(/^[1-9]\d{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const Token = z.union([z.literal('0'), Integer])
const Owner = z.strictObject({
  token: Token,
  scope: Integer,
  generation: Integer,
})
const Status = z.enum(['passed', 'failed', 'timedOut', 'interrupted', 'skipped'])
const key = (owner: NativeStepOwner): string => `${owner.token}:${owner.scope}:${owner.generation}`
const same = (left: NativeStepOwner, right: NativeStepOwner): boolean => key(left) === key(right)
const hex = (value: string): string => Buffer.from(value, 'utf8').toString('hex') || '-'
const decodeError = (wire: string): string => {
  const checked = z
    .string()
    .max(32768)
    .regex(/^(?:-|(?:[a-f\d]{4})+)$/u)
    .parse(wire)
  return checked === '-' ? '' : Buffer.from(checked, 'hex').toString('utf16le')
}

export interface NativeTestSkipBridge {
  acceptedControl(owner: NativeStepOwner, request: string): Promise<UntrustedInput>
  caughtControl(owner: NativeStepOwner, error: UntrustedInput): Promise<string | undefined>
}

export class NativeStepCallbacks {
  private readonly origins = new NativeStepFailureOrigins()
  private readonly context = new AsyncLocalStorage<{
    owner: NativeStepOwner
    id: string
  }>()
  private readonly requests = new Map<string, Admission>()
  private readonly physical = new Map<string, Physical>()
  private readonly admitted = new Map<
    string,
    {
      readonly owner: NativeStepOwner
      readonly active: () => boolean
      readonly abort: AbortController
      rawReturned: boolean
      received: boolean
    }
  >()
  private readonly outer = new Map<
    string,
    { owner: NativeStepOwner; rawSettled: boolean; release: () => void }
  >()
  private nonce = 0n
  private localNonce = 0n
  private readonly localMutations = new Map<string, LocalMutation>()
  constructor(
    private readonly send: (...fields: (string | number)[]) => Promise<void>,
    private readonly fatal: (error: Error) => void,
    private readonly changed: () => void,
    private readonly testSkip?: NativeTestSkipBridge,
    private readonly attachStep?: (
      owner: NativeStepOwner,
      id: string,
      active: () => boolean,
    ) => NativeTestAttach,
    private readonly localSkipAccepted?: (
      owner: NativeStepOwner,
      request: string,
      source: string,
      actualStepIds: readonly string[],
      control: Error,
    ) => void,
    private readonly invokeAuthor?: <T>(
      owner: NativeStepOwner,
      active: () => boolean,
      callback: () => T,
    ) => T,
  ) {}
  get pending(): number {
    return (
      this.requests.size +
      this.physical.size +
      this.admitted.size +
      this.outer.size +
      this.localMutations.size
    )
  }
  /** Native toPass admission already created this real iteration step. */
  runAdmitted<T>(
    owner: NativeStepOwner,
    id: string,
    parent: string,
    active: () => boolean,
    callback: (signal: AbortSignal) => T,
  ): T {
    const actual = Owner.parse(owner),
      step = Integer.parse(id)
    z.union([z.literal('0'), Integer]).parse(parent)
    if (this.admitted.has(step) || this.physical.has(step))
      throw new Error('Native managed step admission was replayed')
    const lease = {
      owner: actual,
      active,
      abort: new AbortController(),
      rawReturned: false,
      received: false,
    }
    this.origins.admit(actual, step, parent)
    this.admitted.set(step, lease)
    return this.context.run({ owner: actual, id: step }, () =>
      this.invokeAuthor
        ? this.invokeAuthor(actual, active, () => callback(lease.abort.signal))
        : callback(lease.abort.signal),
    )
  }
  abortAdmitted(owner: NativeStepOwner, id: string, error: Error): void {
    const actual = this.admitted.get(id)
    if (!actual || !same(actual.owner, owner))
      throw new Error('Native iteration abort lacks its actual step')
    actual.abort.abort(error)
  }
  closeAdmitted(owner: NativeStepOwner, id: string): void {
    const actual = this.admitted.get(id)
    if (!actual || !same(actual.owner, owner) || actual.rawReturned)
      throw new Error('Native managed step raw receipt is stale')
    actual.rawReturned = true
  }
  retireAdmitted(owner: NativeStepOwner, id: string): void {
    const actual = this.admitted.get(id)
    if (!actual || !same(actual.owner, owner) || !actual.rawReturned || !actual.received)
      throw new Error('Native managed step lacks physical receipt')
    this.admitted.delete(id)
    this.changed()
  }
  trackCallback(owner: NativeStepOwner, raw: Promise<void>, release: () => void): void {
    const actual = Owner.parse(owner),
      identity = key(actual)
    if (this.outer.has(identity) || this.outer.size >= 64)
      throw new Error('Native outer callback receipt capacity exceeded')
    const callback = { owner: actual, rawSettled: false, release }
    this.outer.set(identity, callback)
    // This observer runs only after the genuine wrapper/raw author callback
    // promise settles. Root native retirement waits this separate receipt.
    void raw
      .then(async () => {
        callback.rawSettled = true
        await this.send('STEP_CALLBACK_SETTLED', actual.token, actual.scope, actual.generation)
      })
      .catch((error: UntrustedInput) => {
        this.fatal(toError(error))
      })
  }
  current(owner: NativeStepOwner): string {
    const context = this.context.getStore()
    if (!context) return '0'
    if (!same(context.owner, Owner.parse(owner)))
      throw new Error('Native step author context belongs to another callback')
    const admitted = this.admitted.get(context.id)
    if (admitted) {
      if (
        !same(admitted.owner, owner) ||
        admitted.rawReturned ||
        !admitted.active() ||
        admitted.abort.signal.aborted
      )
        throw new Error('Native managed step author context is no longer active')
      return context.id
    }
    const actual = this.physical.get(context.id)
    if (!actual || actual.settled || actual.abort.signal.aborted)
      throw new Error('Native step author context is no longer active')
    return context.id
  }
  originating(owner: NativeStepOwner, error: UntrustedInput): string | undefined {
    return this.origins.source(owner, error)
  }
  bind(owner: NativeStepOwner, active: () => boolean): NativeTestStep {
    const admittedOwner = Owner.parse(owner)
    const step = <T>(
      title: string,
      callback: (info: NativeTestStepInfo) => T | Promise<T>,
      options: NativeTestStepOptions = {},
    ): Promise<T> => {
      const actualTitle = z.string().max(4096).parse(title)
      if (typeof callback !== 'function')
        throw new TypeError('Native step callback must be a function')
      const actualOptions = z
        .strictObject({
          timeout: z.number().int().min(0).max(120000).optional(),
          box: z.boolean().optional(),
          location: z
            .strictObject({
              file: z.string().min(1).max(4096),
              line: z.number().int().min(1).max(2147483647),
              column: z.number().int().min(1).max(2147483647),
            })
            .optional(),
        })
        .parse(options)
      const callsites = nativeRunnerCallsites()
      const location = actualOptions.location ?? callsites[0]
      if (!active()) throw new Error('Native step owning callback is no longer active')
      if (this.requests.size >= 128 || this.nonce === 18446744073709551615n)
        throw new Error('Native step admission request capacity exceeded')
      const ancestry = this.context.getStore()
      if (ancestry && !same(ancestry.owner, admittedOwner))
        throw new Error('Native step ancestry belongs to a different callback')
      const parent = ancestry?.id ?? '0',
        request = String(this.nonce + 1n)
      const admissionFields: (string | number)[] = [
        'STEP_BEGIN',
        request,
        admittedOwner.token,
        admittedOwner.scope,
        admittedOwner.generation,
        parent,
        hex(JSON.stringify(actualTitle)),
        actualOptions.timeout ?? 0,
        location ? Buffer.from(location.file, 'utf16le').toString('hex') : '-',
        location?.line ?? 0,
        location?.column ?? 0,
        location ? (actualOptions.location ? 2 : 1) : 0,
      ]
      if (actualOptions.box) {
        admissionFields.push(1)
        for (const site of [callsites[0], callsites[1]])
          admissionFields.push(
            site ? Buffer.from(site.file, 'utf16le').toString('hex') : '-',
            site?.line ?? 0,
            site?.column ?? 0,
            site ? 1 : 0,
          )
      }
      if (Buffer.byteLength(admissionFields.join(' ')) > 65536)
        throw new RangeError('Native step combined metadata exceeds64KiB control budget')
      ++this.nonce
      return new Promise<T>((resolve, reject) => {
        this.requests.set(request, {
          kind: 'callback',
          owner: admittedOwner,
          reject,
          admit: (id) => {
            this.origins.admit(admittedOwner, id, parent)
            let actualValue: T | undefined, actualError: UntrustedInput
            const lease: Physical = {
              owner: admittedOwner,
              abort: new AbortController(),
              settled: false,
              delivered: false,
              raw: Promise.resolve(),
              result: (status, error, cause) => {
                if (lease.delivered) throw new Error('Duplicate native logical step result')
                lease.delivered = true
                if (status === 'stepSkipped') {
                  if (!cause || !lease.localCause)
                    throw new Error('Native local-step skip has no actual cause')
                  void this.localControl(
                    admittedOwner,
                    cause,
                    lease.localCause.source,
                  ).promise.then(
                    () => resolve(undefined as T),
                    (error: UntrustedInput) => this.fatal(toError(error)),
                  )
                } else if (status === 'skipped') {
                  if (!cause || !this.testSkip)
                    throw new Error('Native skipped step has no accepted test-skip bridge')
                  void this.testSkip
                    .acceptedControl(admittedOwner, cause)
                    .then(reject, (error: UntrustedInput) => this.fatal(toError(error)))
                } else if (status === 'passed' || status === 'softFailed') {
                  if (status === 'softFailed' && lease.rawOK !== true)
                    throw new Error('Native soft step result cannot replace an actual raw error')
                  if (!lease.settled)
                    throw new Error('Native step passed without actual raw callback settlement')
                  resolve(actualValue as T)
                } else {
                  // Preserve the genuine author's thrown value, including primitive throws.
                  if (status === 'failed' && lease.settled && lease.rawOK === false)
                    this.origins.observe(admittedOwner, id, actualError)
                  // eslint-disable-next-line @typescript-eslint/prefer-promise-reject-errors
                  reject(
                    status === 'failed' && lease.settled && lease.rawOK === false
                      ? actualError
                      : new Error(error),
                  )
                }
              },
            }
            this.physical.set(id, lease)
            lease.raw = (async () => {
              let ok = false,
                error = '',
                testSkip: string | undefined,
                localSkip: string | undefined
              try {
                const invoke = () =>
                  callback(
                    Object.freeze({
                      id,
                      title: actualTitle,
                      ...(parent === '0' ? {} : { parentId: parent }),
                      signal: lease.abort.signal,
                      attach:
                        this.attachStep?.(
                          admittedOwner,
                          id,
                          () => active() && !lease.settled && !lease.abort.signal.aborted,
                        ) ??
                        (() => {
                          throw new Error('Native step attachment bridge unavailable')
                        }),
                      skip: this.skipFor(lease, id, admittedOwner, active),
                    }),
                  )
                actualValue = await this.context.run({ owner: admittedOwner, id }, () =>
                  this.invokeAuthor
                    ? this.invokeAuthor(
                        admittedOwner,
                        () => active() && !lease.settled && !lease.abort.signal.aborted,
                        invoke,
                      )
                    : invoke(),
                )
                ok = true
              } catch (value) {
                actualError = value as UntrustedInput
                try {
                  error = (toError(actualError).stack ?? toError(actualError).message).slice(
                    0,
                    8192,
                  )
                } catch {
                  error = 'Thrown value could not be represented'
                }
              }
              if (lease.localMutation) await Promise.allSettled([lease.localMutation.promise])
              if (lease.mutationError) {
                actualError = lease.mutationError
                ok = false
              }
              if (!ok) {
                testSkip = await this.testSkip?.caughtControl(admittedOwner, actualError)
                const cause = lease.localCause
                if (!testSkip && cause) {
                  const mutation = this.localControl(admittedOwner, cause.request, cause.source)
                  await Promise.allSettled([mutation.promise])
                  if (mutation.accepted && actualError === mutation.control)
                    localSkip = cause.request
                }
                if (lease.mutationError)
                  error = (lease.mutationError.stack ?? lease.mutationError.message).slice(0, 8192)
              }
              lease.rawOK = ok
              lease.settled = true
              // Do not retire this map entry on a logical result or send failure.
              // Only the native physical receipt ACK can release the raw lease.
              const originating =
                !ok && !testSkip && !localSkip
                  ? this.origins.source(admittedOwner, actualError, id)
                  : undefined
              await this.send(
                testSkip
                  ? 'STEP_SETTLED_TEST_SKIP'
                  : localSkip
                    ? 'STEP_SETTLED_LOCAL_SKIP'
                    : originating
                      ? 'STEP_SETTLED_FROM_STEP'
                      : 'STEP_SETTLED',
                id,
                admittedOwner.token,
                admittedOwner.scope,
                admittedOwner.generation,
                testSkip ?? localSkip ?? (ok ? 1 : 0),
                testSkip || localSkip ? '-' : Buffer.from(error, 'utf16le').toString('hex') || '-',
                ...(originating === undefined ? [] : [originating]),
              )
            })().catch((error: UntrustedInput) => {
              this.fatal(toError(error))
            })
          },
        })
        void this.send(...admissionFields).catch((error: UntrustedInput) => {
          // Native did not confirm admission failure or physical closure. Keep
          // the request until process teardown; do not invent a rejection ACK.
          this.fatal(toError(error))
        })
      })
    }
    const skip = <T>(
      title: string,
      callback: (info: NativeTestStepInfo) => T | Promise<T>,
      options: NativeTestStepOptions = {},
    ): Promise<void> => {
      const actualTitle = z.string().max(4096).parse(title)
      if (typeof callback !== 'function')
        throw new TypeError('Native step callback must be a function')
      const actualOptions = z
        .strictObject({
          timeout: z.number().int().min(0).max(120000).optional(),
          box: z.boolean().optional(),
          location: z
            .strictObject({
              file: z.string().min(1).max(4096),
              line: z.number().int().min(1).max(2147483647),
              column: z.number().int().min(1).max(2147483647),
            })
            .optional(),
        })
        .parse(options)
      const callsites = nativeRunnerCallsites()
      const location = actualOptions.location ?? callsites[0]
      if (!active()) throw new Error('Native step owning callback is no longer active')
      if (this.requests.size >= 128 || this.nonce === 18446744073709551615n)
        throw new Error('Native step admission request capacity exceeded')
      const ancestry = this.context.getStore()
      if (ancestry && !same(ancestry.owner, admittedOwner))
        throw new Error('Native step ancestry belongs to a different callback')
      const parent = ancestry?.id ?? '0',
        request = String(this.nonce + 1n)
      const admissionFields: (string | number)[] = [
        'STEP_BEGIN_SKIP',
        request,
        admittedOwner.token,
        admittedOwner.scope,
        admittedOwner.generation,
        parent,
        hex(JSON.stringify(actualTitle)),
        actualOptions.timeout ?? 0,
        location ? Buffer.from(location.file, 'utf16le').toString('hex') : '-',
        location?.line ?? 0,
        location?.column ?? 0,
        location ? (actualOptions.location ? 2 : 1) : 0,
      ]
      if (actualOptions.box) {
        admissionFields.push(1)
        for (const site of [callsites[0], callsites[1]])
          admissionFields.push(
            site ? Buffer.from(site.file, 'utf16le').toString('hex') : '-',
            site?.line ?? 0,
            site?.column ?? 0,
            site ? 1 : 0,
          )
      }
      if (Buffer.byteLength(admissionFields.join(' ')) > 65536)
        throw new RangeError('Native step combined metadata exceeds64KiB control budget')
      ++this.nonce
      return new Promise<void>((resolve, reject) => {
        // Retain the unused real author closure until actual native admission.
        // It is never invoked and no raw STEP_SETTLED is sent for it.
        this.requests.set(request, {
          kind: 'notInvoked',
          owner: admittedOwner,
          omittedCallback: callback,
          admit: () => resolve(),
          reject,
        })
        void this.send(...admissionFields).catch((error: UntrustedInput) =>
          this.fatal(toError(error)),
        )
      })
    }
    return Object.assign(step, { skip })
  }
  private localControl(owner: NativeStepOwner, request: string, source: string): LocalMutation {
    const mutation = this.localMutations.get(request)
    if (!mutation || !same(mutation.owner, owner) || mutation.source !== source)
      throw new Error('Native local-step skip cause has no actual author mutation')
    return mutation
  }
  private releaseLocal(request: string, mutation: LocalMutation): void {
    if (mutation.acknowledged && !mutation.users.size) this.localMutations.delete(request)
  }
  private skipFor(
    lease: Physical,
    id: string,
    owner: NativeStepOwner,
    active: () => boolean,
  ): NativeTestStepInfo['skip'] {
    const issue = (reason: string | undefined): never => {
      if (
        lease.settled ||
        lease.delivered ||
        lease.abort.signal.aborted ||
        lease.localMutation ||
        !active()
      )
        throw new Error('Native local-step skip owning callback is no longer active')
      if (this.localMutations.size >= 128 || this.localNonce === 18446744073709551615n)
        throw new Error('Native local-step skip mutation capacity exceeded')
      const request = String(++this.localNonce),
        control = new Error('Native local step skip requested')
      let resolve!: () => void, reject!: (error: Error) => void
      const promise = new Promise<void>((good, bad) => {
        resolve = good
        reject = bad
      })
      const mutation: LocalMutation = {
        owner,
        source: id,
        control,
        promise,
        resolve,
        reject,
        accepted: false,
        acknowledged: false,
        users: new Set(),
      }
      lease.localMutation = mutation
      this.localMutations.set(request, mutation)
      void promise.catch((error: UntrustedInput) => {
        lease.mutationError = toError(error)
      })
      void this.send(
        'STEP_LOCAL_SKIP_REQUEST',
        request,
        id,
        owner.token,
        owner.scope,
        owner.generation,
        Buffer.from(reason ?? '', 'utf16le').toString('hex') || '-',
        reason === undefined ? 0 : 1,
      ).catch((error: UntrustedInput) => this.fatal(toError(error)))
      throw control
    }
    function skip(): never
    function skip(reason: string): never
    function skip(condition: boolean, reason?: string): void
    function skip(conditionOrReason: boolean | string = true, reason?: string): void {
      const condition = z.union([z.boolean(), z.string().max(8192)]).parse(conditionOrReason)
      const actualReason = z
        .string()
        .max(8192)
        .optional()
        .parse(typeof condition === 'string' ? condition : reason)
      if (condition === false) return
      issue(actualReason)
    }
    return skip
  }
  handle(fields: readonly string[]): boolean {
    if (fields[0] === 'STEP_LOCAL_SKIP_ACK') {
      const [request, id, token, scope, generation, ok, error] = z
        .tuple([Integer, Integer, Token, Integer, Integer, z.enum(['0', '1']), z.string()])
        .parse(fields.slice(1))
      const mutation = this.localControl({ token, scope, generation }, request, id)
      if (mutation.acknowledged) throw new Error('Duplicate native local-step skip acknowledgment')
      mutation.acknowledged = true
      if (ok === '1') {
        if (error !== '-') throw new Error('Native local-step skip ACK has an error')
        mutation.accepted = true
        this.localSkipAccepted?.(
          mutation.owner,
          request,
          mutation.source,
          [...mutation.users],
          mutation.control,
        )
        mutation.resolve()
      } else mutation.reject(new Error(decodeError(error)))
      this.releaseLocal(request, mutation)
      this.changed()
      return true
    }
    if (fields[0] === 'STEP_LOCAL_SKIP_ABORT' || fields[0] === 'STEP_LOCAL_SKIP_RESULT') {
      const [id, token, scope, generation, request, source] = z
        .tuple([Integer, Token, Integer, Integer, Integer, Integer])
        .parse(fields.slice(1))
      const lease = this.physical.get(id),
        owner = { token, scope, generation }
      if (!lease || !same(lease.owner, owner))
        throw new Error('Stale native local-step skip outcome')
      const mutation = this.localControl(owner, request, source)
      if (
        lease.localCause &&
        (lease.localCause.request !== request || lease.localCause.source !== source)
      )
        throw new Error('Native local-step skip cause changed after admission')
      lease.localCause = { request, source }
      mutation.users.add(id)
      if (fields[0] === 'STEP_LOCAL_SKIP_ABORT')
        void mutation.promise.then(
          () => lease.abort.abort(mutation.control),
          (error: UntrustedInput) => this.fatal(toError(error)),
        )
      else lease.result('stepSkipped', '', request)
      return true
    }
    if (fields[0] === 'STEP_CALLBACK_RECEIVED') {
      const [token, scope, generation] = z.tuple([Token, Integer, Integer]).parse(fields.slice(1))
      const identity = key({ token, scope, generation }),
        callback = this.outer.get(identity)
      if (!callback?.rawSettled)
        throw new Error('Native outer callback receipt has no actual raw settlement')
      this.outer.delete(identity)
      callback.release()
      this.changed()
      return true
    }
    if (fields[0] === 'STEP_ADMITTED' || fields[0] === 'STEP_ADMITTED_NOT_INVOKED') {
      const [request, token, scope, generation, id, ok, error] = z
        .tuple([Integer, Token, Integer, Integer, Token, z.enum(['0', '1']), z.string()])
        .parse(fields.slice(1))
      const admission = this.requests.get(request)
      if (
        !admission ||
        !same(admission.owner, { token, scope, generation }) ||
        (fields[0] === 'STEP_ADMITTED_NOT_INVOKED') !== (admission.kind === 'notInvoked')
      )
        throw new Error('Stale native step admission acknowledgement')
      if (
        (ok === '1') !== (id !== '0') ||
        this.physical.has(id) ||
        (ok === '1' && admission.kind === 'callback' && this.physical.size >= 64)
      )
        throw new Error('Invalid native admitted step identity')
      this.requests.delete(request)
      if (ok === '1') admission.admit(id)
      else admission.reject(new Error(decodeError(error)))
      this.changed()
      return true
    }
    if (fields[0] === 'STEP_SOFT_RESULT') {
      const [id, token, scope, generation, softError, error] = z
        .tuple([Integer, Token, Integer, Integer, Integer, z.string()])
        .parse(fields.slice(1))
      const admitted = this.admitted.get(id)
      if (admitted) {
        if (!same(admitted.owner, { token, scope, generation }) || !admitted.rawReturned)
          throw new Error('Native managed soft step has no actual raw receipt')
        return true
      }
      const lease = this.physical.get(id)
      if (!lease || !same(lease.owner, { token, scope, generation }) || !softError)
        throw new Error('Native soft step result has a foreign owner or error receipt')
      lease.result('softFailed', decodeError(error))
      return true
    }
    if (fields[0] === 'STEP_RESULT' || fields[0] === 'STEP_ABORT') {
      const [id, token, scope, generation, status, error, cause] = z
        .tuple([Integer, Token, Integer, Integer, Status, z.string(), Integer.optional()])
        .parse(fields.slice(1))
      const admitted = this.admitted.get(id)
      if (admitted) {
        if (!same(admitted.owner, { token, scope, generation }) || cause !== undefined)
          throw new Error('Native managed step logical outcome is foreign')
        if (fields[0] === 'STEP_ABORT') admitted.abort.abort(new Error(decodeError(error)))
        return true
      }
      const lease = this.physical.get(id)
      if (!lease || !same(lease.owner, { token, scope, generation }))
        throw new Error('Stale native step logical outcome')
      const message = decodeError(error)
      if ((status === 'skipped') !== (cause !== undefined))
        throw new Error('Native step skip cause mismatch')
      if (fields[0] === 'STEP_ABORT') {
        if (cause) {
          if (!this.testSkip) throw new Error('Native step skip has no accepted bridge')
          void this.testSkip.acceptedControl(lease.owner, cause).then(
            (control) => lease.abort.abort(control),
            (error: UntrustedInput) => this.fatal(toError(error)),
          )
        } else lease.abort.abort(new Error(message))
      } else lease.result(status, message, cause)
      return true
    }
    if (fields[0] === 'STEP_RECEIVED') {
      const [id, token, scope, generation] = z
        .tuple([Integer, Token, Integer, Integer])
        .parse(fields.slice(1))
      const admitted = this.admitted.get(id)
      if (admitted) {
        if (
          !same(admitted.owner, { token, scope, generation }) ||
          !admitted.rawReturned ||
          admitted.received
        )
          throw new Error('Native managed step physical receipt is stale')
        admitted.received = true
        return true
      }
      const lease = this.physical.get(id)
      if (
        !lease ||
        !same(lease.owner, { token, scope, generation }) ||
        !lease.settled ||
        !lease.delivered
      )
        throw new Error('Native step physical receipt has no actual settled callback')
      this.physical.delete(id)
      if (lease.localCause) {
        const mutation = this.localControl(
          lease.owner,
          lease.localCause.request,
          lease.localCause.source,
        )
        mutation.users.delete(id)
        this.releaseLocal(lease.localCause.request, mutation)
      }
      this.changed()
      return true
    }
    return false
  }
}
