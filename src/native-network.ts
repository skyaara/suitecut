// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'
import { open } from 'node:fs/promises'

import * as z from 'zod'

import { type NativeBrowser } from './native-browser.js'
import { nativeContextPageBindings } from './native-context-page.js'
import { NativeContextRouteInvocations } from './native-context-route-invocations.js'
import { waitNativeEventPredicate } from './native-event-predicate.js'
import { registerNativeHTTPRequestClass } from './native-http-request-input.js'
import { NativeAPIResponse, nativeRouteFetchResponse } from './native-http.js'
import {
  NativeObservedHeadersSchema,
  normalizeNativeObservedHeaders as normalizeHeaders,
  nativeObservedHeaderValues,
  NativeStringHeadersSchema,
} from './native-network-headers.js'
import { nativePageRouteCallbackIdentity } from './native-page-route-removal-protocol.js'
import { NativePageRouteRemovals } from './native-page-route-removals.js'
import { type NativeFrame, type NativePage } from './native-page.js'
import {
  NativeNetworkInfoSchema,
  NativeNetworkResponseSchema,
  type NativeNetworkInfo,
  type NativeNetworkRequestInfo,
  type NativeNetworkResponseInfo,
} from './native-protocol.js'
import {
  invokeNativeRouteCallback,
  type NativeRouteCallbackLease,
} from './native-route-callback.js'
import {
  nativeRouteCommandFailed,
  nativeRouteHandlerInvoked,
} from './native-route-handler-observations.js'
import { invokeNativeRouteURLMatcher } from './native-route-url-matcher-callback.js'
import {
  NativeRouteURLMatcherPolicy,
  type NativeRouteMatcherRegistration,
} from './native-route-url-matcher.js'
import { getNativeDefaultTimeout } from './native-timeouts.js'
import { nativeTraceStack } from './native-trace-metadata.js'
import { type NativeWorkerInfo } from './native-worker-protocol.js'
import { type NativeWorker } from './native-worker.js'
import { toError, type UntrustedInput } from './untrusted.js'

const MAX_ROUTE_BODY = 2 * 1024 * 1024
interface NetworkWaitOptions {
  timeout?: number | undefined
  signal?: AbortSignal | undefined
}
const HeaderSchema = NativeStringHeadersSchema
const ContinueSchema = z.strictObject({
  url: z.url().optional(),
  method: z.string().min(1).max(128).optional(),
  headers: HeaderSchema.optional(),
  postData: z.union([z.string(), z.instanceof(Buffer)]).optional(),
})
export type NativeRouteContinueOptions = z.input<typeof ContinueSchema>
const RouteFetchSchema = ContinueSchema.extend({
  timeout: z.number().int().min(0).max(2_147_483_647).optional(),
  maxRedirects: z.number().int().min(0).max(100).optional(),
  maxRetries: z.number().int().min(0).max(100).optional(),
})
export type NativeRouteFetchOptions = z.input<typeof RouteFetchSchema>
export interface NativeRouteFulfillOptions {
  status?: number
  statusText?: string
  headers?: Record<string, string>
  contentType?: string
  body?: string | Buffer
  path?: string
  json?: UntrustedInput
  response?: NativeResponse | NativeAPIResponse
}
const FulfillSchema = z.strictObject({
  status: z.number().int().min(100).max(599).optional(),
  statusText: z.string().max(1024).optional(),
  headers: HeaderSchema.optional(),
  contentType: z.string().max(1024).optional(),
  body: z.union([z.string(), z.instanceof(Buffer)]).optional(),
  path: z.string().optional(),
  json: z.unknown().optional(),
  response: z
    .custom<NativeResponse | NativeAPIResponse>(
      (value) => value instanceof NativeResponse || value instanceof NativeAPIResponse,
    )
    .optional(),
})
const abortReasons = {
  failed: 'Failed',
  aborted: 'Aborted',
  timedout: 'TimedOut',
  accessdenied: 'AccessDenied',
  connectionclosed: 'ConnectionClosed',
  connectionreset: 'ConnectionReset',
  connectionrefused: 'ConnectionRefused',
  connectionaborted: 'ConnectionAborted',
  connectionfailed: 'ConnectionFailed',
  namenotresolved: 'NameNotResolved',
  internetdisconnected: 'InternetDisconnected',
  addressunreachable: 'AddressUnreachable',
  blockedbyclient: 'BlockedByClient',
  blockedbyresponse: 'BlockedByResponse',
} as const
function headerArray(headers: Record<string, string>): { name: string; value: string }[] {
  return Object.entries(headers).map(([name, value]) => {
    if (!name || /[\r\n\0]/u.test(name) || /[\r\n\0]/u.test(value))
      throw new Error('HTTP header names and values must not contain newline or null characters')
    return { name, value }
  })
}

function observedHeaderArray(
  headers: Record<string, string | number>,
): { name: string; value: string }[] {
  return Object.entries(headers).flatMap(([name, value]) =>
    String(value)
      .split('\n')
      .map((part) => ({ name, value: part })),
  )
}
/** A CEF network request. Objects preserve identity across response and finish events. */
export class NativeRequest {
  static {
    registerNativeHTTPRequestClass(this)
  }
  private originalBody: Buffer | undefined
  private originalBodyId: string | undefined
  private responseObject: WeakRef<NativeResponse> | undefined
  private responseInfo: NativeNetworkResponseInfo | undefined
  private failed: string | undefined
  private previous: NativeRequest | undefined
  private next: NativeRequest | undefined
  private ended = false
  private observedSequence = 0
  private readyHeaderSequence: number | undefined
  private readonly requestFrame: NativeFrame | undefined
  private requestServiceWorker: NativeWorker | null
  constructor(
    private readonly network: NativeNetwork,
    readonly info: NativeNetworkRequestInfo,
    sequence = 0,
  ) {
    this.observedSequence = sequence
    this.readyHeaderSequence = info.headersReady === true ? sequence : undefined
    this.requestFrame = network.findFrame(info.frameId)
    this.requestServiceWorker = network.findServiceWorker(info)
    this.captureBody()
  }
  retainedMetadataBytes(
    visited: Set<NativeRequest>,
    measurements?: Map<NativeRequest, number>,
  ): number {
    const pending: NativeRequest[] = [this]
    let total = 0
    while (pending.length) {
      const request = pending.pop()
      if (!request) break
      if (visited.has(request)) continue
      visited.add(request)
      const bytes = measurements?.get(request) ?? request.ownMetadataBytes()
      measurements?.set(request, bytes)
      total += bytes
      if (request.previous) pending.push(request.previous)
      if (request.next) pending.push(request.next)
    }
    return total
  }
  hasRedirectLinks(): boolean {
    return this.previous !== undefined || this.next !== undefined
  }
  ownMetadataBytes(): number {
    return (
      Buffer.byteLength(JSON.stringify({ request: this.info, response: this.responseInfo })) +
      (this.originalBody?.length ?? 0)
    )
  }
  url(): string {
    return this.info.url
  }
  method(): string {
    return this.info.method
  }
  resourceType(): string {
    return this.info.resourceType.toLowerCase()
  }
  isNavigationRequest(): boolean {
    return this.info.isNavigationRequest
  }
  serviceWorker(): NativeWorker | null {
    if (!this.info.serviceWorker) return null
    this.requestServiceWorker ??= this.network.findServiceWorker(this.info)
    if (!this.requestServiceWorker)
      throw new Error('Native service worker target identity or owner channel is unavailable')
    return this.requestServiceWorker
  }
  frame(): NativeFrame {
    if (this.info.navigationFrameUnavailable)
      throw new Error('Frame for this navigation request is not available')
    const frame = this.requestFrame ?? this.network.findFrame(this.info.frameId)
    if (!frame) throw new Error('Request frame belongs to a service worker or is no longer known')
    return frame
  }
  hasFinished(): boolean {
    return this.ended
  }
  observe(info: NativeNetworkRequestInfo, sequence: number): void {
    if (sequence < this.observedSequence) return
    this.observedSequence = sequence
    this.readyHeaderSequence = info.headersReady === true ? sequence : undefined
    const snapshot = { ...info }
    delete this.info.postData
    delete this.info.postDataBodyId
    delete this.info.postDataBase64
    delete this.info.postDataUnavailable
    Object.assign(this.info, snapshot)
    this.requestServiceWorker ??= this.network.findServiceWorker(this.info)
    this.captureBody()
  }
  private captureBody(): void {
    if (this.info.postDataBodyId === this.originalBodyId && this.originalBody !== undefined) return
    this.originalBodyId = this.info.postDataBodyId
    this.originalBody = this.info.postDataBodyId
      ? this.network.takeBody(this.info.postDataBodyId, this.info)
      : undefined
  }
  sequence(): number {
    return this.observedSequence
  }
  knownFrame(): NativeFrame | undefined {
    return this.requestFrame
  }
  knownResponse(): NativeResponse | undefined {
    return this.responseObject?.deref()
  }
  headers(): Record<string, string> {
    return normalizeHeaders(this.info.headers)
  }
  async allHeaders(): Promise<Record<string, string>> {
    if (this.ended && this.info.headersReady !== false) return this.headers()
    const sequence = this.observedSequence
    const headers = NativeObservedHeadersSchema.parse(
      await this.network.command({
        action: 'requestHeaders',
        requestId: this.info.id,
      }),
    )
    if (this.readyHeaderSequence !== undefined && this.readyHeaderSequence > sequence)
      return this.headers()
    this.info.headers = headers
    this.info.headersReady = true
    return normalizeHeaders(headers)
  }
  async headersArray(): Promise<{ name: string; value: string }[]> {
    await this.allHeaders()
    return observedHeaderArray(this.info.headers)
  }
  async headerValue(name: string): Promise<string | null> {
    return (await this.allHeaders())[name.toLowerCase()] ?? null
  }
  postData(): string | null {
    return this.postDataBuffer()?.toString('utf8') ?? null
  }
  postDataBuffer(): Buffer | null {
    if (this.info.postDataUnavailable)
      throw new Error(
        'Native request body bytes are unavailable or exceed native publication capacity',
      )
    if (this.originalBody) return Buffer.from(this.originalBody)
    if (this.info.postDataBodyId)
      throw new Error('Native original request body bytes are no longer retained')
    if (this.info.postDataBase64 !== undefined)
      return Buffer.from(this.info.postDataBase64, 'base64')
    return this.info.postData === undefined ? null : Buffer.from(this.info.postData)
  }
  postDataJSON(): UntrustedInput {
    const value = this.postData()
    if (value === null) return null
    if (
      this.headers()['content-type']?.split(';')[0]?.trim() === 'application/x-www-form-urlencoded'
    )
      return Object.fromEntries(new URLSearchParams(value))
    return JSON.parse(value) as UntrustedInput
  }
  redirectedFrom(): NativeRequest | null {
    return this.previous ?? null
  }
  redirectedTo(): NativeRequest | null {
    return this.next ?? null
  }
  failure(): { errorText: string } | null {
    return this.failed === undefined ? null : { errorText: this.failed }
  }
  existingResponse(): NativeResponse | null {
    return (
      this.responseObject?.deref() ??
      (this.responseInfo ? this.network.createResponse(this, this.responseInfo) : null)
    )
  }
  async response(): Promise<NativeResponse | null> {
    const existing = this.responseObject?.deref()
    if (existing) return existing
    if (this.responseInfo) return this.network.createResponse(this, this.responseInfo)
    const sequence = this.observedSequence
    const result = await this.network.command({
      action: 'response',
      requestId: this.info.id,
    })
    const info = result === null ? null : NativeNetworkResponseSchema.parse(result)
    if (this.observedSequence > sequence) {
      const observed = this.existingResponse()
      if (observed) return observed
    }
    if (info === null) return this.existingResponse()
    return this.network.createResponse(this, info, sequence, false)
  }
  timing(): NativeRequestTiming {
    return TimingSchema.parse(
      this.info.timing ?? {
        startTime: 0,
        domainLookupStart: -1,
        domainLookupEnd: -1,
        connectStart: -1,
        secureConnectionStart: -1,
        connectEnd: -1,
        requestStart: -1,
        responseStart: -1,
        responseEnd: -1,
      },
    )
  }
  /** Engine event updates; callers should use response()/failure()/redirectedFrom(). */
  update(
    response?: NativeResponse,
    failure?: string,
    previous?: NativeRequest,
    finished = false,
  ): void {
    this.ended ||= finished
    if (response) {
      this.responseObject = new WeakRef(response)
      this.responseInfo = response.info
    }
    if (failure !== undefined) this.failed = failure
    if (previous) {
      this.previous = previous
      previous.next = this
    }
  }
}
const TimingSchema = z.strictObject({
  startTime: z.number(),
  domainLookupStart: z.number(),
  domainLookupEnd: z.number(),
  connectStart: z.number(),
  secureConnectionStart: z.number(),
  connectEnd: z.number(),
  requestStart: z.number(),
  responseStart: z.number(),
  responseEnd: z.number(),
})
export type NativeRequestTiming = z.infer<typeof TimingSchema>

export interface NativeResponseServerAddr {
  ipAddress: string
  port: number
}
export interface NativeResponseSecurityDetails {
  issuer: string
  protocol: string
  subjectName: string
  validFrom: number
  validTo: number
}
/** A native response with body ownership retained by its CEF target. */
export class NativeResponse {
  private bodyPromise: Promise<Buffer> | undefined
  readonly info: NativeNetworkResponseInfo
  private requestObject: NativeRequest | undefined
  private observedSequence = 0
  private readyHeaderSequence: number | undefined
  constructor(
    private readonly owner: NativeNetwork,
    private readonly responseFrame: NativeFrame | undefined,
    info: UntrustedInput,
    request?: NativeRequest,
    sequence = 0,
    fromNativeEvent = true,
  ) {
    this.observedSequence = sequence
    this.info = NativeNetworkResponseSchema.parse(info)
    this.readyHeaderSequence =
      fromNativeEvent && this.info.headersReady === true ? sequence : undefined
    this.requestObject = request
  }
  observe(info: UntrustedInput, sequence: number, fromNativeEvent = true): void {
    if (sequence < this.observedSequence) return
    this.observedSequence = sequence
    const snapshot = NativeNetworkResponseSchema.parse(info)
    if (fromNativeEvent)
      this.readyHeaderSequence = snapshot.headersReady === true ? sequence : undefined
    Object.assign(this.info, snapshot)
  }
  url(): string {
    return this.info.url
  }
  status(): number {
    return this.info.status
  }
  statusText(): string {
    return this.info.statusText
  }
  ok(): boolean {
    return this.info.status === 0 || (this.info.status >= 200 && this.info.status < 300)
  }
  frame(): NativeFrame {
    const frame = this.responseFrame ?? this.requestObject?.frame()
    if (!frame) throw new Error('Response frame is no longer known')
    return frame
  }
  httpVersion(): string | null {
    const protocol = this.info.protocol
    if (protocol === 'h2') return 'HTTP/2.0'
    if (protocol === 'h3' || protocol?.startsWith('h3-')) return 'HTTP/3.0'
    return protocol?.replace(/^http\//iu, 'HTTP/') ?? null
  }
  serverAddr(): Promise<NativeResponseServerAddr | null> {
    return Promise.resolve(this.info.serverAddr ? { ...this.info.serverAddr } : null)
  }
  securityDetails(): Promise<NativeResponseSecurityDetails | null> {
    return Promise.resolve(this.info.securityDetails ? { ...this.info.securityDetails } : null)
  }
  fromServiceWorker(): boolean {
    return this.info.fromServiceWorker ?? false
  }
  headers(): Record<string, string> {
    return normalizeHeaders(this.info.headers)
  }
  async allHeaders(): Promise<Record<string, string>> {
    if (
      !this.requestObject ||
      (this.requestObject.hasFinished() && this.info.headersReady !== false)
    )
      return this.headers()
    const sequence = this.observedSequence
    const headers = NativeObservedHeadersSchema.parse(
      await this.requestObjectNetwork({
        action: 'responseHeaders',
        requestId: this.requestObject.info.id,
      }),
    )
    if (this.readyHeaderSequence !== undefined && this.readyHeaderSequence > sequence)
      return this.headers()
    this.info.headers = headers
    this.info.headersReady = true
    return this.headers()
  }
  async headersArray(): Promise<{ name: string; value: string }[]> {
    await this.allHeaders()
    return observedHeaderArray(this.info.headers)
  }
  async headerValue(name: string): Promise<string | null> {
    return (await this.allHeaders())[name.toLowerCase()] ?? null
  }
  async headerValues(name: string): Promise<string[]> {
    await this.allHeaders()
    return nativeObservedHeaderValues(this.info.headers, name)
  }
  request(): NativeRequest {
    if (!this.requestObject) throw new Error('Navigation response request metadata is unavailable')
    return this.requestObject
  }
  body(): Promise<Buffer> {
    if (this.info.status >= 300 && this.info.status < 400 && this.headers().location !== undefined)
      return Promise.reject(new Error('Response body is unavailable for redirect responses'))
    this.bodyPromise ??= this.owner.responseBody(this.info, this.requestObject).then((result) => {
      const payload = z.object({ body: z.string(), base64Encoded: z.boolean() }).parse(result)
      return Buffer.from(payload.body, payload.base64Encoded ? 'base64' : 'utf8')
    })
    return this.bodyPromise
  }
  async text(): Promise<string> {
    return (await this.body()).toString('utf8')
  }
  async json(): Promise<UntrustedInput> {
    return JSON.parse(await this.text()) as UntrustedInput
  }
  async finished(): Promise<Error | null> {
    if (this.requestObject?.hasFinished())
      return this.requestObject.failure() === null
        ? null
        : new Error(this.requestObject.failure()?.errorText)
    if (!this.requestObject) {
      await this.body()
      return null
    }
    const result = z.object({ errorText: z.string().nullable() }).parse(
      await this.requestObjectNetwork({
        action: 'finished',
        requestId: this.requestObject.info.id,
      }),
    )
    return result.errorText === null ? null : new Error(result.errorText)
  }
  private requestObjectNetwork(config: Record<string, UntrustedInput>): Promise<UntrustedInput> {
    return this.owner.command(config)
  }
}

export class NativeRoute {
  private actionTraceStack: ReturnType<typeof nativeTraceStack> = []
  private resolved = false
  private handling = false
  constructor(
    private readonly network: NativeNetwork,
    readonly id: string,
    private readonly routeRequest: NativeRequest,
    private readonly routeCommand: (
      config: Record<string, UntrustedInput>,
    ) => Promise<UntrustedInput> = (config) => network.command(config),
    private readonly callbackScope?: string,
  ) {}
  request(): NativeRequest {
    return this.routeRequest
  }
  /** Fetches the genuine paused request without resolving its browser route. */
  async fetch(options: NativeRouteFetchOptions = {}): Promise<NativeAPIResponse> {
    if (this.resolved || this.handling) throw new Error('Route is already handled')
    const parsed = RouteFetchSchema.parse(options)
    this.handling = true
    try {
      const source = this.network.routeFetchSource()
      const params: Record<string, UntrustedInput> = {
        action: 'fetch',
        routeId: this.id,
        ...(parsed.url === undefined ? {} : { url: parsed.url }),
        ...(parsed.method === undefined ? {} : { method: parsed.method }),
        ...(parsed.headers === undefined ? {} : { headers: headerArray(parsed.headers) }),
        ...(parsed.timeout === undefined ? {} : { timeoutMs: parsed.timeout }),
        ...(parsed.maxRedirects === undefined ? {} : { maxRedirects: parsed.maxRedirects }),
        ...(parsed.maxRetries === undefined ? {} : { maxRetries: parsed.maxRetries }),
      }
      if (parsed.postData !== undefined) {
        const body = Buffer.from(parsed.postData)
        if (body.length > MAX_ROUTE_BODY) throw new Error('Native route body exceeds 2 MiB')
        if (body.length <= 24 * 1024) params.postData = body.toString('base64')
        else {
          await this.upload(body, params)
          params.bodyUpload = true
        }
      }
      return nativeRouteFetchResponse(source, await this.command(params))
    } finally {
      this.handling = false
    }
  }
  async abort(errorCode: keyof typeof abortReasons = 'failed'): Promise<void> {
    const reason = abortReasons[errorCode]
    if (!reason) throw new Error(`Unknown route abort code: ${errorCode}`)
    await this.handle(() => this.resolve({ action: 'abort', errorReason: reason }))
  }
  async continue(options: NativeRouteContinueOptions = {}): Promise<void> {
    await this.handle(() => this.continueOrFallback('continue', options))
  }
  async fallback(options: NativeRouteContinueOptions = {}): Promise<void> {
    await this.handle(() => this.continueOrFallback('fallback', options))
  }
  /** Transport metadata for automatic recovery, preserving the native route owner. */
  async resolveAutomatically(action: 'fallback' | 'abort'): Promise<void> {
    await this.handle(() =>
      this.resolve({
        action,
        internal: true,
        ...(action === 'abort' ? { errorReason: 'Failed' } : {}),
      }),
    )
  }
  private async continueOrFallback(
    action: 'continue' | 'fallback',
    options: NativeRouteContinueOptions,
  ): Promise<void> {
    const parsed = ContinueSchema.parse(options)
    const params: Record<string, UntrustedInput> = { action }
    if (parsed.url !== undefined) params.url = parsed.url
    if (parsed.method !== undefined) params.method = parsed.method
    if (parsed.headers !== undefined) params.headers = headerArray(parsed.headers)
    if (parsed.postData !== undefined) {
      const data = Buffer.from(parsed.postData)
      if (data.length > MAX_ROUTE_BODY) throw new Error('Native route body exceeds 2 MiB')
      if (data.length <= 24 * 1024) params.postData = data.toString('base64')
      else {
        await this.upload(data)
        params.bodyUpload = true
      }
    }
    await this.resolve(params)
  }
  async fulfill(options: NativeRouteFulfillOptions = {}): Promise<void> {
    await this.handle(() => this.fulfillResponse(options))
  }
  private async fulfillResponse(options: NativeRouteFulfillOptions): Promise<void> {
    const parsed = FulfillSchema.parse(options)
    if (
      [parsed.body !== undefined, parsed.path !== undefined, Object.hasOwn(parsed, 'json')].filter(
        Boolean,
      ).length > 1
    )
      throw new Error('Provide one of body, path or json for route fulfillment')
    const headers = {
      ...normalizeHeaders(parsed.response?.headers() ?? {}),
      ...normalizeHeaders(parsed.headers ?? {}),
    }
    let body = parsed.response ? await parsed.response.body() : Buffer.alloc(0)
    if (parsed.body !== undefined) body = Buffer.from(parsed.body)
    else if (parsed.path !== undefined) {
      const file = await open(parsed.path, 'r')
      try {
        const allocation = Buffer.alloc(MAX_ROUTE_BODY + 1)
        let length = 0
        while (length < allocation.length) {
          const chunk = await file.read(allocation, length, allocation.length - length, null)
          if (!chunk.bytesRead) break
          length += chunk.bytesRead
        }
        if (length > MAX_ROUTE_BODY) throw new Error('Native route body exceeds 2 MiB')
        body = allocation.subarray(0, length)
      } finally {
        await file.close()
      }
    } else if (Object.hasOwn(parsed, 'json')) {
      const json = JSON.stringify(parsed.json)
      if (json === undefined) throw new Error('Route JSON must be serializable')
      body = Buffer.from(json)
      headers['content-type'] ??= 'application/json'
    }
    if (body.length > MAX_ROUTE_BODY) throw new Error('Native route body exceeds 2 MiB')
    if (parsed.contentType !== undefined) headers['content-type'] = parsed.contentType
    // The replaced body has its own framing; stale upstream encodings cannot describe it.
    delete headers['content-encoding']
    delete headers['transfer-encoding']
    headers['content-length'] = String(body.length)
    if (body.length > 24 * 1024) await this.upload(body)
    await this.resolve({
      action: 'fulfill',
      status: parsed.status ?? parsed.response?.status() ?? 200,
      ...(parsed.statusText === undefined ? {} : { statusText: parsed.statusText }),
      headers: headerArray(headers),
      ...(body.length <= 24 * 1024 ? { body: body.toString('base64') } : { bodyUpload: true }),
    })
  }
  private async upload(body: Buffer, fetchOptions?: Record<string, UntrustedInput>): Promise<void> {
    await this.command({
      action: 'resetBody',
      routeId: this.id,
      ...(fetchOptions === undefined ? {} : { fetchOptions: { ...fetchOptions } }),
    })
    for (let offset = 0; offset < body.length; offset += 24 * 1024)
      await this.command({
        action: 'bodyChunk',
        routeId: this.id,
        body: body.subarray(offset, offset + 24 * 1024).toString('base64'),
      })
  }
  private async resolve(config: Record<string, UntrustedInput>): Promise<void> {
    await this.command({ ...config, routeId: this.id, traceStack: this.actionTraceStack })
  }
  private async command(config: Record<string, UntrustedInput>): Promise<UntrustedInput> {
    try {
      return await this.routeCommand(
        this.callbackScope === undefined
          ? config
          : { ...config, callbackScope: this.callbackScope },
      )
    } catch (error) {
      throw nativeRouteCommandFailed(error as UntrustedInput)
    }
  }
  private async handle(work: () => Promise<void>): Promise<void> {
    if (this.resolved || this.handling) throw new Error('Route is already handled')
    this.handling = true
    this.actionTraceStack = nativeTraceStack()
    try {
      await work()
      this.resolved = true
    } finally {
      this.handling = false
      this.actionTraceStack = []
    }
  }
  isHandled(): boolean {
    return this.resolved || this.handling
  }
}

export type NativeNetworkEventName = 'request' | 'response' | 'requestfinished' | 'requestfailed'
type RequestListener = (request: NativeRequest) => void
type ResponseListener = (response: NativeResponse) => void
export type NativeRouteHandler = (
  route: NativeRoute,
  request: NativeRequest,
) => Promise<void> | void
export type NativeNetworkURLMatcher = string | RegExp | ((url: URL) => boolean)
const sourceNetworks = new WeakMap<NativeBrowser, WeakRef<NativeNetwork>>()
/** An actual native owner channel; context workers have no Page/Frame. */
export interface NativeNetworkTransport {
  readonly signal: AbortSignal
  readonly processSignal?: AbortSignal
  readonly pageSignal?: AbortSignal
  readonly predicateSource?: NativeBrowser
  defaultTimeout(): number
  command(config: Record<string, UntrustedInput>, signal?: AbortSignal): Promise<UntrustedInput>
  body(info: NativeNetworkResponseInfo, request: NativeRequest | undefined): Promise<UntrustedInput>
  takeBody?(id: string, info: NativeNetworkRequestInfo): Buffer | undefined
  worker?(info: NativeWorkerInfo): NativeWorker | undefined
  subscribe?(listener: (event: NativeNetworkInfo) => void): () => void
  networkSequence?(): number
  contextRoute?(id: string, exhausted: boolean): NativeRouteHandler | undefined
  contextMatcher?(id: string): NativeRouteMatcherRegistration | undefined
  routeCallback?(action: 'start' | 'finish', owner: NativeRouteCallbackLease): Promise<void>
}
export class NativeNetwork {
  static forPage(owner: NativePage): NativeNetwork {
    const existing = sourceNetworks.get(owner.source)?.deref()
    if (existing) return existing
    const network = new NativeNetwork(owner)
    sourceNetworks.set(owner.source, new WeakRef(network))
    return network
  }
  /** Internal bridge to the same real Source HTTP response cache. */
  routeFetchSource(): NativeBrowser {
    const source = this.transport.predicateSource
    if (!source) throw new Error('Native route fetch requires its actual Source HTTP channel')
    return source
  }
  private readonly requests = new Map<string, NativeRequest>()
  private readonly requestHistory = new Map<string, WeakRef<NativeRequest>>()
  private readonly requestCosts = new Map<string, number>()
  private requestBytes = 0
  private linkedRequestBytes = 0
  private readonly listeners = new Map<
    NativeNetworkEventName,
    ((value: NativeRequest | NativeResponse) => void)[]
  >()
  private readonly handlers = new Map<
    string,
    { policy: NativeRouteURLMatcherPolicy; handler: NativeRouteHandler }
  >()
  // This book is private to Page callbacks. Context observations never consume
  // its host wait quota or borrow its actual scope/token selection.
  private readonly pageInvocations = new NativeContextRouteInvocations()
  private readonly pageRemovals: NativePageRouteRemovals
  private readonly routeErrors: Error[] = []
  private lastRouteSequence = 0
  private readonly detach: (() => void) | undefined
  private readonly transport: NativeNetworkTransport
  constructor(
    readonly owner: NativePage | undefined,
    source: NativeBrowser | undefined = owner?.source,
    transport?: NativeNetworkTransport,
  ) {
    if (transport) this.transport = transport
    else {
      if (!owner || !source) throw new Error('Native network requires an actual owner channel')
      this.transport = {
        get signal() {
          return source.signal
        },
        pageSignal: source.pageSignal ?? source.signal,
        processSignal: source.processSignal ?? source.signal,
        predicateSource: source,
        defaultTimeout: () => owner.defaultTimeout?.() ?? getNativeDefaultTimeout(source),
        command: (config, signal) => source.runAutomation('network', config, signal),
        body: (info) =>
          owner.runAutomation('responseBody', {
            session: info.session,
            requestId: info.requestId,
          }),
        worker: (info) => nativeContextPageBindings.get(owner)?.context.workerFromNetwork(info),
        contextRoute: (id, exhausted) =>
          nativeContextPageBindings.get(owner)?.context.contextRouteHandler(id, exhausted),
        contextMatcher: (id) =>
          nativeContextPageBindings.get(owner)?.context.contextRouteMatcher(id),
        takeBody: (id) => source.takeNetworkBody?.(id),
        subscribe: (listener) => source.onNetworkMessage?.(listener) ?? (() => undefined),
        networkSequence: () => source.networkSequence?.() ?? 0,
        routeCallback: async (action, lease) => {
          await source.runAutomation(
            'routeCallback',
            { action, ...lease },
            source.processSignal ?? source.signal,
          )
        },
      }
    }
    this.pageRemovals = new NativePageRouteRemovals(
      this.pageInvocations,
      (params) => this.command(params),
      (ids) => {
        for (const id of ids) this.dropRoute(id)
      },
    )
    this.detach = this.transport.subscribe?.((event) => this.receive(event))
    const closed = () => {
      this.detach?.()
      this.listeners.clear()
      this.clearRouteHandlers()
      this.pageRemovals.close()
      this.requests.clear()
      // Bounded weak identities preserve held Request objects when a native
      // context reply arrives after its page-close announcement.
      this.requestCosts.clear()
      this.requestBytes = 0
      this.linkedRequestBytes = 0
    }
    const lifetime = this.transport.pageSignal ?? this.transport.signal
    lifetime.addEventListener('abort', closed, { once: true })
    if (lifetime.aborted) {
      lifetime.removeEventListener('abort', closed)
      closed()
    }
  }
  takeBody(id: string, info: NativeNetworkRequestInfo): Buffer | undefined {
    try {
      return this.transport.takeBody?.(id, info)
    } catch {
      return undefined
    }
  }
  findServiceWorker(info: NativeNetworkRequestInfo): NativeWorker | null {
    if (!info.serviceWorker || info.workerInfo?.type !== 'service_worker') return null
    return this.transport.worker?.(info.workerInfo) ?? null
  }
  findFrame(id: string): NativeFrame | undefined {
    return this.owner?.frames().find((item) => item.identity() === id)
  }
  navigationResponse(info: UntrustedInput): NativeResponse {
    const parsed = NativeNetworkResponseSchema.parse(info)
    let request: NativeRequest | undefined
    for (const reference of this.requestHistory.values()) {
      const item = reference.deref()
      if (
        item?.info.requestId === parsed.requestId &&
        item.info.session === parsed.session &&
        item.info.url === parsed.url
      )
        request = item
    }
    if (!request) throw new Error('Native navigation request metadata was not observed')
    return request.existingResponse() ?? this.createResponse(request, parsed)
  }
  createResponse(
    request: NativeRequest,
    info: UntrustedInput,
    sequence?: number,
    fromNativeEvent = sequence !== undefined,
  ): NativeResponse {
    const observedSequence = sequence ?? request.sequence()
    const existing = request.knownResponse()
    if (existing) {
      existing.observe(info, observedSequence, fromNativeEvent)
      return existing
    }
    const response = new NativeResponse(
      this,
      request.knownFrame() ?? this.findFrame(request.info.frameId),
      info,
      request,
      observedSequence,
      fromNativeEvent,
    )
    request.update(response)
    this.retainRequest(request)
    return response
  }
  command(config: Record<string, UntrustedInput>, signal?: AbortSignal): Promise<UntrustedInput> {
    try {
      return this.transport
        .command(
          {
            timeoutMs: this.transport.defaultTimeout(),
            ...config,
          },
          signal ? AbortSignal.any([this.transport.signal, signal]) : this.transport.signal,
        )
        .catch((error: UntrustedInput) => {
          throw nativeRouteCommandFailed(error)
        })
    } catch (error) {
      return Promise.reject(nativeRouteCommandFailed(error as UntrustedInput))
    }
  }
  responseBody(info: NativeNetworkResponseInfo, request?: NativeRequest): Promise<UntrustedInput> {
    try {
      return this.transport.body(info, request).catch((error: UntrustedInput) => {
        throw nativeRouteCommandFailed(error)
      })
    } catch (error) {
      return Promise.reject(nativeRouteCommandFailed(error as UntrustedInput))
    }
  }
  on(event: 'response', listener: ResponseListener, prepend?: boolean): void
  on(
    event: Exclude<NativeNetworkEventName, 'response'>,
    listener: RequestListener,
    prepend?: boolean,
  ): void
  on(
    event: NativeNetworkEventName,
    listener: RequestListener | ResponseListener,
    prepend = false,
  ): void {
    const list = this.listeners.get(event) ?? []
    if (prepend) list.unshift(listener as (value: NativeRequest | NativeResponse) => void)
    else list.push(listener as (value: NativeRequest | NativeResponse) => void)
    this.listeners.set(event, list)
  }
  off(event: NativeNetworkEventName, listener: RequestListener | ResponseListener): void {
    const list = this.listeners.get(event)
    const index =
      list?.lastIndexOf(listener as (value: NativeRequest | NativeResponse) => void) ?? -1
    if (index >= 0) list?.splice(index, 1)
  }
  private dropRoute(id: string): void {
    this.handlers.get(id)?.policy.dispose()
    this.handlers.delete(id)
  }
  private clearRouteHandlers(): void {
    for (const entry of this.handlers.values()) entry.policy.dispose()
    this.handlers.clear()
  }
  async route(
    url: NativeNetworkURLMatcher,
    handler: NativeRouteHandler,
    options: { times?: number } = {},
  ): Promise<void> {
    const parsed = z
      .strictObject({
        times: z.number().int().positive().max(1_000_000).optional(),
      })
      .parse(options)
    if (typeof handler !== 'function') throw new Error('Native route requires a handler')
    const registrationId = randomUUID()
    const policy = new NativeRouteURLMatcherPolicy(url)
    this.handlers.set(registrationId, { policy, handler })
    try {
      await this.command({
        action: 'registerRoute',
        traceStack: nativeTraceStack(),
        registrationId,
        url: policy.nativePattern,
        ...(policy.host ? { hostMatcher: true } : {}),
        times: parsed.times ?? 0,
      })
    } catch (error) {
      this.dropRoute(registrationId)
      throw error
    }
  }
  async unroute(url: NativeNetworkURLMatcher, handler?: NativeRouteHandler): Promise<void> {
    for (const [id, entry] of [...this.handlers])
      if (
        entry.policy.matchesRegistration(url) &&
        (handler === undefined || handler === entry.handler)
      ) {
        await this.command({ action: 'removeRoute', registrationId: id })
        this.dropRoute(id)
      }
  }
  async unrouteAll(
    options: { behavior?: 'default' | 'wait' | 'ignoreErrors' } = {},
  ): Promise<void> {
    const { behavior = 'default' } = z
      .strictObject({ behavior: z.enum(['default', 'wait', 'ignoreErrors']).optional() })
      .parse(options)
    if (behavior !== 'default') return this.pageRemovals.remove(behavior)
    await this.command({ action: 'removeRoute', registrationId: '' })
    this.clearRouteHandlers()
  }
  async setExtraHTTPHeaders(headers: Record<string, string>): Promise<void> {
    const parsed = HeaderSchema.parse(headers)
    headerArray(parsed)
    await this.command({ action: 'setExtraHTTPHeaders', headers: parsed })
  }
  async setOffline(offline: boolean): Promise<void> {
    await this.command({
      action: 'setOffline',
      offline: z.boolean().parse(offline),
    })
  }
  waitForRequest(
    matcher: string | RegExp | ((request: NativeRequest) => boolean | Promise<boolean>),
    options: NetworkWaitOptions = {},
  ): Promise<NativeRequest> {
    return this.wait('request', matcher, options) as Promise<NativeRequest>
  }
  waitForResponse(
    matcher: string | RegExp | ((response: NativeResponse) => boolean | Promise<boolean>),
    options: NetworkWaitOptions = {},
  ): Promise<NativeResponse> {
    return this.wait('response', matcher, options) as Promise<NativeResponse>
  }
  waitForEvent(
    event: 'response',
    options?: {
      timeout?: number | undefined
      signal?: AbortSignal | undefined
      predicate?: (response: NativeResponse) => boolean | Promise<boolean>
    },
  ): Promise<NativeResponse>
  waitForEvent(
    event: Exclude<NativeNetworkEventName, 'response'>,
    options?: {
      timeout?: number | undefined
      signal?: AbortSignal | undefined
      predicate?: (request: NativeRequest) => boolean | Promise<boolean>
    },
  ): Promise<NativeRequest>
  waitForEvent(
    event: NativeNetworkEventName,
    options?: {
      timeout?: number | undefined
      signal?: AbortSignal | undefined
      predicate?: (item: NativeRequest | NativeResponse) => boolean | Promise<boolean>
    },
  ): Promise<NativeRequest | NativeResponse>
  waitForEvent(
    event: NativeNetworkEventName,
    options: {
      timeout?: number | undefined
      signal?: AbortSignal | undefined
      predicate?: (item: NativeRequest | NativeResponse) => boolean | Promise<boolean>
    } = {},
  ): Promise<NativeRequest | NativeResponse> {
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
        predicate: z
          .custom<(item: NativeRequest | NativeResponse) => boolean | Promise<boolean>>(
            (value) => typeof value === 'function',
          )
          .optional(),
      })
      .parse(options)
    return this.wait(event, config.predicate ?? '', {
      timeout: config.timeout,
      signal: config.signal,
    })
  }
  once(event: 'response', listener: ResponseListener): void
  once(event: Exclude<NativeNetworkEventName, 'response'>, listener: RequestListener): void
  once(event: NativeNetworkEventName, listener: RequestListener | ResponseListener): void {
    const callback = (item: NativeRequest | NativeResponse): void => {
      this.off(event, callback)
      if (item instanceof NativeResponse) (listener as ResponseListener)(item)
      else (listener as RequestListener)(item)
    }
    const list = this.listeners.get(event) ?? []
    list.push(callback)
    this.listeners.set(event, list)
  }
  takeRouteErrors(): Error[] {
    return this.routeErrors.splice(0)
  }
  private async wait(
    event: NativeNetworkEventName,
    matcher: string | RegExp | ((item: never) => boolean | Promise<boolean>),
    options: NetworkWaitOptions,
  ): Promise<NativeRequest | NativeResponse> {
    const parsed = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
      })
      .parse(options)
    const timeout = parsed.timeout ?? this.transport.defaultTimeout()
    if (typeof matcher !== 'string') {
      if (!this.transport.predicateSource)
        throw new Error(
          'Context worker network predicates require a native context predicate owner',
        )
      return waitNativeEventPredicate(
        this.transport.predicateSource,
        event,
        (value) => this.eventValue(event, value),
        async (item) =>
          matcher instanceof RegExp
            ? new RegExp(matcher.source, matcher.flags).test(item.url())
            : await matcher(item as never),
        { timeout, signal: parsed.signal },
      )
    }
    return this.eventValue(
      event,
      await this.command(
        {
          action: 'waitEvent',
          event,
          url: matcher,
          afterSequence: this.transport.networkSequence?.() ?? 0,
          timeoutMs: timeout,
        },
        parsed.signal,
      ),
    )
  }
  eventValue(
    event: NativeNetworkEventName | 'requestupdated',
    value: UntrustedInput,
  ): NativeRequest | NativeResponse {
    const data = NativeNetworkInfoSchema.parse(value)
    if (data.event !== event) throw new Error('Native network wait returned a different event')
    const request = this.request(data.request, data.sequence)
    const response = data.response
      ? this.createResponse(request, data.response, data.sequence)
      : undefined
    request.update(
      response,
      data.errorText,
      undefined,
      event === 'requestfinished' || event === 'requestfailed',
    )
    const item = event === 'response' ? response : request
    if (!item) throw new Error('Native response wait returned no response metadata')
    if (item instanceof NativeResponse) request.update(item)
    this.retainRequest(request)
    return item
  }
  private request(info: NativeNetworkRequestInfo, sequence: number): NativeRequest {
    const old = this.requests.get(info.id) ?? this.requestHistory.get(info.id)?.deref()
    if (old) {
      old.observe(info, sequence)
      return old
    }
    const request = new NativeRequest(this, info, sequence)
    if (info.redirectedFrom)
      request.update(
        undefined,
        undefined,
        this.requests.get(info.redirectedFrom) ??
          this.requestHistory.get(info.redirectedFrom)?.deref(),
      )
    return request
  }
  metadataDiagnostics(): {
    cachedRequests: number
    cachedRequestBytes: number
    weakRequestIdentities: number
  } {
    return {
      cachedRequests: this.requests.size,
      cachedRequestBytes: this.requestBytes + this.linkedRequestBytes,
      weakRequestIdentities: this.requestHistory.size,
    }
  }
  private retainRequest(request: NativeRequest): void {
    if (this.transport.signal.aborted) {
      this.requestHistory.set(request.info.id, new WeakRef(request))
      while (this.requestHistory.size > 8192) {
        const oldest = this.requestHistory.keys().next().value
        if (oldest === undefined) break
        this.requestHistory.delete(oldest)
      }
      return
    }
    const cost = request.ownMetadataBytes()
    this.requestBytes += cost - (this.requestCosts.get(request.info.id) ?? 0)
    this.requestCosts.set(request.info.id, cost)
    this.requests.set(request.info.id, request)
    this.requestHistory.set(request.info.id, new WeakRef(request))
    while (this.requestHistory.size > 8192) {
      const id = this.requestHistory.keys().next().value
      if (id === undefined) break
      this.requestHistory.delete(id)
    }
    if (
      !request.hasRedirectLinks() &&
      this.requestBytes + this.linkedRequestBytes <= 32 * 1024 * 1024 &&
      this.requests.size <= 4096
    )
      return
    // Redirect links can keep evicted wrappers alive. Measure the shared graph
    // under pressure instead of crediting a removal that did not free metadata.
    // Only this synchronous admission shares byte measurements; each removal
    // still recomputes reachability, and later observations measure afresh.
    const measurements = new Map<NativeRequest, number>([[request, cost]])
    while (true) {
      const visited = new Set<NativeRequest>()
      let retained = 0
      for (const item of this.requests.values())
        retained += item.retainedMetadataBytes(visited, measurements)
      if (retained <= 32 * 1024 * 1024 && this.requests.size <= 4096) {
        this.linkedRequestBytes = retained - this.requestBytes
        break
      }
      let oldest: string | undefined
      for (const [id, item] of this.requests)
        if (id !== request.info.id && item.hasFinished()) {
          oldest = id
          break
        }
      if (oldest === undefined)
        throw new Error('Native request binding metadata exceeds 32 MiB or 4096 active records')
      this.requests.delete(oldest)
      this.requestBytes -= this.requestCosts.get(oldest) ?? 0
      this.requestCosts.delete(oldest)
    }
  }
  private receiveMatcher(
    event: Extract<NativeNetworkInfo, { event: 'routematch' }>,
    command?: (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>,
  ): void {
    const actual = event.matcher,
      lease = { invocation: actual.id, scope: actual.callbackScope, token: actual.callbackToken },
      page = this.handlers.get(actual.registrationId),
      context = page ? undefined : this.transport.contextMatcher?.(actual.registrationId),
      callback = page?.policy.host ? page.policy.callback() : context?.callback,
      processSignal = this.transport.processSignal ?? this.transport.signal,
      late: Error[] = []
    let nativeSkipped = false
    const acknowledge = async (action: 'start' | 'finish') => {
      try {
        if (!this.transport.routeCallback)
          throw new Error('Native URL matcher callback owner channel is unavailable')
        await this.transport.routeCallback(action, lease)
      } catch (error) {
        throw nativeRouteCommandFailed(error as UntrustedInput)
      }
    }
    const decide = async (matched: boolean | undefined) => {
      try {
        const params = {
          action: 'matchRoute',
          routeId: actual.id,
          registrationId: actual.registrationId,
          callbackScope: actual.callbackScope,
          callbackToken: actual.callbackToken,
          ...(matched === undefined ? { invalid: true } : { matched }),
        }
        const result = command ? await command(params) : await this.command(params)
        nativeSkipped =
          z.strictObject({ skipped: z.literal(true).optional() }).parse(result).skipped === true
      } catch (error) {
        throw nativeRouteCommandFailed(error as UntrustedInput)
      }
    }
    const invoke = async () => {
      if (!callback) {
        // Missing host state is not an invented false predicate result. Retire
        // the unstarted receipt, then report unavailable to the actual owner.
        if (!processSignal.aborted) await acknowledge('finish')
        this.transport.signal.throwIfAborted()
        await decide(undefined)
        if (nativeSkipped) return
        throw nativeRouteCommandFailed(
          new Error('Actual native URL matcher registration is unavailable'),
        )
      }
      await invokeNativeRouteURLMatcher(
        {
          signal: this.transport.signal,
          processSignal,
          start: () => acknowledge('start'),
          finish: () => acknowledge('finish'),
        },
        lease,
        callback,
        actual.url,
        decide,
        (error) => late.push(error),
      )
    }
    const observed = page
      ? this.pageInvocations.resolveCallback(
          nativePageRouteCallbackIdentity(lease.scope, lease.token),
          invoke,
        )
      : (context?.observe(invoke) ?? invoke)
    const physical = observed()
    nativeRouteHandlerInvoked(observed, physical)
    void physical.catch((error: UntrustedInput) => {
      this.recordRouteError(error)
      for (const original of late) this.recordRouteError(original)
    })
  }
  receiveRoute(
    value: UntrustedInput,
    command: (config: Record<string, UntrustedInput>) => Promise<UntrustedInput>,
  ): void {
    const event = NativeNetworkInfoSchema.parse(value)
    if (event.event !== 'route' && event.event !== 'routematch')
      throw new Error('Native context route delivered a different event')
    this.receive(event, command)
  }
  private receive(
    event: NativeNetworkInfo,
    routeCommand?: (config: Record<string, UntrustedInput>) => Promise<UntrustedInput>,
  ): void {
    if (event.event === 'routemutation') {
      this.pageRemovals.receive(event)
      return
    }
    if (event.event === 'route' || event.event === 'routematch') {
      // The context owner envelope precedes its optional Page packet in the
      // same native publication stream. Both project into this canonical cache.
      if (event.sequence <= this.lastRouteSequence) return
      this.lastRouteSequence = event.sequence
    }
    if (event.event === 'routematch') {
      this.receiveMatcher(event, routeCommand)
      return
    }
    if (event.event === 'routeerror') {
      this.recordRouteError(new Error(event.errorText))
      return
    }
    if (event.event === 'routeexpired') {
      this.recordRouteError(
        new Error(`Native route ${event.routeId} exceeded its 30 second deadline`),
      )
      return
    }
    const request = this.request(event.request, event.sequence)
    const response = event.response
      ? this.createResponse(request, event.response, event.sequence)
      : undefined
    request.update(
      response,
      event.errorText,
      undefined,
      event.event === 'requestfinished' || event.event === 'requestfailed',
    )
    this.retainRequest(request)
    if (event.event === 'requestupdated') return
    if (event.event === 'route') {
      if (!event.route) throw new Error('Native route event has no interception ID')
      const pageHandler = this.handlers.get(event.route.registrationId)?.handler
      let handler =
        pageHandler ??
        this.transport.contextRoute?.(event.route.registrationId, event.route.exhausted ?? false)
      if (event.route.exhausted) this.dropRoute(event.route.registrationId)
      const route = new NativeRoute(
        this,
        event.route.id,
        request,
        routeCommand,
        event.route.callbackScope,
      )
      const processSignal = this.transport.processSignal ?? this.transport.signal
      const lease: NativeRouteCallbackLease | undefined =
        event.route.callbackScope && event.route.callbackToken
          ? {
              invocation: event.route.id,
              scope: event.route.callbackScope,
              token: event.route.callbackToken,
            }
          : undefined
      if (pageHandler && lease)
        handler = this.pageInvocations.resolve(
          nativePageRouteCallbackIdentity(lease.scope, lease.token),
          pageHandler,
        )
      const acknowledge = async (action: 'start' | 'finish', owner: NativeRouteCallbackLease) => {
        if (!this.transport.routeCallback)
          throw new Error('Native route callback owner channel is unavailable')
        await this.transport.routeCallback(action, owner)
      }
      const invoke = async () => {
        if (!lease) throw new Error('Native route event has no physical callback ownership lease')
        if (!handler) {
          try {
            await route.resolveAutomatically('fallback')
          } finally {
            if (!processSignal.aborted) await acknowledge('finish', lease)
          }
          return
        }
        await invokeNativeRouteCallback(
          {
            signal: this.transport.signal,
            processSignal,
            start: (owner) => acknowledge('start', owner),
            finish: (owner) => acknowledge('finish', owner),
          },
          lease,
          () => handler(route, request),
        )
      }
      const invocation = invoke()
      void invocation.catch(async (error: UntrustedInput) => {
        this.recordRouteError(error)
        if (!route.isHandled())
          try {
            await route.resolveAutomatically('abort')
          } catch (abortError) {
            this.recordRouteError(abortError as UntrustedInput)
          }
      })
      nativeRouteHandlerInvoked(handler, invocation)
      return
    }
    const value = event.event === 'response' ? response : request
    if (!value) throw new Error('Native response event has no metadata')
    for (const listener of [...(this.listeners.get(event.event) ?? [])]) listener(value)
  }
  private recordRouteError(error: UntrustedInput): void {
    if (this.routeErrors.length >= 128) this.routeErrors.shift()
    this.routeErrors.push(toError(error))
  }
}
