// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { NativeBrowserCapabilitiesSchema } from './native-browser-capabilities.js'
import { NativeObservedHeadersSchema } from './native-network-headers.js'
import { NativePageRouteMutationSchema } from './native-page-route-removal-protocol.js'
import { NativeVideoDescriptorSchema } from './native-video-protocol.js'
import { NativeWebSocketInfoSchema } from './native-websocket-protocol.js'
import { NativeWebSocketRouteEnvelopeSchema } from './native-websocket-route-protocol.js'
import { NativeWorkWatermarkSchema } from './native-work-watermark.js'
import { NativeWorkerControlSchema, NativeWorkerInfoSchema } from './native-worker-protocol.js'
import { type UntrustedInput } from './untrusted.js'

export const NATIVE_PROTOCOL_VERSION = 1
export const MAX_NATIVE_PAYLOAD = 3840 * 2160 * 4
export const MAX_NATIVE_RESPONSE_PAYLOAD = 192 * 1024 * 1024
const MAX_HEADER = 65_536
const MAX_REENTRANT_CHUNKS = 4096
const MAX_REENTRANT_BYTES = MAX_NATIVE_RESPONSE_PAYLOAD + MAX_HEADER + 4

const base = {
  version: z.literal(NATIVE_PROTOCOL_VERSION),
  pageId: z.number().int().positive().optional(),
  bytes: z.number().int().min(0).max(MAX_NATIVE_PAYLOAD),
}
const timestamp = z.number().finite().nonnegative()
export const NativeFrameInfoSchema = z
  .strictObject({
    id: z.string().min(1).max(128),
    parentId: z.string().max(128),
    name: z.string().max(1024).optional(),
    nameJSON: z
      .string()
      .max(1024 * 6 + 2)
      .optional(),
    url: z.string().max(8192),
    navigationSequence: z.number().int().nonnegative().optional(),
  })
  .transform((value, context) => {
    const { name: regular, nameJSON: encoded, ...frame } = value
    if ((regular === undefined) === (encoded === undefined)) {
      context.addIssue({ code: 'custom', message: 'Invalid native frame name wire identity' })
      return z.NEVER
    }
    let name: UntrustedInput = regular
    if (encoded !== undefined) {
      try {
        name = JSON.parse(encoded) as UntrustedInput
      } catch {
        context.addIssue({ code: 'custom', message: 'Invalid native frame name JSON string' })
        return z.NEVER
      }
    }
    if (typeof name !== 'string' || name.length > 1024) {
      context.addIssue({
        code: 'custom',
        message: 'Invalid native frame name string or unit limit',
      })
      return z.NEVER
    }
    return { ...frame, name }
  })
export type NativeFrameInfo = z.infer<typeof NativeFrameInfoSchema>
export const NativeAutomationEventSchema = z.strictObject({
  event: z.enum(['frameattached', 'framenavigated', 'framedetached']),
  frame: NativeFrameInfoSchema,
  sequence: z.number().int().positive().optional(),
})
export type NativeAutomationEvent = z.infer<typeof NativeAutomationEventSchema>
export const NativeConsoleInfoSchema = z.strictObject({
  sequence: z.number().int().positive().optional(),
  worker: z.string().min(1).max(128).optional(),
  workerInfo: NativeWorkerInfoSchema.optional(),
  type: z.string().min(1).max(64),
  text: z.string().max(1_000_000),
  timestamp: z.number().finite().nonnegative().optional(),
  arguments: z
    .array(
      z.strictObject({
        handle: z.number().int().positive(),
        guid: z.string().max(256),
        preview: z.string().max(8192),
        node: z.boolean(),
        frame: z.string().max(128).optional(),
        worker: z.string().min(1).max(128).optional(),
        workerControl: NativeWorkerControlSchema.optional(),
      }),
    )
    .max(64)
    .optional(),
  argumentsError: z.string().max(512).optional(),
  location: z.strictObject({
    line: z.number().int().nonnegative().optional(),
    url: z.string().max(8192),
    lineNumber: z.number().int().nonnegative(),
    columnNumber: z.number().int().nonnegative(),
  }),
})
export type NativeConsoleInfo = z.infer<typeof NativeConsoleInfoSchema>
// Inline images/fonts are genuine browser requests. Their observed data URLs
// can exceed the command URL limit; retain their identity without making
// ordinary network URLs or observed inline resources unbounded.
const NativeObservedNetworkURLSchema = z
  .string()
  .max(1_048_576)
  .refine((value) => value.length <= 8192 || value.startsWith('data:'), {
    message: 'Observed non-data network URLs must not exceed 8192 characters',
  })
export const NativeNetworkRequestSchema = z.object({
  id: z.string().min(1).max(256),
  requestId: z.string().max(256),
  session: z.string().max(256),
  frameId: z.string().max(128),
  url: NativeObservedNetworkURLSchema,
  method: z.string().max(128),
  resourceType: z.string().max(128),
  postDataBodyId: z
    .string()
    .regex(/^[1-9][0-9]{0,19}$/u)
    .optional(),
  postDataOwner: z
    .string()
    .regex(/^(?:page|broker):[1-9][0-9]*$/u)
    .max(128)
    .optional(),
  postDataBase64: z.string().max(2_796_204).optional(),
  postDataUnavailable: z.boolean().optional(),
  headers: NativeObservedHeadersSchema,
  headersReady: z.boolean().optional(),
  postData: z.string().max(1_048_576).optional(),
  isNavigationRequest: z.boolean(),
  serviceWorker: z.boolean().optional(),
  workerInfo: NativeWorkerInfoSchema.optional(),
  contextWorker: z.boolean().optional(),
  navigationFrameUnavailable: z.boolean().optional(),
  blockedReason: z.string().max(128).optional(),
  timing: z.record(z.string(), z.number()).optional(),
  redirectedFrom: z.string().max(256).optional(),
})
export const NativeResponseServerAddrSchema = z.strictObject({
  ipAddress: z.string().min(1).max(128),
  port: z.number().int().min(1).max(65535),
})
export const NativeResponseSecurityDetailsSchema = z.strictObject({
  issuer: z.string().max(8192),
  protocol: z.string().max(128),
  subjectName: z.string().max(8192),
  validFrom: z.number(),
  validTo: z.number(),
})
export const NativeNetworkResponseSchema = z.object({
  headersReady: z.boolean().optional(),
  url: NativeObservedNetworkURLSchema,
  status: z.number().int().min(0).max(999),
  statusText: z.string().max(1024),
  headers: NativeObservedHeadersSchema,
  requestId: z.string().max(256),
  session: z.string().max(256),
  frameId: z.string().max(128),
  method: z.string().max(128),
  fromServiceWorker: z.boolean().optional(),
  mimeType: z.string().optional(),
  serverAddr: NativeResponseServerAddrSchema.optional(),
  securityDetails: NativeResponseSecurityDetailsSchema.optional(),
  protocol: z.string().max(128).optional(),
  timing: z.record(z.string(), z.number()).optional(),
})
export const NativeNetworkInfoSchema = z.discriminatedUnion('event', [
  NativePageRouteMutationSchema,
  z.object({
    event: z.literal('routematch'),
    sequence: z.number().int().positive().max(Number.MAX_SAFE_INTEGER),
    matcher: z.strictObject({
      id: z.string().min(1).max(128),
      registrationId: z.string().min(1).max(128),
      url: z.string().min(1).max(8192),
      callbackScope: z.string().regex(/^[1-9][0-9]{0,19}$/u),
      callbackToken: z.string().regex(/^[1-9][0-9]{0,19}$/u),
    }),
  }),
  z.object({
    event: z.enum([
      'request',
      'response',
      'requestfinished',
      'requestfailed',
      'requestupdated',
      'route',
    ]),
    sequence: z.number().int().positive(),
    request: NativeNetworkRequestSchema,
    response: NativeNetworkResponseSchema.optional(),
    errorText: z.string().max(4096).optional(),
    route: z
      .object({
        id: z.string().max(128),
        registrationId: z.string().max(128),
        callbackScope: z
          .string()
          .regex(/^[1-9][0-9]{0,19}$/u)
          .optional(),
        callbackToken: z
          .string()
          .regex(/^[1-9][0-9]{0,19}$/u)
          .optional(),
        exhausted: z.boolean().optional(),
      })
      .optional(),
  }),
  z.object({
    event: z.literal('routeexpired'),
    sequence: z.number().int().positive(),
    routeId: z.string().max(128),
  }),
  z.object({
    event: z.literal('routeerror'),
    sequence: z.number().int().positive(),
    errorText: z.string().max(4096),
  }),
])
export type NativeNetworkInfo = z.infer<typeof NativeNetworkInfoSchema>
export type NativeNetworkRequestInfo = z.infer<typeof NativeNetworkRequestSchema>
export type NativeNetworkResponseInfo = z.infer<typeof NativeNetworkResponseSchema>
export const NativeContextEventSchema = z.strictObject({
  event: z.enum(['page-created', 'page-closed', 'context-closed']),
  contextId: z.number().int().positive(),
  sequence: z.number().int().positive().optional(),
  pageId: z.number().int().positive().optional(),
  openerId: z.number().int().positive().optional(),
  closedReason: z.string().max(16384).optional(),
  video: NativeVideoDescriptorSchema.optional(),
})
export type NativeContextEvent = z.infer<typeof NativeContextEventSchema>
export const NativeActionStageSchema = z.strictObject({
  clientToken: z.string().min(1).max(128),
  id: z.number().int().positive(),
  generation: z.number().int().positive(),
  token: z.number().int().positive(),
  stage: z.enum(['beforeStable', 'beforeScreenshot']),
})
export type NativeActionStage = z.infer<typeof NativeActionStageSchema>
export const NativeLocatorHandlerStageSchema = NativeActionStageSchema.omit({ stage: true }).extend(
  {
    stage: z.enum(['locatorHandlerRun', 'locatorHandlerAbort', 'locatorHandlerRetired']),
    handlerId: z.number().int().positive(),
    removed: z.boolean().optional(),
  },
)
export type NativeLocatorHandlerStage = z.infer<typeof NativeLocatorHandlerStageSchema>

export const NativeURLPredicateSchema = z.strictObject({
  clientToken: z.string().min(1).max(128),
  id: z.number().int().positive(),
  generation: z.number().int().positive(),
  token: z.number().int().positive(),
  url: z.string().max(8192),
})
export type NativeURLPredicate = z.infer<typeof NativeURLPredicateSchema>
export const NativeEventPredicateHeaderSchema = NativeActionStageSchema.omit({
  stage: true,
}).extend({
  event: z.enum([
    'console',
    'pageerror',
    'crash',
    'request',
    'response',
    'requestfinished',
    'requestfailed',
    'frameattached',
    'framenavigated',
    'framedetached',
    'dialog',
    'dialogclosed',
    'filechooser',
    'download',
    'websocket',
    'framesent',
    'framereceived',
    'socketerror',
    'close',
  ]),
  sequence: z.number().int().positive(),
})
export const NativeDialogInfoSchema = z.strictObject({
  state: z.enum(['opened', 'closed']).default('opened'),
  id: z.string().min(1).max(32),
  type: z.enum(['alert', 'confirm', 'prompt', 'beforeunload']),
  url: z.string().max(8192),
  message: z.string().max(524288),
  defaultValue: z.string().max(262144),
})
export type NativeDialogInfo = z.infer<typeof NativeDialogInfoSchema>
export const NativeFileChooserInfoSchema = z.strictObject({
  id: z.string().min(1).max(256),
  frame: z.string().min(1).max(128),
  backendNodeId: z.number().int().positive(),
  multiple: z.boolean(),
})
export type NativeFileChooserInfo = z.infer<typeof NativeFileChooserInfoSchema>
export const NativePacketSchema = z.discriminatedUnion('type', [
  NativeWorkWatermarkSchema,
  z.strictObject({
    ...base,
    type: z.literal('ready'),
    capabilities: NativeBrowserCapabilitiesSchema.optional(),
    video: NativeVideoDescriptorSchema.optional(),
    timestampMs: timestamp,
    cefVersion: z.string().max(128),
    captureWidth: z.number().int().min(1).max(3840).optional(),
    captureHeight: z.number().int().min(1).max(2160).optional(),
    viewport: z
      .strictObject({ width: z.number().int().positive(), height: z.number().int().positive() })
      .nullable()
      .optional(),
    mainFrame: NativeFrameInfoSchema.optional(),
  }),
  z
    .strictObject({
      ...base,
      type: z.literal('actionstage'),
      ...NativeActionStageSchema.omit({ stage: true }).shape,
      stage: z.enum([
        'beforeStable',
        'beforeScreenshot',
        'locatorHandlerRun',
        'locatorHandlerAbort',
        'locatorHandlerRetired',
      ]),
      handlerId: z.number().int().positive().optional(),
      removed: z.boolean().optional(),
    })
    .superRefine((value, context) => {
      const handler = value.stage.startsWith('locatorHandler')
      if (value.removed !== undefined && value.stage !== 'locatorHandlerRetired')
        context.addIssue({
          code: 'custom',
          message: 'Native locator removal receipt outside retirement',
        })
      if (handler !== (value.handlerId !== undefined))
        context.addIssue({
          code: 'custom',
          message: 'Native action stage handler identity mismatch',
        })
    }),
  z.strictObject({ ...base, type: z.literal('urlpredicate'), ...NativeURLPredicateSchema.shape }),
  z.strictObject({
    ...base,
    type: z.literal('eventpredicate'),
    ...NativeEventPredicateHeaderSchema.shape,
  }),
  z.strictObject({
    ...base,
    type: z.literal('websocket'),
    bytes: z.number().int().min(0).max(2_097_152),
    socket: NativeWebSocketInfoSchema,
  }),
  z.strictObject({ ...base, type: z.literal('pageerror'), sequence: z.number().int().positive() }),
  z.strictObject({
    ...base,
    type: z.literal('network'),
    sequence: z.number().int().positive().optional(),
  }),
  z.strictObject({
    ...base,
    type: z.literal('networkbody'),
    bodyId: z.string().regex(/^[1-9][0-9]{0,19}$/u),
    offset: z.number().int().min(0).max(2_097_152),
    totalBytes: z.number().int().min(0).max(2_097_152),
    canceled: z.boolean().optional(),
  }),
  z.strictObject({ ...base, type: z.literal('httplookup') }),
  z.strictObject({ ...base, type: z.literal('binding') }),
  z.strictObject({ ...base, type: z.literal('worker'), sequence: z.number().int().positive() }),
  z.strictObject({ ...base, type: z.literal('context') }),
  z.strictObject({ ...base, type: z.literal('contextconsole') }),
  z.strictObject({ ...base, type: z.literal('contextworker') }),
  z.strictObject({ ...base, type: z.literal('contextworkerpredicate') }),
  z.strictObject({ ...base, type: z.literal('contextnetwork') }),
  z.strictObject({ ...base, type: z.literal('contextroute') }),
  z.strictObject({
    ...base,
    ...NativeWebSocketRouteEnvelopeSchema.shape,
    type: z.literal('websocketroute'),
    bytes: z.number().int().min(0).max(2_097_152),
  }),
  z.strictObject({
    ...base,
    type: z.literal('contextroutemutation'),
    bytes: z.number().int().positive().max(65_536),
  }),
  z.strictObject({
    ...base,
    type: z.literal('contextnetworkbody'),
    contextId: z.number().int().positive(),
    owner: z
      .string()
      .regex(/^broker:[1-9][0-9]*$/u)
      .max(128),
    bodyId: z.string().regex(/^[1-9][0-9]{0,19}$/u),
    offset: z.number().int().min(0).max(2_097_152),
    totalBytes: z.number().int().min(0).max(2_097_152),
    canceled: z.boolean().optional(),
  }),
  z.strictObject({
    ...base,
    type: z.literal('filechooser'),
    sequence: z.number().int().positive(),
  }),
  z.strictObject({ ...base, type: z.literal('recording') }),
  z.strictObject({ ...base, type: z.literal('streaming') }),
  z.strictObject({ ...base, type: z.literal('download'), sequence: z.number().int().positive() }),
  z.strictObject({ ...base, type: z.literal('dialog'), sequence: z.number().int().positive() }),
  z.strictObject({ ...base, type: z.literal('event'), ...NativeAutomationEventSchema.shape }),
  z.strictObject({
    ...base,
    type: z.literal('console'),
    sequence: z.number().int().positive().optional(),
  }),
  z.strictObject({
    ...base,
    type: z.literal('navigation'),
    url: z.string().max(8192),
  }),
  z
    .strictObject({
      ...base,
      type: z.literal('frame'),
      pixelFormat: z.enum(['bgra', 'i420']).default('bgra'),
      timestampMs: timestamp,
      width: z.number().int().min(1).max(3840),
      height: z.number().int().min(1).max(2160),
      // CSS bounds account for the smallest supported 0.25 device scale.
      viewport: z
        .strictObject({
          width: z.number().int().min(1).max(15360),
          height: z.number().int().min(1).max(8640),
        })
        .nullable()
        .optional(),
      contentRect: z
        .strictObject({
          x: z.number().int().min(0).max(3839),
          y: z.number().int().min(0).max(2159),
          width: z.number().int().min(1).max(3840),
          height: z.number().int().min(1).max(2160),
        })
        .optional(),
      dropped: z.number().int().nonnegative(),
    })
    .superRefine((value, context) => {
      const rect = value.contentRect
      if (rect && (rect.x + rect.width > value.width || rect.y + rect.height > value.height))
        context.addIssue({
          code: 'custom',
          message: 'Native frame content rectangle exceeds capture canvas',
        })
    }),
  z.strictObject({
    ...base,
    type: z.literal('audio'),
    timestampMs: timestamp,
    frames: z.number().int().min(1).max(4096),
  }),
  z.strictObject({
    ...base,
    type: z.literal('response'),
    bytes: z.number().int().min(0).max(MAX_NATIVE_RESPONSE_PAYLOAD),
    id: z.number().int().positive(),
    ok: z.boolean(),
    navigation: NativeFrameInfoSchema.optional(),
    error: z.string().max(4096).optional(),
  }),
  z.strictObject({ ...base, type: z.literal('error'), error: z.string().max(4096) }),
  z.strictObject({
    ...base,
    type: z.literal('pagerecovered'),
    pageId: z.number().int().min(2),
    bytes: z.literal(0),
    rendererGeneration: z.number().int().positive(),
    mainFrame: NativeFrameInfoSchema,
  }),
  z.strictObject({
    ...base,
    type: z.literal('pagecrash'),
    sequence: z.number().int().positive().optional(),
    pageId: z.number().int().min(2),
    bytes: z.literal(0),
    error: z.string().max(4096),
  }),
])
export type NativePacket = z.infer<typeof NativePacketSchema>

/** Incrementally decodes length-prefixed packets with bounded allocation and no quadratic concatenation. */
export class NativePacketDecoder {
  private readonly length = Buffer.allocUnsafe(4)
  private target: Buffer = this.length
  private offset = 0
  private phase: 'length' | 'header' | 'payload' = 'length'
  private header: NativePacket | undefined
  private disposed = false
  private pushing = false
  private pending: (Buffer | undefined)[] | undefined
  private pendingIndex = 0
  private pendingBytes = 0

  constructor(private readonly receive: (header: NativePacket, payload: Buffer) => void) {}

  push(chunk: Buffer): void {
    if (this.disposed) throw new Error('Native packet decoder is disposed')
    if (this.pushing) {
      if (!chunk.length) return
      if (
        this.pendingBytes + chunk.length > MAX_REENTRANT_BYTES ||
        (this.pending?.length ?? 0) - this.pendingIndex >= MAX_REENTRANT_CHUNKS
      ) {
        this.dispose()
        throw new Error('Native packet reentrant queue limit exceeded')
      }
      // The caller can reuse its read buffer when push returns. Own deferred bytes.
      const pending = (this.pending ??= [])
      pending.push(Buffer.from(chunk))
      this.pendingBytes += chunk.length
      return
    }
    this.pushing = true
    try {
      let input = chunk
      for (;;) {
        let cursor = 0
        while (cursor < input.length && !this.disposed) {
          const count = Math.min(this.target.length - this.offset, input.length - cursor)
          input.copy(this.target, this.offset, cursor, cursor + count)
          cursor += count
          this.offset += count
          if (this.offset !== this.target.length) continue
          this.offset = 0
          if (this.phase === 'length') {
            const size = this.length.readUInt32LE()
            if (size < 2 || size > MAX_HEADER)
              throw new Error('Invalid native packet header length')
            if (input.length - cursor >= size) {
              // Parse headers already present in the read buffer without allocating and copying them.
              const json = input.toString('utf8', cursor, cursor + size)
              cursor += size
              this.readHeader(json)
            } else {
              this.target = Buffer.allocUnsafe(size)
              this.phase = 'header'
            }
          } else if (this.phase === 'header') {
            this.readHeader(this.target.toString('utf8'))
          } else {
            const header = this.header
            const payload = this.target
            this.reset()
            if (header) this.receive(header, payload)
          }
        }
        if (this.disposed || !this.pending || this.pendingIndex === this.pending.length) break
        const next = this.pending[this.pendingIndex]
        this.pending[this.pendingIndex++] = undefined
        if (!next) break
        this.pendingBytes -= next.length
        input = next
        // Keep a long chain of receiver-fed reads bounded without quadratic Array.shift().
        if (this.pendingIndex >= 1024) {
          this.pending.splice(0, this.pendingIndex)
          this.pendingIndex = 0
        }
      }
    } catch (error) {
      // Framing has no resynchronization marker. Never retain or reuse a failed partial packet.
      this.dispose()
      throw error
    } finally {
      this.pushing = false
      if (this.pending) this.clearPending()
    }
  }

  private readHeader(json: string): void {
    const header = NativePacketSchema.parse(JSON.parse(json))
    if (header.type === 'websocketroute' && header.stage !== 'message' && header.bytes !== 0)
      throw new Error('Unexpected native WebSocket route control payload')
    if (header.type === 'frame') {
      if (header.pixelFormat === 'i420' && (header.width % 2 || header.height % 2))
        throw new Error('I420 frames require even dimensions')
      if (header.bytes !== header.width * header.height * (header.pixelFormat === 'bgra' ? 4 : 1.5))
        throw new Error('Invalid native frame length')
    }
    if (
      (header.type === 'networkbody' || header.type === 'contextnetworkbody') &&
      (header.bytes > 24_576 || (header.canceled && header.bytes))
    )
      throw new Error('Invalid native network body packet length')
    if (header.type === 'audio' && header.bytes !== header.frames * 4)
      throw new Error('Invalid native PCM packet length')
    if (
      (header.type === 'ready' ||
        header.type === 'error' ||
        header.type === 'pagecrash' ||
        header.type === 'pagerecovered' ||
        header.type === 'navigation' ||
        header.type === 'event' ||
        header.type === 'actionstage' ||
        header.type === 'urlpredicate') &&
      header.bytes !== 0
    )
      throw new Error('Unexpected native control payload')
    if (
      (header.type === 'console' ||
        header.type === 'pageerror' ||
        header.type === 'network' ||
        header.type === 'context' ||
        header.type === 'contextconsole' ||
        header.type === 'contextworker' ||
        header.type === 'contextworkerpredicate' ||
        header.type === 'contextnetwork' ||
        header.type === 'contextroute' ||
        header.type === 'dialog' ||
        header.type === 'binding') &&
      (header.bytes === 0 || header.bytes > 1_048_576)
    )
      throw new Error('Invalid native console payload length')
    if (header.type === 'eventpredicate') {
      const socket = ['websocket', 'framesent', 'framereceived', 'socketerror', 'close'].includes(
        header.event,
      )
      if (header.bytes === 0 || header.bytes > (socket ? 3_145_728 : 1_048_576))
        throw new Error('Invalid native event predicate payload length')
    }
    if (header.bytes === 0) {
      this.reset()
      this.receive(header, Buffer.alloc(0))
    } else {
      this.header = header
      this.target = Buffer.allocUnsafe(header.bytes)
      this.phase = 'payload'
    }
  }

  end(): void {
    if (this.disposed) return
    if (this.phase !== 'length' || this.offset !== 0) {
      this.dispose()
      throw new Error('Truncated native packet')
    }
  }

  /** Releases partial payload storage when a process fails or closes. */
  dispose(): void {
    this.disposed = true
    this.reset()
    this.clearPending()
  }

  private clearPending(): void {
    this.pending = undefined
    this.pendingIndex = 0
    this.pendingBytes = 0
  }

  private reset(): void {
    this.target = this.length
    this.offset = 0
    this.header = undefined
    this.phase = 'length'
  }
}
