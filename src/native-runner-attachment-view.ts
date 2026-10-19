// SPDX-License-Identifier: MIT
// Author declaration reflection only. Native packets own acceptance, actual file
// publication and callback retirement; this class does no file I/O or scheduling.
import { AsyncLocalStorage } from 'node:async_hooks'
import { Buffer } from 'node:buffer'

import * as z from 'zod'

import { type NativeStepOwner } from './native-runner-steps.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeTestAttachment {
  name: string
  contentType: string
  body?: Uint8Array
  path?: string
}
export type NativeTestAttachments = NativeTestAttachment[]
const Integer = z
  .string()
  .regex(/^[1-9]\d{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const Owner = z.strictObject({
  token: z.union([z.literal('0'), Integer]),
  scope: Integer,
  generation: Integer,
})
const Context = Owner.extend({
  testId: Integer,
  attemptGeneration: Integer,
  workerIndex: z.number().int().positive(),
  pid: z.number().int().positive(),
})
export type NativeAttachmentViewContext = z.infer<typeof Context>
const Declaration = z.strictObject({
  name: z.string().max(4096),
  contentType: z.string().max(1024),
  body: z
    .instanceof(Uint8Array)
    .refine((value) => value.byteLength <= 16 * 1024 * 1024)
    .optional(),
  path: z.string().max(8192).optional(),
})
interface AttemptView {
  readonly target: NativeTestAttachment[]
  readonly view: NativeTestAttachments
  readonly references: WeakMap<NativeTestAttachment, string>
  readonly slots: (string | null)[]
  proposedRevision: bigint
  initialized: boolean
  editing: boolean
}
interface Entry {
  readonly owner: NativeStepOwner
  readonly attempt: AttemptView
  readonly kind: 'append' | 'attach'
  readonly expectedType?: string
  readonly item: NativeTestAttachment
  readonly expectedBytes?: number
  readonly authorBody?: Uint8Array
  state: 'declared' | 'referenceAccepted' | 'committed' | 'failed' | 'canceled'
}
const key = (owner: NativeStepOwner): string => `${owner.token}:${owner.scope}:${owner.generation}`
const attemptKey = (context: NativeAttachmentViewContext): string =>
  `${context.testId}:${context.attemptGeneration}:${context.workerIndex}:${context.pid}`
const arrayIndex = (property: PropertyKey): boolean =>
  typeof property === 'string' &&
  /^(?:0|[1-9]\d*)$/u.test(property) &&
  Number(property) < 4294967295
const mutation = new Set<PropertyKey>([
  'pop',
  'shift',
  'unshift',
  'splice',
  'sort',
  'reverse',
  'fill',
  'copyWithin',
])
export class NativeAttachmentViews {
  private readonly contexts = new Map<string, NativeAttachmentViewContext>()
  private readonly attempts = new Map<string, AttemptView>()
  private readonly entries = new Map<string, Entry>()
  private readonly artifacts = new Set<string>()
  private readonly author = new AsyncLocalStorage<{
    owner: NativeStepOwner
    active: () => boolean
  }>()
  private retainedBytes = 0
  // 128 native files + 128 authored declarations + at most 128 pending
  // native rejections. A full committed view cannot preempt native rejection.
  private appendCount = 0
  constructor(
    private readonly workerIndex: number,
    private readonly pid: number,
    private readonly enqueue: (
      owner: NativeStepOwner,
      items: readonly NativeTestAttachment[],
    ) => readonly string[],
    private readonly propose?: (
      owner: NativeStepOwner,
      expectedRevision: string,
      slots: readonly (string | null)[],
      publication?: { readonly ticket: string; readonly artifact: string },
    ) => string,
  ) {}
  get contextCount(): number {
    return this.contexts.size
  }
  open(raw: NativeAttachmentViewContext): void {
    const context = Context.parse(raw),
      owner = key(context)
    if (
      context.workerIndex !== this.workerIndex ||
      context.pid !== this.pid ||
      this.contexts.has(owner) ||
      this.contexts.size >= 64
    )
      throw new Error('Native attachment view has foreign or duplicate callback context')
    const attempt = attemptKey(context)
    if (!this.attempts.has(attempt)) {
      if (this.attempts.size >= 8192)
        throw new Error('Native attachment view attempt catalog exceeded')
      const target: NativeTestAttachment[] = []
      const view: NativeTestAttachments = new Proxy(target, {
        get: (array, property, receiver) => {
          if (property === 'push')
            return (...items: readonly NativeTestAttachment[]): number => {
              if (!this.propose) return this.legacyPush(view, items)
              const result = this.edit(view, (candidate) => candidate.push(...items), items)
              if (typeof result !== 'number')
                throw new Error('Native attachment push result changed type')
              return result
            }
          if (mutation.has(property)) {
            if (!this.propose)
              return (): never => {
                throw new Error('Native attachment view supports append/read only')
              }
            return (...args: readonly UntrustedInput[]): UntrustedInput =>
              this.edit(view, (candidate) => {
                const method = Reflect.get(Array.prototype, property) as UntrustedInput
                if (typeof method !== 'function')
                  throw new Error('Native attachment array method unavailable')
                const result = Reflect.apply(method, candidate, args) as UntrustedInput
                return result === candidate ? view : result
              })
          }
          return Reflect.get(array, property, receiver) as UntrustedInput
        },
        set: (_array, property, value: UntrustedInput) => {
          if (!this.propose || (property !== 'length' && !arrayIndex(property)))
            throw new Error('Native attachment view does not support this property assignment')
          this.edit(view, (candidate) => Reflect.set(candidate, property, value))
          return true
        },
        defineProperty: () => {
          throw new Error('Native attachment view does not support property mutation')
        },
        deleteProperty: (_array, property) => {
          if (!this.propose || !arrayIndex(property))
            throw new Error('Native attachment view does not support this property deletion')
          this.edit(view, (candidate) => Reflect.deleteProperty(candidate, property))
          return true
        },
        preventExtensions: () => {
          throw new Error('Native attachment view cannot be frozen or sealed')
        },
        setPrototypeOf: () => {
          throw new Error('Native attachment view does not support prototype mutation')
        },
      })
      this.attempts.set(attempt, {
        target,
        view,
        references: new WeakMap(),
        slots: [],
        proposedRevision: 0n,
        initialized: false,
        editing: false,
      })
    }
    this.contexts.set(owner, context)
  }
  hasContext(owner: NativeStepOwner): boolean {
    return this.contexts.has(key(Owner.parse(owner)))
  }
  run<T>(owner: NativeStepOwner, active: () => boolean, callback: () => T): T {
    this.context(owner)
    return this.author.run({ owner: Object.freeze(Owner.parse(owner)), active }, callback)
  }
  view(owner: NativeStepOwner): NativeTestAttachments {
    const context = this.context(owner),
      attempt = this.attempts.get(attemptKey(context))
    if (!attempt) throw new Error('Native attachment view has no admitted attempt')
    return attempt.view
  }
  private context(owner: NativeStepOwner): NativeAttachmentViewContext {
    const actual = Owner.parse(owner),
      context = this.contexts.get(key(actual))
    if (!context) throw new Error('Native attachment view callback is stale or retired')
    return context
  }
  prepareNormal(owner: NativeStepOwner): void {
    const context = this.context(owner),
      attempt = this.attempts.get(attemptKey(context))
    if (!attempt) throw new Error('Native attachment view has no actual attempt')
    this.initialize(owner, attempt)
  }
  private initialize(owner: NativeStepOwner, attempt: AttemptView): void {
    if (!this.propose || attempt.initialized) return
    this.submit(owner, attempt, [])
    attempt.initialized = true
  }
  private submit(
    owner: NativeStepOwner,
    attempt: AttemptView,
    slots: readonly (string | null)[],
    publication?: { readonly ticket: string; readonly artifact: string },
  ): string | undefined {
    if (!this.propose) return undefined
    if (attempt.proposedRevision >= 512n)
      throw new Error('Native attachment mutation proposal capacity exceeded')
    const request = this.propose(owner, String(attempt.proposedRevision), slots, publication)
    ++attempt.proposedRevision
    return request
  }
  private edit(
    view: NativeTestAttachments,
    operation: (candidate: NativeTestAttachment[]) => UntrustedInput,
    pushed?: readonly NativeTestAttachment[],
  ): UntrustedInput {
    const author = this.author.getStore()
    if (!author?.active())
      throw new Error('Native attachment mutation has no active author callback')
    const context = this.context(author.owner),
      attempt = this.attempts.get(attemptKey(context))
    if (attempt?.view !== view)
      throw new Error('Native attachment mutation belongs to another actual attempt')
    if (attempt.editing) throw new Error('Native attachment array mutation is reentrant')
    attempt.editing = true
    try {
      const candidate = attempt.target.slice(),
        result = operation(candidate)
      if (candidate.length > 256)
        throw new Error('Native attachment visible slot capacity exceeded')
      const unchanged =
        candidate.length === attempt.target.length &&
        candidate.every(
          (item, index) => Object.hasOwn(attempt.target, index) && attempt.target[index] === item,
        ) &&
        attempt.target.every((_item, index) => Object.hasOwn(candidate, index))
      if (unchanged) return result
      const unknown: NativeTestAttachment[] = pushed === undefined ? [] : [...pushed],
        seen = new Set<NativeTestAttachment>()
      for (let index = 0; index < candidate.length; ++index) {
        if (!Object.hasOwn(candidate, index)) continue
        const item = candidate[index]
        if (item === undefined)
          throw new Error('Native attachment array value is not a declaration')
        if (pushed === undefined && !attempt.references.has(item) && !seen.has(item)) {
          seen.add(item)
          unknown.push(item)
        }
      }
      // Complete validation precedes native declaration requests and author
      // reflection. No old request entry is removed by replacement/withdrawal.
      const parsed = z.array(Declaration).max(128).parse(unknown)
      const bytes = parsed.reduce((sum, item) => sum + (item.body?.byteLength ?? 0), 0)
      if (
        this.appendCount + parsed.length > 128 ||
        this.entries.size + parsed.length > 384 ||
        bytes > 64 * 1024 * 1024 - this.retainedBytes
      )
        throw new Error('Native attachment declaration host capacity exceeded')
      const privateItems = parsed.map((item) =>
        Object.freeze({
          name: item.name,
          contentType: item.contentType,
          ...(item.path === undefined ? {} : { path: item.path }),
          ...(item.body === undefined ? {} : { body: Buffer.from(item.body) }),
        }),
      )
      this.initialize(author.owner, attempt)
      const requests = z
        .array(Integer)
        .length(parsed.length)
        .parse(this.enqueue(author.owner, privateItems))
      if (
        new Set(requests).size !== requests.length ||
        requests.some((request) => this.entries.has(request))
      )
        throw new Error('Native attachment mutation contains duplicate actual declaration requests')
      const admitted: string[] = []
      for (const [index, request] of requests.entries()) {
        const item = privateItems[index],
          original = unknown[index]
        if (!item || !original)
          throw new Error('Native attachment mutation changed declaration shape')
        this.entries.set(request, {
          owner: author.owner,
          attempt,
          kind: 'append',
          expectedType: item.contentType,
          item,
          ...(item.body === undefined ? {} : { expectedBytes: item.body.byteLength }),
          state: 'declared',
        })
        const reference = `d${request}`
        attempt.references.set(original, reference)
        admitted.push(reference)
      }
      // Author identity and immutable insertion receipts are separate. Existing
      // repeated objects can refer to distinct older push snapshots; preserve
      // those occurrence references while reflecting the original objects.
      const existing = new Map<NativeTestAttachment, string[]>()
      for (let index = 0; index < attempt.target.length; ++index) {
        const item = attempt.target[index],
          reference = attempt.slots[index]
        if (item === undefined || !reference) continue
        const refs = existing.get(item) ?? []
        refs.push(reference)
        existing.set(item, refs)
      }
      const slots: (string | null)[] = []
      for (let index = 0; index < candidate.length; ++index) {
        if (!Object.hasOwn(candidate, index)) {
          slots.push(null)
          continue
        }
        const item = candidate[index],
          pushedIndex = pushed === undefined ? -1 : index - attempt.target.length,
          reference =
            item === undefined
              ? undefined
              : pushedIndex >= 0
                ? admitted[pushedIndex]
                : (existing.get(item)?.shift() ?? attempt.references.get(item))
        if (!reference) throw new Error('Native attachment mutation has an unstaged reference')
        slots.push(reference)
      }
      this.retainedBytes += bytes
      this.appendCount += parsed.length
      this.submit(author.owner, attempt, slots)
      attempt.slots.splice(0, attempt.slots.length, ...slots)
      attempt.target.length = candidate.length
      for (let index = 0; index < candidate.length; ++index) {
        if (Object.hasOwn(candidate, index)) {
          const item = candidate[index]
          if (item === undefined) throw new Error('Native attachment candidate changed shape')
          attempt.target[index] = item
        } else Reflect.deleteProperty(attempt.target, String(index))
      }
      return result
    } finally {
      attempt.editing = false
    }
  }
  private legacyPush(view: NativeTestAttachments, values: readonly NativeTestAttachment[]): number {
    const author = this.author.getStore()
    if (!author?.active()) throw new Error('Native attachment append has no active author callback')
    const context = this.context(author.owner),
      attempt = this.attempts.get(attemptKey(context))
    if (attempt?.view !== view)
      throw new Error('Native attachment append belongs to another actual attempt')
    // Validate the whole batch before any request or declaration mutation.
    const parsed = z.array(Declaration).max(128).parse(values)
    const bytes = parsed.reduce((total, item) => total + (item.body?.byteLength ?? 0), 0)
    if (
      this.appendCount + parsed.length > 128 ||
      this.entries.size + parsed.length > 384 ||
      bytes > 64 * 1024 * 1024 - this.retainedBytes
    )
      throw new Error('Native attachment declaration host capacity exceeded')
    const privateItems = parsed.map((item) =>
      Object.freeze({
        name: item.name,
        contentType: item.contentType,
        ...(item.path === undefined ? {} : { path: item.path }),
        ...(item.body === undefined ? {} : { body: Buffer.from(item.body) }),
      }),
    )
    const requests = z
      .array(Integer)
      .length(parsed.length)
      .parse(this.enqueue(author.owner, privateItems))
    if (
      new Set(requests).size !== requests.length ||
      requests.some((request) => this.entries.has(request))
    )
      throw new Error('Native attachment append has duplicate actual request identity')
    for (const [index, request] of requests.entries()) {
      const item = privateItems[index],
        original = values[index]
      if (!item || !original) throw new Error('Native attachment declaration batch changed shape')
      this.entries.set(request, {
        owner: author.owner,
        attempt,
        kind: 'append',
        expectedType: item.contentType,
        item,
        ...(item.body === undefined ? {} : { expectedBytes: item.body.byteLength }),
        state: 'declared',
      })
      attempt.target.push(original)
      attempt.references.set(original, `d${request}`)
      attempt.slots.push(`d${request}`)
    }
    this.retainedBytes += bytes
    this.appendCount += parsed.length
    return attempt.target.length
  }
  trackNormal(
    owner: NativeStepOwner,
    request: string,
    raw: { name: string; contentType?: string; body?: Uint8Array; path?: string },
    authorBody?: Uint8Array,
  ): void {
    const context = this.context(owner),
      attempt = this.attempts.get(attemptKey(context))
    const item = z
      .strictObject({
        name: z.string().max(4096),
        contentType: z.string().max(1024).optional(),
        body: z
          .instanceof(Uint8Array)
          .refine((value) => value.byteLength <= 16 * 1024 * 1024)
          .optional(),
        path: z.string().max(8192).optional(),
      })
      .refine((value) => value.body === undefined || value.path === undefined)
      .parse(raw)
    const actualRequest = Integer.parse(request),
      bytes = item.body?.byteLength ?? 0
    if (
      !attempt ||
      this.entries.has(actualRequest) ||
      this.entries.size >= 384 ||
      bytes > 64 * 1024 * 1024 - this.retainedBytes ||
      (authorBody !== undefined && (item.body === undefined || authorBody.byteLength !== bytes))
    )
      throw new Error('Native attachment view normal request catalog rejected')
    this.entries.set(actualRequest, {
      owner: Object.freeze(Owner.parse(owner)),
      attempt,
      kind: 'attach',
      ...(item.contentType === undefined ? {} : { expectedType: item.contentType }),
      item: Object.freeze({
        name: item.name,
        contentType: item.contentType ?? '',
        ...(item.body === undefined ? {} : { body: Buffer.from(item.body) }),
        ...(item.path === undefined ? {} : { path: item.path }),
      }),
      ...(item.body === undefined ? {} : { expectedBytes: bytes }),
      ...(authorBody === undefined ? {} : { authorBody }),
      state: 'declared',
    })
    this.retainedBytes += bytes
  }
  committed(owner: NativeStepOwner, request: string): boolean {
    this.context(owner)
    const entry = this.entries.get(Integer.parse(request))
    if (!entry || key(entry.owner) !== key(owner))
      throw new Error('Native attachment result has no actual request view')
    return entry.state === 'committed'
  }
  referenceAccepted(owner: NativeStepOwner, request: string): void {
    const entry = this.pending(owner, request)
    if (entry.kind !== 'append' || entry.expectedBytes !== undefined)
      throw new Error('Native body attachment cannot claim a declaration-only receipt')
    entry.state = 'referenceAccepted'
  }
  published(
    owner: NativeStepOwner,
    request: string,
    raw: { id: string; name: string; contentType: string; path: string; bytes: number },
    publicationTicket?: string,
  ): string | undefined {
    const entry = this.pending(owner, request)
    const receipt = z
      .strictObject({
        id: Integer,
        name: z.string().max(4096),
        contentType: z.string().max(1024),
        path: z
          .string()
          .max(8192)
          .startsWith('/')
          .refine((value) => !value.includes('\0')),
        bytes: z
          .number()
          .int()
          .min(0)
          .max(16 * 1024 * 1024),
      })
      .parse(raw)
    if (
      (entry.kind === 'append' && entry.expectedBytes === undefined) ||
      (entry.expectedBytes !== undefined && receipt.bytes !== entry.expectedBytes) ||
      receipt.name !== entry.item.name ||
      (entry.expectedType !== undefined && receipt.contentType !== entry.expectedType) ||
      this.artifacts.has(receipt.id)
    )
      throw new Error('Native attachment publication has no exact declared body artifact')
    this.artifacts.add(receipt.id)
    entry.state = 'committed'
    let mutationRequest: string | undefined
    if (entry.kind === 'attach') {
      const item = {
        name: receipt.name,
        contentType: receipt.contentType,
        ...(entry.item.body === undefined
          ? { path: receipt.path }
          : { body: entry.authorBody ?? Buffer.from(entry.item.body) }),
      }
      entry.attempt.references.set(item, `a${receipt.id}`)
      if (this.propose) {
        if (!publicationTicket) throw new Error('Native publication has no genuine view ticket')
        mutationRequest = this.submit(owner, entry.attempt, [], {
          ticket: Integer.parse(publicationTicket),
          artifact: receipt.id,
        })
      }
      entry.attempt.target.push(item)
      entry.attempt.slots.push(`a${receipt.id}`)
    }
    // Body entries keep their actual declared byte shape; only a distinct native
    // receipt carries the copied artifact path. No public implementation state.
    return mutationRequest
  }
  failed(owner: NativeStepOwner, request: string): void {
    this.pending(owner, request).state = 'failed'
  }
  canceled(owner: NativeStepOwner, request: string): void {
    this.pending(owner, request).state = 'canceled'
  }
  retire(owner: NativeStepOwner): void {
    this.context(owner)
    if (
      [...this.entries.values()].some(
        (entry) => key(entry.owner) === key(owner) && entry.state === 'declared',
      )
    )
      throw new Error('Native attachment view retired before actual declaration receipts')
    this.contexts.delete(key(owner))
  }
  private pending(owner: NativeStepOwner, request: string): Entry {
    this.context(owner)
    const entry = this.entries.get(Integer.parse(request))
    if (!entry || key(entry.owner) !== key(owner) || entry.state !== 'declared')
      throw new Error('Native attachment declaration receipt is stale, foreign or duplicate')
    return entry
  }
}
