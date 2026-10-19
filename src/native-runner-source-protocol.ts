// SPDX-License-Identifier: MIT
// Private dedicated-fd marshalling. C++ selects every fixture and registration.
import { Buffer } from 'node:buffer'

import * as z from 'zod'

import {
  NativeSourceSetupCallbacks,
  type NativeSourceSetupSelection,
  type NativeSourceLaunchReceipt,
} from './native-runner-source-setup.js'
import {
  type NativeSourceWorkChallenge,
  type NativeSourceWorkConnection,
} from './native-source-work.js'
import { type NativeWorkWatermark } from './native-work-watermark.js'
import { toError, type UntrustedInput } from './untrusted.js'

const UInt = z
  .string()
  .regex(/^[1-9][0-9]{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const ZeroUInt = z.union([z.literal('0'), UInt])
const Key = z.string().regex(/^[A-Za-z_][A-Za-z0-9_]{0,255}$/u)
const Setup = z.union([
  z.tuple([z.literal('SOURCE_SETUP'), UInt, UInt, UInt, UInt, UInt, UInt, Key]),
  z.tuple([
    z.literal('SOURCE_SETUP'),
    UInt,
    UInt,
    UInt,
    UInt,
    ZeroUInt,
    UInt,
    Key,
    z.enum(['test', 'beforeAll', 'afterAll']),
  ]),
])
type Pending = {
  readonly selection: NativeSourceSetupSelection
  readonly reject: (error: Error) => void
} & (
  | {
      readonly kind: 'register'
      readonly resolve: (value: NativeSourceLaunchReceipt | { readonly kind: 'freshOnly' }) => void
    }
  | { readonly kind: 'startup' | 'cancel'; readonly ticket: string; readonly resolve: () => void }
)
export class NativeSourceSetupProtocol {
  readonly callbacks: NativeSourceSetupCallbacks
  private readonly selections = new Map<string, NativeSourceSetupSelection>()
  private readonly pending = new Map<string, Pending>()
  private readonly registered = new Map<string, NativeSourceSetupSelection>()
  private readonly work = new Map<string, NativeSourceWorkConnection>()
  private readonly closedWork = new Map<string, NativeSourceWorkConnection>()
  private readonly challenges = new Map<string, NativeSourceWorkChallenge>()
  private readonly ready = new Set<string>()
  private readonly resumes = new Map<string, string>()
  private nonce = 0n
  constructor(
    private readonly send: (...fields: (string | number)[]) => Promise<void>,
    private readonly changed: () => void,
    private readonly workBridge = false,
  ) {
    this.callbacks = new NativeSourceSetupCallbacks(
      {
        request: (selection, executable) => {
          const request = this.next()
          const promise = new Promise<NativeSourceLaunchReceipt | { readonly kind: 'freshOnly' }>(
            (resolve, reject) => {
              this.pending.set(request, { kind: 'register', selection, resolve, reject })
            },
          )
          this.deliver(request, [
            'SOURCE_REGISTER',
            request,
            selection.catalog,
            selection.declaration,
            selection.token,
            selection.owner,
            selection.test,
            selection.generation,
            selection.fixtureKey,
            Buffer.from(executable, 'utf8').toString('hex'),
          ])
          return promise
        },
        ...(workBridge
          ? {
              bindWork: (
                selection: NativeSourceSetupSelection,
                ticket: string,
                connection: NativeSourceWorkConnection,
              ) => {
                if (this.registered.get(ticket) !== selection || this.work.has(ticket))
                  throw new Error('Native Source work binding differs from its actual registration')
                this.work.set(ticket, connection)
                const unsubscribe = connection.onChange?.(() => this.workChanged(ticket))
                return () => {
                  unsubscribe?.()
                  if (this.work.get(ticket) === connection) {
                    this.work.delete(ticket)
                    this.closedWork.set(ticket, connection)
                  }
                  this.changed()
                }
              },
              observeWork: (
                selection: NativeSourceSetupSelection,
                ticket: string,
                watermark: NativeWorkWatermark,
              ) => {
                if (this.registered.get(ticket) !== selection || !this.work.has(ticket))
                  return Promise.reject(
                    new Error('Native Source observation lost its actual registration'),
                  )
                return this.send(
                  'SOURCE_WORK_OBSERVED',
                  ticket,
                  watermark.barrier,
                  watermark.readerInstance,
                  watermark.readerThrough,
                  watermark.readerCut,
                  watermark.writerInstance,
                  watermark.writerThrough,
                  watermark.writerCut,
                )
              },
            }
          : {}),
        startup: (selection, ticket) => this.continue('startup', selection, ticket),
        cancel: (selection, ticket) => this.continue('cancel', selection, ticket),
      },
      changed,
    )
  }
  get idleReady(): boolean {
    return (
      !this.selections.size &&
      !this.pending.size &&
      this.callbacks.idleReady &&
      [...this.work.values()].every((connection) => connection.idleReady)
    )
  }
  take(
    token: string,
    owner: string,
    generation: string,
    key: string,
    scope: 'worker' | 'test' | undefined,
    constant: boolean,
    callbackScope: 'test' | 'beforeAll' | 'afterAll' = 'test',
    actualTestId?: string,
  ): NativeSourceSetupSelection | undefined {
    const selection = this.selections.get(token)
    if (!selection) return undefined
    if (
      selection.owner !== owner ||
      selection.generation !== generation ||
      selection.fixtureKey !== key ||
      scope !== 'worker' ||
      constant ||
      (selection.callbackScope ?? 'test') !== callbackScope ||
      (actualTestId !== undefined && selection.test !== actualTestId)
    )
      throw new Error('Native Source selected callback differs from actual fixture admission')
    this.selections.delete(token)
    return selection
  }
  close(token: string, generation: string): void {
    this.callbacks.close(token, generation)
    this.changed()
  }
  packet(fields: readonly string[]): boolean {
    if (fields[0]?.startsWith('SOURCE_WORK_')) {
      if (!this.workBridge)
        throw new Error(
          'Native Source work bridge was not admitted by the actual native host launch',
        )
      return this.workPacket(fields)
    }
    if (fields[0] === 'SOURCE_SETUP') {
      const [, catalog, declaration, token, owner, test, generation, fixtureKey, callbackScope] =
        Setup.parse(fields)
      if (this.selections.has(token) || this.selections.size >= 64)
        throw new Error('Duplicate native Source setup sidecar')
      if (callbackScope === undefined || callbackScope === 'test' ? test === '0' : test !== '0')
        throw new Error('Native Source all-hook associated test identity differs')
      this.selections.set(token, {
        catalog,
        declaration,
        token,
        owner,
        test,
        generation,
        fixtureKey,
        scope: 'worker',
        origin: 'callback',
        ...(callbackScope === undefined ? {} : { callbackScope }),
      })
      return true
    }
    if (
      fields[0] !== 'SOURCE_REGISTERED' &&
      fields[0] !== 'SOURCE_STARTED' &&
      fields[0] !== 'SOURCE_CANCELLED'
    )
      return false
    const request = UInt.parse(fields[1]),
      pending = this.pending.get(request)
    if (!pending) throw new Error('Native Source acknowledgment has no actual pending request')
    const selection = pending.selection
    if (fields[0] === 'SOURCE_REGISTERED') {
      const [, , token, owner, generation, registered, ticket, descriptor, cookie] = z
        .tuple([
          z.literal('SOURCE_REGISTERED'),
          UInt,
          UInt,
          UInt,
          UInt,
          z.enum(['0', '1', '2']),
          ZeroUInt,
          z.enum(['0', '4']),
          ZeroUInt,
        ])
        .parse(fields)
      if (
        pending.kind !== 'register' ||
        token !== selection.token ||
        owner !== selection.owner ||
        generation !== selection.generation ||
        (registered !== '1'
          ? ticket !== '0' || descriptor !== '0' || cookie !== '0'
          : ticket === '0' || descriptor !== '4' || cookie === '0')
      )
        throw new Error('Native Source registration acknowledgment owner/configuration differs')
      this.pending.delete(request)
      if (registered === '1') this.registered.set(ticket, selection)
      if (registered === '2')
        pending.reject(
          new Error('Native Source setup admission closed or its original deadline expired'),
        )
      else
        pending.resolve(
          registered === '0'
            ? { kind: 'freshOnly' }
            : { kind: 'registered', ticket, descriptor: 4, cookie },
        )
    } else if (fields[0] === 'SOURCE_STARTED') {
      const [, , ticket, token, owner, generation, ready, pid, start] = z
        .tuple([
          z.literal('SOURCE_STARTED'),
          UInt,
          UInt,
          UInt,
          UInt,
          UInt,
          z.enum(['0', '1']),
          ZeroUInt,
          ZeroUInt,
        ])
        .parse(fields)
      if (
        pending.kind !== 'startup' ||
        ticket !== pending.ticket ||
        token !== selection.token ||
        owner !== selection.owner ||
        generation !== selection.generation ||
        (ready === '0' ? pid !== '0' || start !== '0' : pid === '0' || start === '0')
      )
        throw new Error('Native Source startup acknowledgment owner/identity differs')
      this.pending.delete(request)
      if (ready === '1') pending.resolve()
      else
        pending.reject(
          new Error('Native Source startup was not accepted within the original fixture deadline'),
        )
    } else {
      const [, , ticket, token, owner, generation] = z
        .tuple([z.literal('SOURCE_CANCELLED'), UInt, UInt, UInt, UInt, UInt])
        .parse(fields)
      if (
        pending.kind !== 'cancel' ||
        ticket !== pending.ticket ||
        token !== selection.token ||
        owner !== selection.owner ||
        generation !== selection.generation
      )
        throw new Error('Native Source cancellation acknowledgment owner differs')
      this.pending.delete(request)
      pending.resolve()
    }
    this.changed()
    return true
  }
  private workPacket(fields: readonly string[]): boolean {
    if (fields[0] === 'SOURCE_WORK_BEGIN') {
      const [, ticket, nonce, borrowLease, borrowGeneration, deadline, purpose] = z
        .tuple([
          z.literal('SOURCE_WORK_BEGIN'),
          UInt,
          UInt,
          UInt,
          UInt,
          z.string().min(1).max(64),
          z.literal('callbackIteration').optional(),
        ])
        .parse(fields)
      const connection = this.work.get(ticket)
      if (!connection || this.challenges.has(ticket))
        throw new Error('Native Source work challenge has no unique actual browser transport')
      const challenge = Object.freeze({
        ticket,
        nonce,
        borrowLease,
        borrowGeneration,
        deadline,
        ...(purpose ? { purpose } : {}),
      })
      this.challenges.set(ticket, challenge)
      connection.begin(challenge)
      this.workChanged(ticket)
    } else if (fields[0] === 'SOURCE_WORK_EXPECT' || fields[0] === 'SOURCE_WORK_ACK') {
      const [
        ,
        ticket,
        barrier,
        readerInstance,
        readerThrough,
        readerCut,
        writerInstance,
        writerThrough,
        writerCut,
      ] = z
        .tuple([
          z.enum(['SOURCE_WORK_EXPECT', 'SOURCE_WORK_ACK']),
          UInt,
          UInt,
          UInt,
          ZeroUInt,
          UInt,
          UInt,
          ZeroUInt,
          UInt,
        ])
        .parse(fields)
      const connection = this.work.get(ticket) ?? this.closedWork.get(ticket),
        challenge = this.challenges.get(ticket)
      if (!connection || challenge?.nonce !== barrier)
        throw new Error('Native Source cut receipt has no exact challenge/transport')
      const watermark: NativeWorkWatermark = {
        version: 1,
        type: 'workwatermark',
        bytes: 0,
        barrier,
        readerInstance,
        readerThrough,
        readerCut,
        writerInstance,
        writerThrough,
        writerCut,
      }
      if (fields[0] === 'SOURCE_WORK_EXPECT') connection.expect(ticket, watermark)
      else connection.acknowledge(ticket, watermark)
    } else if (fields[0] === 'SOURCE_WORK_REFUSE') {
      const [, ticket, nonce] = z
        .tuple([z.literal('SOURCE_WORK_REFUSE'), UInt, UInt, UInt])
        .parse(fields)
      const connection = this.work.get(ticket) ?? this.closedWork.get(ticket),
        challenge = this.challenges.get(ticket)
      if (!connection || challenge?.nonce !== nonce)
        throw new Error('Native Source refusal has no exact pending challenge')
      connection.refuse(ticket, nonce)
    } else if (fields[0] === 'SOURCE_WORK_RESUME') {
      const [, ticket, nonce, nextGeneration] = z
        .tuple([z.literal('SOURCE_WORK_RESUME'), UInt, UInt, UInt])
        .parse(fields)
      const connection = this.work.get(ticket),
        challenge = this.challenges.get(ticket)
      if (
        !connection ||
        challenge?.nonce !== nonce ||
        BigInt(nextGeneration) <= BigInt(challenge.borrowGeneration)
      )
        throw new Error('Native Source resume has no exact completed challenge')
      if (this.resumes.has(ticket)) throw new Error('Native Source resume was replayed')
      if (challenge.purpose === 'callbackIteration') {
        this.resumes.set(ticket, nextGeneration)
        this.workChanged(ticket)
      } else {
        connection.resume(challenge, nextGeneration)
        this.challenges.delete(ticket)
      }
    } else throw new Error('Unknown native Source work control packet')
    if (fields[1]) this.workChanged(fields[1])
    this.changed()
    return true
  }
  private workChanged(ticket: string): void {
    const connection = this.work.get(ticket),
      challenge = this.challenges.get(ticket)
    if (!connection || challenge?.purpose !== 'callbackIteration' || !connection.idleReady) return
    if (!this.ready.has(ticket)) {
      this.ready.add(ticket)
      void this.send('SOURCE_WORK_READY', ticket, challenge.nonce).catch((error) => {
        throw toError(error as UntrustedInput)
      })
    }
    const next = this.resumes.get(ticket)
    if (next === undefined) return
    // Remove bookkeeping before the real synchronous resume emits onChange.
    this.resumes.delete(ticket)
    this.challenges.delete(ticket)
    this.ready.delete(ticket)
    connection.resume(challenge, next)
    void this.send('SOURCE_WORK_HOST_RESUMED', ticket, challenge.nonce, next).catch((error) => {
      throw toError(error as UntrustedInput)
    })
    this.changed()
  }
  private next(): string {
    if (this.pending.size >= 16 || this.nonce === 18446744073709551615n)
      throw new Error('Native Source request capacity exceeded')
    return String(++this.nonce)
  }
  private continue(
    kind: 'startup' | 'cancel',
    selection: NativeSourceSetupSelection,
    ticket: string,
  ): Promise<void> {
    const request = this.next(),
      promise = new Promise<void>((resolve, reject) => {
        this.pending.set(request, { kind, selection, ticket, resolve, reject })
      })
    this.deliver(request, [
      kind === 'startup' ? 'SOURCE_WAIT' : 'SOURCE_CANCEL',
      request,
      ticket,
      selection.token,
      selection.owner,
      selection.generation,
    ])
    return promise
  }
  private deliver(request: string, fields: (string | number)[]): void {
    void this.send(...fields).catch((error: UntrustedInput) => {
      const pending = this.pending.get(request)
      this.pending.delete(request)
      pending?.reject(toError(error))
      this.changed()
    })
  }
}
