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

const base = {
  version: z.literal(NATIVE_PROTOCOL_VERSION),
  pageId: z.number().int().positive().optional(),
  bytes: z.number().int().min(0),
}
const timestamp = z.number().finite().nonnegative()
export const NativeFrameInfoSchema = z
  .strictObject({
    id: z.string().min(1),
    parentId: z.string(),
    name: z.string().optional(),
    nameJSON: z.string().optional(),
    url: z.string(),
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
    if (typeof name !== 'string') {
      context.addIssue({ code: 'custom', message: 'Invalid native frame name string' })
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
  worker: z.string().min(1).optional(),
  workerInfo: NativeWorkerInfoSchema.optional(),
  type: z.string().min(1),
  text: z.string(),
  timestamp: z.number().finite().nonnegative().optional(),
  arguments: z
    .array(
      z.strictObject({
        handle: z.number().int().positive(),
        guid: z.string(),
        preview: z.string(),
        node: z.boolean(),
        frame: z.string().optional(),
        worker: z.string().min(1).optional(),
        workerControl: NativeWorkerControlSchema.optional(),
      }),
    )
    .optional(),
  argumentsError: z.string().optional(),
  location: z.strictObject({
    line: z.number().int().nonnegative().optional(),
    url: z.string(),
    lineNumber: z.number().int().nonnegative(),
    columnNumber: z.number().int().nonnegative(),
  }),
})
export type NativeConsoleInfo = z.infer<typeof NativeConsoleInfoSchema>
// Observed request URLs, data URLs included, are kept whole as the page used them.
const NativeObservedNetworkURLSchema = z.string()
export const NativeNetworkRequestSchema = z.object({
  id: z.string().min(1),
  requestId: z.string(),
  session: z.string(),
  frameId: z.string(),
  url: NativeObservedNetworkURLSchema,
  method: z.string(),
  resourceType: z.string(),
  postDataBodyId: z
    .string()
    .regex(/^[1-9][0-9]{0,19}$/u)
    .optional(),
  postDataOwner: z
    .string()
    .regex(/^(?:page|broker):[1-9][0-9]*$/u)
    .optional(),
  postDataBase64: z.string().optional(),
  postDataUnavailable: z.boolean().optional(),
  headers: NativeObservedHeadersSchema,
  headersReady: z.boolean().optional(),
  postData: z.string().optional(),
  isNavigationRequest: z.boolean(),
  serviceWorker: z.boolean().optional(),
  workerInfo: NativeWorkerInfoSchema.optional(),
  contextWorker: z.boolean().optional(),
  navigationFrameUnavailable: z.boolean().optional(),
  blockedReason: z.string().optional(),
  timing: z.record(z.string(), z.number()).optional(),
  redirectedFrom: z.string().optional(),
})
export const NativeResponseServerAddrSchema = z.strictObject({
  ipAddress: z.string().min(1),
  port: z.number().int().min(1).max(65535),
})
export const NativeResponseSecurityDetailsSchema = z.strictObject({
  issuer: z.string(),
  protocol: z.string(),
  subjectName: z.string(),
  validFrom: z.number(),
  validTo: z.number(),
})
export const NativeNetworkResponseSchema = z.object({
  headersReady: z.boolean().optional(),
  url: NativeObservedNetworkURLSchema,
  status: z.number().int().min(0).max(999),
  statusText: z.string(),
  headers: NativeObservedHeadersSchema,
  requestId: z.string(),
  session: z.string(),
  frameId: z.string(),
  method: z.string(),
  fromServiceWorker: z.boolean().optional(),
  mimeType: z.string().optional(),
  serverAddr: NativeResponseServerAddrSchema.optional(),
  securityDetails: NativeResponseSecurityDetailsSchema.optional(),
  protocol: z.string().optional(),
  timing: z.record(z.string(), z.number()).optional(),
})
export const NativeNetworkInfoSchema = z.discriminatedUnion('event', [
  NativePageRouteMutationSchema,
  z.object({
    event: z.literal('routematch'),
    sequence: z.number().int().positive().max(Number.MAX_SAFE_INTEGER),
    matcher: z.strictObject({
      id: z.string().min(1),
      registrationId: z.string().min(1),
      url: z.string().min(1),
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
    errorText: z.string().optional(),
    route: z
      .object({
        id: z.string(),
        registrationId: z.string(),
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
    routeId: z.string(),
  }),
  z.object({
    event: z.literal('routeerror'),
    sequence: z.number().int().positive(),
    errorText: z.string(),
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
  closedReason: z.string().optional(),
  video: NativeVideoDescriptorSchema.optional(),
})
export type NativeContextEvent = z.infer<typeof NativeContextEventSchema>
export const NativeActionStageSchema = z.strictObject({
  clientToken: z.string().min(1),
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
  clientToken: z.string().min(1),
  id: z.number().int().positive(),
  generation: z.number().int().positive(),
  token: z.number().int().positive(),
  url: z.string(),
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
  id: z.string().min(1),
  type: z.enum(['alert', 'confirm', 'prompt', 'beforeunload']),
  url: z.string(),
  message: z.string(),
  defaultValue: z.string(),
})
export type NativeDialogInfo = z.infer<typeof NativeDialogInfoSchema>
export const NativeFileChooserInfoSchema = z.strictObject({
  id: z.string().min(1),
  frame: z.string().min(1),
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
    cefVersion: z.string(),
    captureWidth: z.number().int().min(1).optional(),
    captureHeight: z.number().int().min(1).optional(),
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
    offset: z.number().int().min(0),
    totalBytes: z.number().int().min(0),
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
  }),
  z.strictObject({
    ...base,
    type: z.literal('contextroutemutation'),
    bytes: z.number().int().positive(),
  }),
  z.strictObject({
    ...base,
    type: z.literal('contextnetworkbody'),
    contextId: z.number().int().positive(),
    owner: z.string().regex(/^broker:[1-9][0-9]*$/u),
    bodyId: z.string().regex(/^[1-9][0-9]{0,19}$/u),
    offset: z.number().int().min(0),
    totalBytes: z.number().int().min(0),
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
    url: z.string(),
  }),
  z
    .strictObject({
      ...base,
      type: z.literal('frame'),
      pixelFormat: z.enum(['bgra', 'i420']).default('bgra'),
      timestampMs: timestamp,
      width: z.number().int().min(1),
      height: z.number().int().min(1),
      viewport: z
        .strictObject({
          width: z.number().int().min(1),
          height: z.number().int().min(1),
        })
        .nullable()
        .optional(),
      contentRect: z
        .strictObject({
          x: z.number().int().min(0),
          y: z.number().int().min(0),
          width: z.number().int().min(1),
          height: z.number().int().min(1),
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
    frames: z.number().int().min(1),
  }),
  z.strictObject({
    ...base,
    type: z.literal('response'),
    id: z.number().int().positive(),
    ok: z.boolean(),
    navigation: NativeFrameInfoSchema.optional(),
    error: z.string().optional(),
  }),
  z.strictObject({ ...base, type: z.literal('error'), error: z.string() }),
  z.strictObject({
    ...base,
    type: z.literal('pagerecovered'),
    pageId: z.number().int().min(1),
    bytes: z.literal(0),
    rendererGeneration: z.number().int().positive(),
    mainFrame: NativeFrameInfoSchema,
  }),
  z.strictObject({
    ...base,
    type: z.literal('pagecrash'),
    sequence: z.number().int().positive().optional(),
    pageId: z.number().int().min(1),
    bytes: z.literal(0),
    error: z.string(),
  }),
])
export type NativePacket = z.infer<typeof NativePacketSchema>

const EMPTY = Buffer.alloc(0)

/**
 * Incrementally decodes length-prefixed packets with no quadratic concatenation.
 * Header and payload storage grows geometrically with the bytes that actually
 * arrive, so a corrupt length never allocates memory for data that is not there.
 */
export class NativePacketDecoder {
  private readonly length = Buffer.allocUnsafe(4)
  private target: Buffer = this.length
  private expected = 4
  private offset = 0
  private phase: 'length' | 'header' | 'payload' = 'length'
  private header: NativePacket | undefined
  private disposed = false
  private pushing = false
  private pending: (Buffer | undefined)[] | undefined
  private pendingIndex = 0

  constructor(private readonly receive: (header: NativePacket, payload: Buffer) => void) {}

  push(chunk: Buffer): void {
    if (this.disposed) throw new Error('Native packet decoder is disposed')
    if (this.pushing) {
      if (!chunk.length) return
      // The caller can reuse its read buffer when push returns. Own deferred bytes.
      const pending = (this.pending ??= [])
      pending.push(Buffer.from(chunk))
      return
    }
    this.pushing = true
    try {
      let input = chunk
      for (;;) {
        let cursor = 0
        while (cursor < input.length && !this.disposed) {
          const count = Math.min(this.expected - this.offset, input.length - cursor)
          if (this.offset + count > this.target.length) this.grow(this.offset + count)
          input.copy(this.target, this.offset, cursor, cursor + count)
          cursor += count
          this.offset += count
          if (this.offset !== this.expected) continue
          this.offset = 0
          if (this.phase === 'length') {
            const size = this.length.readUInt32LE()
            if (size < 2) throw new Error('Invalid native packet header length')
            if (input.length - cursor >= size) {
              // Parse headers already present in the read buffer without allocating and copying them.
              const json = input.toString('utf8', cursor, cursor + size)
              cursor += size
              this.readHeader(json)
            } else {
              this.expect(size)
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
      header.canceled &&
      header.bytes
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
      header.bytes === 0
    )
      throw new Error('Invalid native console payload length')
    if (header.type === 'eventpredicate' && header.bytes === 0)
      throw new Error('Invalid native event predicate payload length')
    if (header.bytes === 0) {
      this.reset()
      this.receive(header, Buffer.alloc(0))
    } else {
      this.header = header
      this.expect(header.bytes)
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
  }

  /** Starts a header or payload of `size` bytes without allocating before they arrive. */
  private expect(size: number): void {
    this.target = EMPTY
    this.expected = size
  }

  /** Doubles storage up to the expected size; amortized copying stays linear. */
  private grow(required: number): void {
    const next = Buffer.allocUnsafe(
      Math.min(this.expected, Math.max(required, this.target.length * 2)),
    )
    this.target.copy(next, 0, 0, this.offset)
    this.target = next
  }

  private reset(): void {
    this.target = this.length
    this.expected = 4
    this.offset = 0
    this.header = undefined
    this.phase = 'length'
  }
}
