// SPDX-License-Identifier: MIT
// Byte/path marshaling only. Native C++ owns file jobs, deadlines and artifacts.
import { Buffer } from 'node:buffer'
import { resolve } from 'node:path'
import { pid } from 'node:process'

import * as z from 'zod'

import {
  currentNativeRetryInvocation,
  nativeRetryInfrastructureFailure,
  type NativeRetryInvocation,
} from './native-callback-operations.js'
import { type NativeTestAttach } from './native-runner-attachment-types.js'
import {
  NativeAttachmentViews,
  type NativeTestAttachment,
  type NativeTestAttachments,
} from './native-runner-attachment-view.js'
import { type NativeStepOwner } from './native-runner-steps.js'
import { toError, type UntrustedInput } from './untrusted.js'

export type {
  NativeTestAttach,
  NativeTestAttachmentOptions,
} from './native-runner-attachment-types.js'

interface Pending {
  readonly owner: NativeStepOwner
  readonly retry?: NativeRetryInvocation
  readonly bytes?: Buffer
  readonly resolve: () => void
  readonly reject: (error: Error) => void
  readonly stepId?: string
  readonly append?: boolean
  readonly reference?: boolean
  promise?: Promise<void>
  testSkip?: { readonly request: string; readonly control: Error }
  localSkip?: { readonly request: string; readonly source: string; readonly control: Error }
  cancellationObserved?: boolean
  id?: string
  offset: number
  sequence: number
  awaitingChunk: boolean
  finished: boolean
  publicationMutation?: string
  publicationAccepted?: boolean
  publicationError?: Error
  resultReady?: boolean
}
interface MutationPending {
  readonly retry?: NativeRetryInvocation
  readonly owner: NativeStepOwner
  readonly expected: bigint
  readonly publicationArtifact?: string
  fileRequest?: string
}
const Integer = z
  .string()
  .regex(/^[1-9]\d{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const Token = z.union([z.literal('0'), Integer])
const retryFields = (retry: NativeRetryInvocation | undefined): string[] =>
  retry ? [retry.assertionID, retry.invocationNonce, retry.step] : []
const wire = (text: string): string => Buffer.from(text, 'utf16le').toString('hex') || '-'
const decode = (text: string): string => {
  const value = z
    .string()
    .max(32768)
    .regex(/^(?:-|(?:[a-f\d]{4})+)$/u)
    .parse(text)
  return value === '-' ? '' : Buffer.from(value, 'hex').toString('utf16le')
}
export class NativeAttachmentCallbacks {
  private nonce = 0n
  private retainedBytes = 0
  private readonly pending = new Map<string, Pending>()
  private readonly mutations = new Map<string, MutationPending>()
  private readonly callbackRetirements = new Map<string, () => void>()
  private readonly views: NativeAttachmentViews
  constructor(
    private readonly send: (...fields: (string | number)[]) => Promise<void>,
    private readonly fatal: (error: Error) => void,
    private readonly settled: () => void,
    identity: { readonly workerIndex: number; readonly pid: number } = { workerIndex: 1, pid },
    private readonly currentStep?: (owner: NativeStepOwner) => string,
  ) {
    this.views = new NativeAttachmentViews(
      identity.workerIndex,
      identity.pid,
      (owner, items) => this.append(owner, items),
      this.currentStep === undefined
        ? undefined
        : (owner, expected, slots, publication) =>
            this.propose(owner, expected, slots, publication),
    )
  }
  get idleReady(): boolean {
    return (
      this.pending.size === 0 &&
      this.mutations.size === 0 &&
      this.callbackRetirements.size === 0 &&
      this.views.contextCount === 0
    )
  }
  get pendingMutationRequests(): number {
    return this.mutations.size
  }
  private propose(
    owner: NativeStepOwner,
    expected: string,
    slots: readonly (string | null)[],
    publication?: { readonly ticket: string; readonly artifact: string },
  ): string {
    const actual = z
      .strictObject({ token: Token, scope: Integer, generation: Integer })
      .parse(owner)
    const revision = BigInt(Token.parse(expected))
    if (this.mutations.size >= 512 || this.nonce === 18446744073709551615n || revision >= 512n)
      throw new Error('Native attachment mutation pending request capacity exceeded')
    if (slots.length > 256) throw new Error('Native attachment mutation slot capacity exceeded')
    const references = slots.map((reference) =>
      reference === null
        ? 'h'
        : z
            .string()
            .regex(/^[da][1-9]\d{0,19}$/u)
            .parse(reference),
    )
    const publicationJob = publication
      ? [...this.pending.values()].find(
          (file) =>
            file.id === publication.artifact &&
            file.owner.token === actual.token &&
            file.owner.scope === actual.scope &&
            file.owner.generation === actual.generation,
        )
      : undefined
    if (
      publication &&
      (publicationJob?.id !== publication.artifact ||
        publicationJob.owner.token !== actual.token ||
        publicationJob.owner.scope !== actual.scope ||
        publicationJob.owner.generation !== actual.generation)
    )
      throw new Error('Native attachment publication lost its admitted job provenance')
    const retry = publication ? publicationJob?.retry : currentNativeRetryInvocation(actual)
    const step = publication ? '0' : Token.parse(this.currentStep?.(actual) ?? '0')
    const request = String(++this.nonce)
    this.mutations.set(request, {
      owner: actual,
      ...(retry ? { retry } : {}),
      expected: revision,
      ...(publication === undefined
        ? {}
        : { publicationArtifact: Integer.parse(publication.artifact) }),
    })
    void this.send(
      'ATTACH_MUTATE',
      request,
      actual.token,
      actual.scope,
      actual.generation,
      expected,
      step,
      publication ? 'P' : 'S',
      publication ? Integer.parse(publication.ticket) : '0',
      publication ? Integer.parse(publication.artifact) : references.join(',') || '-',
      ...retryFields(retry),
    ).catch((error: UntrustedInput) => this.fatal(toError(error)))
    return request
  }
  retireCallback(owner: NativeStepOwner, release: () => void): void {
    const identity = `${owner.token}:${owner.scope}:${owner.generation}`
    if (this.callbackRetirements.has(identity) || this.callbackRetirements.size >= 64)
      throw new Error('Native callback retirement is duplicate or exceeds capacity')
    if (!this.views.hasContext(owner)) release()
    else this.callbackRetirements.set(identity, release)
  }
  view(owner: NativeStepOwner): NativeTestAttachments {
    return this.views.view(owner)
  }
  run<T>(owner: NativeStepOwner, active: () => boolean, callback: () => T): T {
    return this.views.run(owner, active, callback)
  }
  private append(
    owner: NativeStepOwner,
    items: readonly NativeTestAttachment[],
  ): readonly string[] {
    const bytes = items.reduce((sum, item) => sum + (item.body?.byteLength ?? 0), 0)
    if (
      this.pending.size + items.length > 128 ||
      bytes > 64 * 1024 * 1024 - this.retainedBytes ||
      this.nonce > 18446744073709551615n - BigInt(items.length)
    )
      throw new Error('Native append pending byte/request capacity exceeded')
    const retry = currentNativeRetryInvocation(owner)
    const requests: string[] = []
    for (const item of items) {
      let promise: Promise<void>
      if (item.body !== undefined)
        promise = this.bindOwned(
          owner,
          () => true,
          undefined,
          true,
          item.path,
        )(item.name, { body: item.body, contentType: item.contentType })
      else {
        const request = String(++this.nonce)
        promise = new Promise<void>((resolvePromise, reject) => {
          this.pending.set(request, {
            owner,
            ...(retry ? { retry } : {}),
            append: true,
            reference: true,
            resolve: resolvePromise,
            reject,
            offset: 0,
            sequence: 0,
            awaitingChunk: false,
            finished: true,
          })
          void this.send(
            'ATTACH_APPEND_BEGIN',
            request,
            owner.token,
            owner.scope,
            owner.generation,
            wire(item.name),
            item.path === undefined ? 'N' : 'R',
            0,
            wire(item.contentType),
            item.path === undefined ? '-' : wire(item.path),
            ...retryFields(retry),
          ).catch((error: UntrustedInput) => this.fatal(toError(error)))
        })
        const pending = this.pending.get(request)
        if (!pending) throw new Error('Native append reference disappeared before actual send')
        pending.promise = promise
      }
      requests.push(String(this.nonce))
      // push() has no returned Promise. This observes only this actual append
      // request; C++ retains its error and writer/rollback fence independently.
      void promise.catch(() => undefined)
    }
    return requests
  }
  get physical(): number {
    return this.pending.size
  }
  bind(owner: NativeStepOwner, active: () => boolean): NativeTestAttach {
    return this.bindOwned(owner, active)
  }
  bindStep(owner: NativeStepOwner, stepId: string, active: () => boolean): NativeTestAttach {
    return this.bindOwned(owner, active, Integer.parse(stepId))
  }
  private bindOwned(
    owner: NativeStepOwner,
    active: () => boolean,
    stepId?: string,
    append = false,
    appendPath?: string,
  ): NativeTestAttach {
    const actualOwner = z
      .strictObject({ token: Token, scope: Integer, generation: Integer })
      .parse(owner)
    if (appendPath !== undefined) z.string().max(8192).parse(appendPath)
    return (name, options = {}) => {
      const retry = currentNativeRetryInvocation(actualOwner)
      if (typeof name !== 'string') throw new TypeError('"name" should be string.')
      const actualName = z.string().max(4096).parse(name)
      const actual = z
        .strictObject({
          body: z.union([z.string(), z.instanceof(Uint8Array)]).optional(),
          path: z
            .string()
            .max(8192)
            .refine((value) => !value.includes('\0'))
            .optional(),
          contentType: z.string().max(1024).optional(),
        })
        .parse(options)
      if (actual.path !== undefined && actual.body !== undefined)
        throw new Error('Exactly one of "path" and "body" must be specified')
      if (!active()) throw new Error('Native attachment owning callback is no longer active')
      const kind =
        actual.path !== undefined
          ? 'P'
          : typeof actual.body === 'string' || actual.body === undefined
            ? 'S'
            : 'B'
      const size =
        kind === 'P'
          ? 0
          : typeof actual.body === 'string'
            ? Buffer.byteLength(actual.body)
            : (actual.body?.byteLength ?? 0)
      if (
        size > 16 * 1024 * 1024 ||
        this.retainedBytes + size > 64 * 1024 * 1024 ||
        this.pending.size >= 128 ||
        this.nonce === 18446744073709551615n
      )
        throw new Error('Native attachment host byte/request capacity exceeded')
      const path =
        actual.path === undefined || actual.path === '' ? (actual.path ?? '') : resolve(actual.path)
      if (Buffer.byteLength(path) > 8192)
        throw new Error('Native attachment source path exceeds bound')
      // Snapshot the author's actual bytes before returning; later author writes
      // cannot change an admitted attachment's byte sequence.
      const bytes =
        kind === 'P'
          ? undefined
          : typeof actual.body === 'string'
            ? Buffer.from(actual.body)
            : Buffer.from(actual.body ?? [])
      if (!append) this.views.prepareNormal(actualOwner)
      if (this.nonce === 18446744073709551615n)
        throw new Error('Native attachment host request capacity exceeded')
      const request = String(this.nonce + 1n)
      if (!append)
        this.views.trackNormal(
          actualOwner,
          request,
          {
            name: actualName,
            ...(actual.contentType === undefined ? {} : { contentType: actual.contentType }),
            ...(bytes === undefined ? { path: actual.path ?? '' } : { body: bytes }),
          },
          typeof actual.body === 'object' ? actual.body : undefined,
        )
      ++this.nonce
      const promise = new Promise<void>((resolvePromise, reject) => {
        this.pending.set(request, {
          owner: actualOwner,
          ...(retry ? { retry } : {}),
          ...(append ? { append: true } : {}),
          ...(stepId === undefined ? {} : { stepId }),
          ...(bytes === undefined ? {} : { bytes }),
          resolve: resolvePromise,
          reject,
          offset: 0,
          sequence: 0,
          awaitingChunk: false,
          finished: false,
        })
        this.retainedBytes += size
        void this.send(
          append
            ? 'ATTACH_APPEND_BEGIN'
            : stepId === undefined
              ? 'ATTACH_BEGIN'
              : 'ATTACH_STEP_BEGIN',
          request,
          actualOwner.token,
          actualOwner.scope,
          actualOwner.generation,
          ...(stepId === undefined ? [] : [stepId]),
          wire(actualName),
          append && appendPath !== undefined ? 'D' : kind,
          size,
          actual.contentType === undefined ? '~' : wire(actual.contentType),
          append && appendPath !== undefined
            ? wire(appendPath)
            : Buffer.from(path).toString('hex') || '-',
          ...retryFields(retry),
        ).catch((error: UntrustedInput) => this.fatal(toError(error)))
      })
      const pending = this.pending.get(request)
      if (!pending) throw new Error('Native attachment request disappeared before actual send')
      pending.promise = promise
      return promise
    }
  }
  // Called only by the owning host callback after authenticated native SKIP_RESULT.
  // Ordinary/rejected skip requests never install these cancellation observers.
  acceptTestSkip(owner: NativeStepOwner, request: string, control: Error): void {
    const actual = z
      .strictObject({ token: Token, scope: Integer, generation: Integer })
      .parse(owner)
    const cause = Integer.parse(request)
    for (const pending of this.pending.values()) {
      if (
        pending.owner.token !== actual.token ||
        pending.owner.scope !== actual.scope ||
        pending.owner.generation !== actual.generation
      )
        continue
      if (pending.testSkip || !pending.promise)
        throw new Error('Native attachment cancellation cause is duplicate or unowned')
      pending.testSkip = { request: cause, control }
      this.observeAcceptedCancellation(pending)
    }
  }
  // Native ACK supplies the exact source and native outcome step IDs. This
  // performs no ancestry calculation and never treats arbitrary errors as skip.
  acceptLocalSkip(
    owner: NativeStepOwner,
    request: string,
    source: string,
    actualStepIds: readonly string[],
    control: Error,
  ): void {
    const actual = z
      .strictObject({ token: Token, scope: Integer, generation: Integer })
      .parse(owner)
    const cause = Integer.parse(request),
      root = Integer.parse(source)
    const ids = z.array(Integer).min(1).max(64).parse(actualStepIds)
    if (!ids.includes(root) || new Set(ids).size !== ids.length)
      throw new Error('Native attachment local-skip ACK has invalid actual step IDs')
    for (const pending of this.pending.values()) {
      if (
        pending.owner.token !== actual.token ||
        pending.owner.scope !== actual.scope ||
        pending.owner.generation !== actual.generation ||
        pending.stepId === undefined ||
        !ids.includes(pending.stepId)
      )
        continue
      if (pending.testSkip || pending.localSkip || !pending.promise)
        throw new Error('Native attachment local-skip cause is duplicate or unowned')
      pending.localSkip = { request: cause, source: root, control }
      this.observeAcceptedCancellation(pending)
    }
  }
  packet(fields: readonly string[]): boolean {
    if (fields[0] === 'ATTACH_MUTATION_RESULT') {
      const [request, token, scope, generation, revision, ok, error, classification] = z
        .tuple([
          Integer,
          Token,
          Integer,
          Integer,
          Token,
          z.enum(['0', '1']),
          z.string(),
          z.literal('I').optional(),
        ])
        .parse(fields.slice(1))
      const pending = this.mutations.get(request)
      if (
        pending?.owner.token !== token ||
        pending.owner.scope !== scope ||
        pending.owner.generation !== generation
      )
        throw new Error('Native attachment mutation result has a foreign request owner')
      if (Boolean(pending.retry && ok === '0') !== Boolean(classification))
        throw new Error('Native attachment mutation retry failure lacks native provenance')
      if (classification) nativeRetryInfrastructureFailure(new Error(decode(error)), pending.retry)
      const observed = BigInt(revision)
      if (
        (ok === '1' && (observed !== pending.expected + 1n || error !== '-')) ||
        (ok === '0' && (observed > pending.expected || error === '-'))
      )
        throw new Error('Native attachment mutation result contradicts its native revision')
      this.mutations.delete(request)
      if (pending.publicationArtifact !== undefined) {
        const file =
          pending.fileRequest === undefined ? undefined : this.pending.get(pending.fileRequest)
        if (file?.publicationMutation !== request || file.id !== pending.publicationArtifact)
          throw new Error('Native publication view ACK has no exact actual file request')
        file.publicationAccepted = ok === '1'
        if (ok === '0') file.publicationError = new Error(decode(error))
        if (file.resultReady) {
          if (pending.fileRequest === undefined)
            throw new Error('Native publication view lost its file request')
          this.finish(pending.fileRequest, file, file.publicationError)
        }
      }
      // Synchronous authored mutations have no Promise. C++ retains every
      // native rejection as the owning callback/report cause independently.
      this.settled()
      return true
    }

    if (fields[0] === 'ATTACH_CONTEXT') {
      const [token, scope, generation, testId, attemptGeneration, workerIndex, actualPid] = z
        .tuple([
          Token,
          Integer,
          Integer,
          Integer,
          Integer,
          z.coerce.number().int().positive(),
          z.coerce.number().int().positive(),
        ])
        .parse(fields.slice(1))
      this.views.open({
        token,
        scope,
        generation,
        testId,
        attemptGeneration,
        workerIndex,
        pid: actualPid,
      })
      return true
    }
    if (fields[0] === 'ATTACH_CONTEXT_RETIRED') {
      const [token, scope, generation] = z.tuple([Token, Integer, Integer]).parse(fields.slice(1))
      if (
        [...this.mutations.values()].some(
          (entry) =>
            entry.owner.token === token &&
            entry.owner.scope === scope &&
            entry.owner.generation === generation,
        )
      )
        throw new Error('Native attachment context retired before actual mutation receipts')
      this.views.retire({ token, scope, generation })
      const identity = `${token}:${scope}:${generation}`,
        release = this.callbackRetirements.get(identity)
      if (release) {
        this.callbackRetirements.delete(identity)
        release()
      }
      return true
    }
    if (
      fields[0] === 'ATTACH_REFERENCE_ACCEPTED' ||
      fields[0] === 'ATTACH_APPEND_REJECTED' ||
      fields[0] === 'ATTACH_PUBLISHED'
    ) {
      const [request, token, scope, generation, id] = z
        .tuple([Integer, Token, Integer, Integer, Integer])
        .parse(fields.slice(1, 6))
      const pending = this.pending.get(request),
        owner = { token, scope, generation }
      if (
        pending?.owner.token !== token ||
        pending.owner.scope !== scope ||
        pending.owner.generation !== generation
      )
        throw new Error('Native attachment declaration receipt has a foreign request owner')
      if (fields[0] === 'ATTACH_REFERENCE_ACCEPTED') {
        z.tuple([]).parse(fields.slice(6))
        if (!pending.append || !pending.reference)
          throw new Error('Native reference acceptance has no actual reference declaration')
        this.views.referenceAccepted(owner, request)
        this.finish(request, pending)
      } else if (fields[0] === 'ATTACH_APPEND_REJECTED') {
        const [error, classification] = z
          .tuple([z.string(), z.literal('O').optional()])
          .parse(fields.slice(6))
        if (Boolean(pending.retry) !== Boolean(classification))
          throw new Error('Native append rejection lacks exact retry classification')
        if (!pending.append || pending.id !== undefined)
          throw new Error('Native append rejection has contradictory admission')
        this.views.failed(owner, request)
        this.finish(request, pending, decode(error))
      } else {
        const extra = pending.stepId === undefined ? 0 : 1
        if (extra && pending.stepId !== Integer.parse(fields[6]))
          throw new Error('Native publication has a foreign actual step')
        const [name, contentType, path, bytes, viewTicket] = z
          .tuple([
            z.string(),
            z.string(),
            z.string(),
            z.coerce
              .number()
              .int()
              .min(0)
              .max(16 * 1024 * 1024),
            Token.optional(),
          ])
          .parse(fields.slice(6 + extra))
        if (pending.reference || pending.id !== id || !pending.finished)
          throw new Error('Native publication lacks actual file/byte admission')
        if (
          (pending.append && viewTicket !== undefined && viewTicket !== '0') ||
          (this.currentStep !== undefined &&
            !pending.append &&
            (viewTicket === undefined || viewTicket === '0'))
        )
          throw new Error('Native publication has contradictory actual view ticket')
        const mutation = this.views.published(
          owner,
          request,
          {
            id,
            name: decode(name),
            contentType: decode(contentType),
            path: decode(path),
            bytes,
          },
          viewTicket === '0' ? undefined : viewTicket,
        )
        if (mutation !== undefined) {
          const proposal = this.mutations.get(mutation)
          if (proposal?.publicationArtifact !== id)
            throw new Error('Native publication reflection lost its exact proposal request')
          proposal.fileRequest = request
          pending.publicationMutation = mutation
        }
      }
      return true
    }
    const stepReceipt = fields[0]?.startsWith('ATTACH_STEP_') ?? false
    const command = stepReceipt ? fields[0]?.replace('ATTACH_STEP_', 'ATTACH_') : fields[0]
    if (
      command !== 'ATTACH_ADMITTED' &&
      command !== 'ATTACH_CHUNK_OK' &&
      command !== 'ATTACH_RESULT' &&
      command !== 'ATTACH_TEST_SKIP_RESULT' &&
      command !== 'ATTACH_LOCAL_SKIP_RESULT'
    )
      return false
    const [request, token, scope, generation, id] = z
      .tuple([Integer, Token, Integer, Integer, z.union([z.literal('0'), Integer])])
      .parse(fields.slice(1, 6))
    const pending = this.pending.get(request)
    if (
      pending?.owner.token !== token ||
      pending.owner.scope !== scope ||
      pending.owner.generation !== generation ||
      (pending.id !== undefined && pending.id !== id)
    )
      throw new Error('Native attachment receipt has stale request/owner identity')
    const extra = stepReceipt ? 1 : 0
    if (
      (pending.stepId !== undefined) !== stepReceipt ||
      (stepReceipt && pending.stepId !== Integer.parse(fields[6]))
    )
      throw new Error('Native attachment receipt has stale actual step identity')
    if (command === 'ATTACH_LOCAL_SKIP_RESULT') {
      const [cause, source] = z
        .tuple([Integer, Integer, z.literal('-')])
        .parse(fields.slice(6 + extra))
      if (
        !stepReceipt ||
        id === '0' ||
        pending.localSkip?.request !== cause ||
        pending.localSkip.source !== source
      )
        throw new Error('Native attachment terminal local-skip cause has no accepted mutation')
      this.views.canceled({ token, scope, generation }, request)
      this.finish(request, pending, pending.localSkip.control)
    } else if (command === 'ATTACH_TEST_SKIP_RESULT') {
      const [cause] = z.tuple([Integer, z.literal('-')]).parse(fields.slice(6 + extra))
      if (id === '0' || pending.testSkip?.request !== cause)
        throw new Error('Native attachment terminal test-skip cause has no accepted mutation')
      this.views.canceled({ token, scope, generation }, request)
      this.finish(request, pending, pending.testSkip.control)
    } else if (command === 'ATTACH_CHUNK_OK') {
      const [sequence] = z
        .tuple([z.coerce.number().int().min(1).max(1024)])
        .parse(fields.slice(6 + extra))
      if (
        id === '0' ||
        pending.id === undefined ||
        !pending.awaitingChunk ||
        pending.sequence !== sequence
      )
        throw new Error('Native attachment chunk ACK has no physical write admission')
      pending.awaitingChunk = false
      this.next(pending)
    } else {
      const [ok, error, classification] = z
        .tuple([z.enum(['0', '1']), z.string(), z.enum(['O', 'I']).optional()])
        .parse(fields.slice(6 + extra))
      if (classification && (!pending.retry || ok === '1'))
        throw new Error('Native retry file classification lacks its admitted failed job')
      if (pending.retry && ok === '0' && !classification)
        throw new Error('Native retry file failure lacks native classification')
      const actualFailure = ok === '0' ? new Error(decode(error)) : undefined
      if (classification === 'I') nativeRetryInfrastructureFailure(actualFailure, pending.retry)
      if (ok === '1' && error !== '-')
        throw new Error('Native attachment success has contradictory error metadata')
      if (command === 'ATTACH_ADMITTED') {
        if (pending.id !== undefined || (ok === '1') !== (id !== '0'))
          throw new Error('Native attachment admission shape is inconsistent')
        if (ok === '1') {
          pending.id = id
          this.next(pending)
        } else {
          this.views.failed({ token, scope, generation }, request)
          this.finish(request, pending, actualFailure)
        }
      } else {
        if (id === '0' || (ok === '1' && (pending.id === undefined || !pending.finished)))
          throw new Error('Native attachment result lacks genuine admission/byte completion')
        if (ok === '1' && !this.views.committed({ token, scope, generation }, request))
          throw new Error('Native attachment success has no committed publication receipt')
        if (ok === '0') this.views.failed({ token, scope, generation }, request)
        if (ok === '1' && pending.publicationMutation !== undefined) {
          pending.resultReady = true
          if (pending.publicationAccepted !== undefined)
            this.finish(request, pending, pending.publicationError)
        } else this.finish(request, pending, actualFailure)
      }
    }
    return true
  }
  private observeAcceptedCancellation(pending: Pending): void {
    if (pending.cancellationObserved) return
    if (!pending.promise) throw new Error('Native file cancellation has no actual pending API')
    pending.cancellationObserved = true
    void pending.promise.catch((error: UntrustedInput) => {
      if (error !== pending.testSkip?.control && error !== pending.localSkip?.control)
        this.fatal(toError(error))
    })
  }
  private next(pending: Pending): void {
    if (pending.testSkip || pending.localSkip) return
    if (pending.finished || pending.awaitingChunk || !pending.id)
      throw new Error('Native attachment byte continuation is duplicate/stale')
    if (pending.bytes === undefined) {
      pending.finished = true
      return
    }
    const owner = pending.owner
    if (pending.offset === pending.bytes.length) {
      pending.finished = true
      void this.send('ATTACH_FINISH', pending.id, owner.token, owner.scope, owner.generation).catch(
        (error: UntrustedInput) => this.fatal(toError(error)),
      )
    } else {
      const end = Math.min(pending.offset + 16384, pending.bytes.length)
      const actual = pending.bytes.subarray(pending.offset, end)
      pending.offset = end
      pending.awaitingChunk = true
      void this.send(
        'ATTACH_CHUNK',
        pending.id,
        owner.token,
        owner.scope,
        owner.generation,
        ++pending.sequence,
        actual.toString('hex'),
      ).catch((error: UntrustedInput) => this.fatal(toError(error)))
    }
  }
  private finish(request: string, pending: Pending, error?: string | Error): void {
    this.pending.delete(request)
    this.retainedBytes -= pending.bytes?.length ?? 0
    if (error === undefined) pending.resolve()
    else pending.reject(error instanceof Error ? error : new Error(error))
    this.settled()
  }
}
