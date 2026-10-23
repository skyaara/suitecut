// SPDX-License-Identifier: MIT
/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase lifecycle module edges. */
import { Buffer } from 'node:buffer'
import { randomUUID } from 'node:crypto'
import { ReadStream } from 'node:fs'
import { mkdir, writeFile } from 'node:fs/promises'
import { dirname } from 'node:path'
import { inspect } from 'node:util'

import * as z from 'zod'

import {
  bindNativeRetryContinuation,
  retainNativeRetryOperation,
  runNativeRetryCleanup,
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import { nativeCookieFilter, type NativeCookieFilter } from './native-cookie-filter.js'
import { NativeHTTPLookups, type NativeHTTPLookup } from './native-http-lookup.js'
import { isNativeHTTPRequest } from './native-http-request-input.js'
import { hasNativeMultipartStream, NativeMultipartStreamJob } from './native-http-stream.js'
import { runNativeMatcherUserPromise } from './native-soft-assertions.js'
import {
  captureNativeStorageState,
  readNativeStorageState,
  restoreNativeStorageState,
  NativeStorageStateSchema,
  NativeStorageStateInputSchema,
  type NativeStorageState,
} from './native-storage-state.js'
import { nativeTraceStack } from './native-trace-metadata.js'
import { NativeTracing } from './native-tracing.js'

import type { NativeBrowser } from './native-browser.js'
import type { NativeCookieInput } from './native-context.js'
import type { NativeRequest } from './native-network.js'
import type { UntrustedInput } from './untrusted.js'

const REQUEST_CONTEXT_CLOSED =
  'Target page, context or browser has been closed (Request context disposed.)'
const MAX_UPLOAD = 8 * 1024 * 1024
const HeadersSchema = z.record(z.string(), z.string())
const Scalar = z.union([z.string(), z.number().finite(), z.boolean()])
const CredentialsSchema = z.strictObject({
  username: z.string().max(8192),
  password: z.string().max(8192),
  origin: z.url().optional(),
  send: z.enum(['always', 'unauthorized']).optional(),
})
const HTTPContextOptionsSchema = z.strictObject({
  storageState: z.union([z.string(), NativeStorageStateInputSchema]).optional(),
  baseURL: z.string().optional(),
  extraHTTPHeaders: HeadersSchema.optional(),
  userAgent: z.string().max(8192).optional(),
  timeout: z.number().int().min(0).max(120_000).optional(),
  httpCredentials: z.union([CredentialsSchema, z.array(CredentialsSchema).max(64)]).optional(),
  extraCACertificates: z
    .array(
      z
        .string()
        .min(1)
        .max(32 * 1024),
    )
    .max(16)
    .refine(
      (values) => values.reduce((bytes, value) => bytes + Buffer.byteLength(value), 0) <= 32 * 1024,
      'Extra CA certificates exceed 32 KiB',
    )
    .optional(),
  ignoreHTTPSErrors: z.boolean().optional(),
  proxy: z
    .strictObject({
      server: z.string().min(1).max(4096),
      bypass: z.string().max(4096).optional(),
      username: z.string().max(8192).optional(),
      password: z.string().max(8192).optional(),
    })
    .optional(),
  failOnStatusCode: z.boolean().optional(),
  maxRedirects: z
    .number()
    .int()
    .min(0, "'maxRedirects' must be greater than or equal to '0'")
    .max(100)
    .optional(),
  maxRetries: z.number().int().min(0).max(100).optional(),
})
// Generic runner context defaults may accompany a request-context creation.
// Validate recognized browser-only fields without claiming page emulation or
// offline behavior for an HTTP client. Only HTTP policy reaches native create.
const ContextOptionsSchema = HTTPContextOptionsSchema.extend({
  acceptDownloads: z.boolean().optional(),
  bypassCSP: z.boolean().optional(),
  javaScriptEnabled: z.boolean().optional(),
  serviceWorkers: z.enum(['allow', 'block']).optional(),
  colorScheme: z.enum(['light', 'dark', 'no-preference']).nullable().optional(),
  contrast: z.enum(['more', 'no-preference']).nullable().optional(),
  forcedColors: z.enum(['active', 'none']).nullable().optional(),
  reducedMotion: z.enum(['reduce', 'no-preference']).nullable().optional(),
  hasTouch: z.boolean().optional(),
  isMobile: z.boolean().optional(),
  offline: z.boolean().optional(),
  locale: z.string().min(1).max(128).optional(),
  timezoneId: z.string().min(1).max(256).optional(),
  deviceScaleFactor: z.number().finite().min(0.25).max(4).optional(),
  viewport: z
    .strictObject({
      width: z.number().int().positive().max(3840),
      height: z.number().int().positive().max(2160),
    })
    .nullable()
    .optional(),
  screen: z
    .strictObject({
      width: z.number().int().positive().max(3840),
      height: z.number().int().positive().max(2160),
    })
    .optional(),
}).transform((config) =>
  withoutNativeRetryOperation(() => HTTPContextOptionsSchema.strip().parse(config)),
)
export type NativeAPIRequestContextOptions = z.input<typeof ContextOptionsSchema>
export interface NativeMultipartFile {
  name: string
  mimeType?: string | undefined
  buffer: Buffer
}
export interface NativeAPIRequestOptions {
  lookup?: NativeHTTPLookup
  method?: string
  headers?: Record<string, string>
  params?: Record<string, string | number | boolean | undefined> | URLSearchParams | string
  data?: UntrustedInput
  form?: Record<string, string | number | boolean> | URLSearchParams | FormData
  multipart?:
    Record<string, string | number | boolean | NativeMultipartFile | ReadStream> | FormData
  timeout?: number
  failOnStatusCode?: boolean
  maxRedirects?: number
  maxRetries?: number
  ignoreHTTPSErrors?: boolean
  signal?: AbortSignal
}
const OptionsSchema = z.strictObject({
  lookup: z.custom<NativeHTTPLookup>((value) => typeof value === 'function').optional(),
  method: z.string().min(1).max(32).optional(),
  headers: HeadersSchema.optional(),
  params: z
    .union([z.record(z.string(), Scalar.optional()), z.instanceof(URLSearchParams), z.string()])
    .optional(),
  data: z.unknown().optional(),
  form: z
    .union([z.record(z.string(), Scalar), z.instanceof(URLSearchParams), z.instanceof(FormData)])
    .optional(),
  multipart: z
    .union([
      z.record(
        z.string(),
        z.union([
          Scalar,
          z.instanceof(ReadStream),
          z.strictObject({
            name: z.string(),
            mimeType: z.string().optional(),
            buffer: z.instanceof(Buffer),
          }),
        ]),
      ),
      z.instanceof(FormData),
    ])
    .optional(),
  timeout: z.number().int().min(0).max(120_000).optional(),
  failOnStatusCode: z.boolean().optional(),
  maxRedirects: z
    .number()
    .int()
    .min(0, "'maxRedirects' must be greater than or equal to '0'")
    .max(100)
    .optional(),
  maxRetries: z.number().int().min(0).max(100).optional(),
  ignoreHTTPSErrors: z.boolean().optional(),
  signal: z.instanceof(AbortSignal).optional(),
})
function headersArray(headers: Record<string, string>): { name: string; value: string }[] {
  return Object.entries(headers).map(([name, value]) => {
    if (!name || !/^[!#$%&'*+.^_`|~A-Za-z0-9-]+$/u.test(name) || /[\r\n\0]/u.test(value))
      throw new Error('Invalid HTTP header name or value')
    return { name, value }
  })
}
function mergeHeaders(...sources: Record<string, string>[]): Record<string, string> {
  const result = new Map<string, [string, string]>()
  for (const source of sources)
    for (const [name, value] of Object.entries(source))
      result.set(name.toLowerCase(), [name, value])
  return Object.fromEntries(result.values())
}
function headerValue(headers: Record<string, string>, name: string): string | undefined {
  const key = Object.keys(headers).find((key) => key.toLowerCase() === name)
  return key === undefined ? undefined : headers[key]
}
function defaultHeader(headers: Record<string, string>, name: string, value: string): void {
  if (headerValue(headers, name) === undefined) headers[name] = value
}
async function multipartInput(
  value: NonNullable<z.infer<typeof OptionsSchema>['multipart']>,
): Promise<{ body: Buffer; parts: Record<string, UntrustedInput>[] }> {
  const chunks: Buffer[] = []
  const parts: Record<string, UntrustedInput>[] = []
  let total = 0
  const entries: [string, string | number | boolean | NativeMultipartFile | Blob][] = []
  if (value instanceof FormData) value.forEach((item, name) => entries.push([name, item]))
  else {
    for (const [name, item] of Object.entries(value)) {
      if (item instanceof ReadStream)
        throw new Error('Native ReadStream requires prepared multipart upload')
      entries.push([name, item])
    }
  }
  if (entries.length > 1024) throw new Error('Native multipart supports at most 1024 parts')
  for (const [name, item] of entries) {
    let buffer: Buffer, filename: string | undefined, mime: string | undefined
    if (typeof item === 'object' && !(item instanceof Blob)) {
      buffer = item.buffer
      filename = item.name
      mime = item.mimeType ?? 'application/octet-stream'
    } else if (typeof item === 'object' && item instanceof Blob) {
      if (item.size > MAX_UPLOAD) throw new Error('Native API upload exceeds 8 MiB')
      buffer = Buffer.from(await item.arrayBuffer())
      filename = item instanceof File ? item.name : 'blob'
      mime = item.type || 'application/octet-stream'
    } else buffer = Buffer.from(String(item))
    if (buffer.length > MAX_UPLOAD - total) throw new Error('Native API upload exceeds 8 MiB')
    parts.push({
      name,
      offset: total,
      size: buffer.length,
      ...(filename === undefined ? {} : { filename, mimeType: mime ?? 'application/octet-stream' }),
    })
    total += buffer.length
    chunks.push(buffer)
  }
  if (Buffer.byteLength(JSON.stringify(parts)) > 48 * 1024)
    throw new Error('Native multipart metadata exceeds 48 KiB')
  return { body: Buffer.concat(chunks, total), parts }
}
const ResponseTimingSchema = z.strictObject({
  startTime: z.number().finite(),
  domainLookupStart: z.number().finite(),
  domainLookupEnd: z.number().finite(),
  connectStart: z.number().finite(),
  secureConnectionStart: z.number().finite(),
  connectEnd: z.number().finite(),
  requestStart: z.number().finite(),
  responseStart: z.number().finite(),
  responseEnd: z.number().finite(),
})
const ResponseSecuritySchema = z.strictObject({
  issuer: z.string(),
  protocol: z.string(),
  subjectName: z.string(),
  validFrom: z.number().finite(),
  validTo: z.number().finite(),
})
const ResponseSchema = z.strictObject({
  responseId: z.string().min(1),
  apiContextId: z.number().int().positive(),
  url: z.string(),
  status: z.number().int(),
  statusText: z.string(),
  method: z.string().optional(),
  timing: ResponseTimingSchema.optional(),
  serverAddr: z
    .strictObject({ ipAddress: z.string(), port: z.number().int() })
    .nullable()
    .optional(),
  securityDetails: ResponseSecuritySchema.nullable().optional(),
  headers: z.array(z.strictObject({ name: z.string(), value: z.string() })),
})
const RouteFetchResponseSchema = ResponseSchema.extend({
  browserContextId: z.number().int().positive(),
})
interface NativeResponseReceipt {
  readonly context: NativeAPIRequestContext
  readonly info: z.infer<typeof ResponseSchema>
  readonly method: string
}
const nativeResponseReceipts = new WeakMap<NativeAPIResponse, NativeResponseReceipt>()
const nativeResponseOwners = new WeakMap<
  NativeAPIRequestContext,
  Map<string, WeakRef<NativeAPIResponse>>
>()
const nativeResponseFinalizer = new FinalizationRegistry<{
  owners: Map<string, WeakRef<NativeAPIResponse>>
  id: string
  reference: WeakRef<NativeAPIResponse>
}>(({ owners, id, reference }) => {
  if (owners.get(id) === reference) owners.delete(id)
})
function registerNativeResponseOwner(
  owners: Map<string, WeakRef<NativeAPIResponse>>,
  id: string,
  response: NativeAPIResponse,
): void {
  const reference = new WeakRef(response)
  owners.set(id, reference)
  // A full WeakRef scan revives every discarded response, including its cached
  // body, until the next job. Collection can retire identities without touching
  // unrelated payloads or making every new response linear in response history.
  nativeResponseFinalizer.register(response, { owners, id, reference })
}
function retireNativeResponse(context: NativeAPIRequestContext, id: string): void {
  const owners = nativeResponseOwners.get(context),
    response = owners?.get(id)?.deref()
  if (response) nativeResponseReceipts.delete(response)
  owners?.delete(id)
}
function nativeDispatchedResponse(
  context: NativeAPIRequestContext,
  result: UntrustedInput,
  method: string,
): NativeAPIResponse {
  // Only the actual dispatch completion enters here. The public constructor
  // deliberately cannot turn caller-supplied metadata into an issued receipt.
  const info = withoutNativeRetryOperation(() => ResponseSchema.parse(result))
  return nativeIssuedResponse(context, info, method)
}
function nativeIssuedResponse(
  context: NativeAPIRequestContext,
  info: z.infer<typeof ResponseSchema>,
  method: string,
): NativeAPIResponse {
  const response = new NativeAPIResponse(context, info, method)
  if (context.isDisposed) return response
  const owners = nativeResponseOwners.get(context) ?? new Map<string, WeakRef<NativeAPIResponse>>()
  retireNativeResponse(context, info.responseId)
  nativeResponseReceipts.set(response, { context, info, method: info.method ?? method })
  registerNativeResponseOwner(owners, info.responseId, response)
  nativeResponseOwners.set(context, owners)
  return response
}
/** Internal author bridge; this is not exported from the public native entry. */
export function hasNativeAPIResponseReceipt(response: NativeAPIResponse): boolean {
  return nativeResponseReceipts.has(response)
}
/** One fresh body command owns this lease; no public shared body cache is used. */
export async function observeNativeAPIResponseAssertion(
  response: NativeAPIResponse,
  options: { readonly isNot: boolean; readonly timeoutMs: number; readonly signal: AbortSignal },
): Promise<{
  readonly ok: boolean
  readonly url: string
  readonly method: string
  readonly status: number
  readonly statusText: string
  readonly text?: string
}> {
  return runNativeRetryOperation(async () => {
    const receipt = nativeResponseReceipts.get(response)
    if (!receipt)
      throw new Error('Response has been disposed or has no native HTTP dispatch receipt')
    const current = () => {
      options.signal.throwIfAborted()
      if (nativeResponseReceipts.get(response) !== receipt)
        throw new Error('Response has been disposed or its native receipt was retired')
      receipt.context.assertOpen()
    }
    current()
    const info = receipt.info,
      ok = info.status >= 200 && info.status < 300
    const headers = info.headers.filter((header) => header.name.toLowerCase() === 'content-type')
    const contentType = headers
      .map((header) => header.value)
      .join(', ')
      .toLowerCase()
    let text: string | undefined
    if (
      ok === options.isNot &&
      /^(?:text\/|application\/(?:json|xml|javascript|[^;]+\+json|[^;]+\+xml))/u.test(contentType)
    ) {
      const result = await receipt.context.command(
        {
          action: 'body',
          responseId: info.responseId,
          timeoutMs: options.timeoutMs,
        },
        options.signal,
      )
      current()
      text = Buffer.from(
        withoutNativeRetryOperation(() => z.object({ body: z.string() }).parse(result)).body,
        'base64',
      ).toString('utf8')
    }
    current()
    return {
      ok,
      url: info.url,
      method: receipt.method,
      status: info.status,
      statusText: info.statusText,
      ...(text === undefined ? {} : { text }),
    }
  })
}
export class NativeAPIResponse {
  private readonly info: z.infer<typeof ResponseSchema>
  private bytes: Promise<Buffer> | undefined
  private disposed = false
  constructor(
    private readonly context: NativeAPIRequestContext,
    info: UntrustedInput,
    private readonly originalMethod?: string,
  ) {
    this.info = withoutNativeRetryOperation(() => ResponseSchema.parse(info))
    this.context.registerResponse(this.info.responseId, this)
  }
  url(): string {
    return this.info.url
  }
  requestMethod(): string {
    const method = this.info.method ?? this.originalMethod
    if (method === undefined) throw new Error('Native request method is unavailable')
    return method
  }
  status(): number {
    return this.info.status
  }
  statusText(): string {
    return this.info.statusText
  }
  ok(): boolean {
    return this.info.status >= 200 && this.info.status < 300
  }
  timing(): z.infer<typeof ResponseTimingSchema> {
    if (this.info.timing === undefined) throw new Error('Native API timing is unavailable')
    return { ...this.info.timing }
  }
  serverAddr(): Promise<{ ipAddress: string; port: number } | null> {
    if (this.info.serverAddr === undefined)
      throw new Error('Native API server address is unavailable')
    return Promise.resolve(this.info.serverAddr === null ? null : { ...this.info.serverAddr })
  }
  securityDetails(): Promise<z.infer<typeof ResponseSecuritySchema> | null> {
    if (this.info.securityDetails === undefined)
      throw new Error('Native API security details are unavailable')
    return Promise.resolve(
      this.info.securityDetails === null ? null : { ...this.info.securityDetails },
    )
  }
  [inspect.custom](): string {
    return [
      `APIResponse: ${this.status()} ${this.statusText()}`,
      ...this.headersArray().map(({ name, value }) => `  ${name}: ${value}`),
    ].join('\n')
  }

  headers(): Record<string, string> {
    const result: Record<string, string> = {}
    for (const { name, value } of this.info.headers) {
      const key = name.toLowerCase()
      Object.defineProperty(result, key, {
        value: Object.hasOwn(result, key)
          ? `${result[key]}${key === 'set-cookie' ? '\n' : ', '}${value}`
          : value,
        enumerable: true,
        configurable: true,
        writable: true,
      })
    }
    return result
  }
  headersArray(): { name: string; value: string }[] {
    return this.info.headers.map((header) => ({ ...header }))
  }
  body(): Promise<Buffer> {
    return runNativeRetryOperation(() => {
      if (this.disposed) return Promise.reject(new Error('Response has been disposed'))
      try {
        this.context.assertOpen()
      } catch (error) {
        return Promise.reject(
          error instanceof Error ? error : new Error('API request context is closed'),
        )
      }
      this.bytes ??= this.context
        .command({ action: 'body', responseId: this.info.responseId })
        .then((result) => {
          if (this.disposed) throw new Error('Response has been disposed')
          this.context.assertOpen()
          return Buffer.from(
            withoutNativeRetryOperation(() => z.object({ body: z.string() }).parse(result)).body,
            'base64',
          )
        })
      return this.bytes
    })
  }
  async text(): Promise<string> {
    return (await this.body()).toString('utf8')
  }
  async json(): Promise<UntrustedInput> {
    return JSON.parse(await this.text()) as UntrustedInput
  }
  async dispose(): Promise<void> {
    if (this.disposed) return
    this.markContextClosed()
    this.context.responseDisposed(this.info.responseId)
    if (!this.context.isDisposed)
      await this.context.command({
        action: 'disposeResponse',
        responseId: this.info.responseId,
      })
  }
  /** Clears SDK byte ownership when the native request context is released. */
  markContextClosed(): void {
    const receipt = nativeResponseReceipts.get(this)
    if (receipt) retireNativeResponse(receipt.context, receipt.info.responseId)
    this.disposed = true
    this.bytes = undefined
  }
  toString(): string {
    return `NativeAPIResponse: ${this.info.status} ${this.info.statusText}\n  ${this.info.url}`
  }
}
/** Native HTTP transport with the actual CEF cookie jar; no SDK HTTP client is used. */
const pageRequests = new WeakMap<NativeBrowser, NativeAPIRequestContext>()
const routeRequests = new WeakMap<NativeBrowser, Map<number, WeakRef<NativeAPIRequestContext>>>()
let acceptNativeRouteResponse: (source: NativeBrowser, result: UntrustedInput) => NativeAPIResponse
/** Internal native completion bridge, not exported by the public native entry. */
export function nativeRouteFetchResponse(
  source: NativeBrowser,
  result: UntrustedInput,
): NativeAPIResponse {
  return acceptNativeRouteResponse(source, result)
}
export class NativeAPIRequestContext {
  static {
    acceptNativeRouteResponse = (source, result) => this.routeFetchResponse(source, result)
  }
  readonly tracing: NativeTracing
  static forPage(source: NativeBrowser): NativeAPIRequestContext {
    const existing = pageRequests.get(source)
    if (existing) return existing
    const context = new NativeAPIRequestContext(source)
    pageRequests.set(source, context)
    return context
  }
  private creation: Promise<number> | undefined
  private associatedBrowserContextId: number | undefined
  private readonly lifecycle = new AbortController()
  private readonly options: z.infer<typeof ContextOptionsSchema>
  private closureMessage = REQUEST_CONTEXT_CLOSED
  private disposed = false
  private disposing: Promise<void> | undefined
  private readonly responses = new Map<string, WeakRef<NativeAPIResponse>>()
  private unsubscribeContext: (() => void) | undefined
  private unsubscribeProcess: (() => void) | undefined
  get isDisposed(): boolean {
    return this.disposed
  }
  assertOpen(): void {
    if (this.disposed) throw new Error(this.closureMessage)
    this.source.processSignal?.throwIfAborted()
    this.lifecycle.signal.throwIfAborted()
  }
  registerResponse(id: string, response: NativeAPIResponse): void {
    registerNativeResponseOwner(this.responses, id, response)
    if (this.disposed) response.markContextClosed()
  }
  responseDisposed(id: string): void {
    retireNativeResponse(this, id)
    this.responses.delete(id)
  }
  /** Native browser context closure already released the native response cache. */
  markContextClosed(reason?: string): void {
    if (reason) this.closureMessage = `${REQUEST_CONTEXT_CLOSED}: ${reason}`
    if (this.disposed) return
    this.disposed = true
    for (const id of nativeResponseOwners.get(this)?.keys() ?? []) retireNativeResponse(this, id)
    nativeResponseOwners.delete(this)
    this.lifecycle.abort(new Error(this.closureMessage))
    this.unsubscribeContext?.()
    this.unsubscribeProcess?.()
    this.unsubscribeContext = undefined
    this.unsubscribeProcess = undefined
    for (const reference of this.responses.values()) reference.deref()?.markContextClosed()
    this.responses.clear()
  }
  constructor(
    private readonly source: NativeBrowser,
    options: NativeAPIRequestContextOptions = {},
    private readonly scope: 'page' | 'context' | 'independent' = 'page',
    private readonly browserContextId?: number,
    private readonly inheritedTimeout?: () => number,
  ) {
    this.options = withoutNativeRetryOperation(() => ContextOptionsSchema.parse(options))
    this.tracing = new NativeTracing({
      signal: this.lifecycle.signal,
      defaultTimeout: () => this.defaultTimeout(),
      runTrace: (params) =>
        this.command({ action: 'tracing', params, timeoutMs: params.timeoutMs }),
    })
  }
  private defaultTimeout(): number {
    return this.options.timeout ?? this.inheritedTimeout?.() ?? 30_000
  }
  static async create(
    source: NativeBrowser,
    options: NativeAPIRequestContextOptions = {},
  ): Promise<NativeAPIRequestContext> {
    return runNativeRetryOperation(async () => {
      const context = new NativeAPIRequestContext(source, options, 'independent')
      await context.id()
      return context
    })
  }
  private id(): Promise<number> {
    if (this.disposed) return Promise.reject(new Error(this.closureMessage))
    return (this.creation ??= this.initialize())
  }
  private async initialize(): Promise<number> {
    const state =
      this.options.storageState === undefined
        ? undefined
        : await readNativeStorageState(this.options.storageState)
    this.assertOpen()
    const info = z
      .object({
        apiContextId: z.number().int().positive(),
        browserContextId: z.number().int().positive(),
      })
      .parse(
        await this.source.runAutomation(
          'http',
          {
            action: 'create',
            scope: this.scope,
            ...(this.browserContextId === undefined
              ? {}
              : { browserContextId: this.browserContextId }),
            headers: headersArray(this.options.extraHTTPHeaders ?? {}),
            ...(this.options.userAgent === undefined ? {} : { userAgent: this.options.userAgent }),
            ...(Array.isArray(this.options.httpCredentials)
              ? { credentials: this.options.httpCredentials }
              : (this.options.httpCredentials ?? {})),
            ...(this.options.ignoreHTTPSErrors === undefined
              ? {}
              : { ignoreHTTPSErrors: this.options.ignoreHTTPSErrors }),
            ...(this.options.extraCACertificates === undefined
              ? {}
              : { extraCACertificates: this.options.extraCACertificates }),
            ...(this.options.proxy === undefined ? {} : { proxy: this.options.proxy }),
            initializeStorage: state !== undefined,
          },
          this.source.processSignal,
        ),
      )
    try {
      this.assertOpen()
      if (state !== undefined) {
        await restoreNativeStorageState(
          {
            runContext: (storageAction, params) =>
              this.source.runAutomation(
                'http',
                {
                  action: 'initializeStorage',
                  storageAction,
                  params,
                  apiContextId: info.apiContextId,
                },
                AbortSignal.any([
                  this.lifecycle.signal,
                  ...(this.source.processSignal ? [this.source.processSignal] : []),
                ]),
              ),
          },
          info.browserContextId,
          state,
        )
        this.assertOpen()
      }
      this.observeBrowserContext(info.browserContextId)
      return info.apiContextId
    } catch (error) {
      this.markContextClosed()
      if (!this.source.processSignal?.aborted) {
        try {
          await runNativeRetryCleanup(() =>
            this.source.runAutomation('http', {
              action: 'dispose',
              apiContextId: info.apiContextId,
            }),
          )
        } catch (cleanup) {
          throw new AggregateError(
            [error, cleanup],
            'Native API context initialization and physical cleanup failed',
            { cause: cleanup },
          )
        }
      }
      throw error
    }
  }
  async addCookies(cookies: NativeCookieInput[]): Promise<void> {
    const values = withoutNativeRetryOperation(() => z.array(z.unknown()).max(1024).parse(cookies))
    await this.command({ action: 'addCookies', cookies: values })
  }
  async cookies(urls: string | string[] = []): Promise<NativeStorageState['cookies']> {
    const list = withoutNativeRetryOperation(() =>
      z.union([z.string(), z.array(z.string()).max(128)]).parse(urls),
    )
    return z
      .object({ cookies: NativeStorageStateSchema.shape.cookies })
      .parse(
        await this.command({ action: 'cookies', urls: typeof list === 'string' ? [list] : list }),
      ).cookies
  }
  async clearCookies(options: NativeCookieFilter = {}): Promise<void> {
    const filter = nativeCookieFilter(options)
    await this.command({ action: 'clearCookies', filter })
  }
  async storageState(
    options: {
      path?: string
      indexedDB?: boolean
      opfs?: boolean
      credentials?: boolean
    } = {},
  ): Promise<NativeStorageState> {
    return runNativeRetryOperation(async () => {
      await this.id()
      this.assertOpen()
      const contextId = this.associatedBrowserContextId
      if (!contextId) {
        const config = withoutNativeRetryOperation(() =>
          z
            .strictObject({
              path: z.string().optional(),
              indexedDB: z.boolean().optional(),
              opfs: z.boolean().optional(),
              credentials: z.boolean().optional(),
            })
            .parse(options),
        )
        const state = NativeStorageStateSchema.parse(await this.command({ action: 'storageState' }))
        this.assertOpen()
        if (config.path !== undefined) {
          await mkdir(dirname(config.path), { recursive: true })
          await writeFile(config.path, JSON.stringify(state, null, 2), {
            mode: 0o600,
          })
        }
        return state
      }
      const state = await captureNativeStorageState(
        {
          runContext: (storageAction, params) =>
            this.command({ action: 'contextStorage', storageAction, params }),
        },
        contextId,
        options,
      )
      this.assertOpen()
      return state
    })
  }
  /** Internal actual route-fetch completion; no context/setup IPC is issued. */
  private static routeFetchResponse(
    source: NativeBrowser,
    result: UntrustedInput,
  ): NativeAPIResponse {
    const parsed = withoutNativeRetryOperation(() => RouteFetchResponseSchema.parse(result))
    const { browserContextId, ...info } = parsed
    const contexts =
      routeRequests.get(source) ?? new Map<number, WeakRef<NativeAPIRequestContext>>()
    for (const [id, reference] of contexts) if (!reference.deref()) contexts.delete(id)
    let context = contexts.get(info.apiContextId)?.deref()
    if (!context) {
      context = new NativeAPIRequestContext(source)
      context.creation = Promise.resolve(info.apiContextId)
      context.observeBrowserContext(browserContextId)
      contexts.set(info.apiContextId, new WeakRef(context))
      routeRequests.set(source, contexts)
    }
    context.assertOpen()
    if (context.associatedBrowserContextId !== browserContextId)
      throw new Error('Native route fetch response changed its actual browser context')
    return nativeIssuedResponse(context, info, info.method ?? 'GET')
  }
  private observeBrowserContext(browserContextId: number): void {
    this.associatedBrowserContextId = browserContextId
    if (this.source.onContextEvent)
      this.unsubscribeContext = this.source.onContextEvent((event) => {
        if (event.event === 'context-closed' && event.contextId === browserContextId)
          this.markContextClosed(event.closedReason)
      })
    const processSignal = this.source.processSignal
    if (processSignal) {
      const abort = () => this.markContextClosed()
      processSignal.addEventListener('abort', abort, { once: true })
      this.unsubscribeProcess = () => processSignal.removeEventListener('abort', abort)
      if (processSignal.aborted) abort()
    }
  }
  async command(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput> {
    return runNativeRetryOperation(async () => {
      const id = await this.id()
      this.assertOpen()
      const scopedSignal = AbortSignal.any([
        this.lifecycle.signal,
        ...(this.source.processSignal ? [this.source.processSignal] : []),
        ...(signal ? [signal] : []),
      ])
      return this.source.runAutomation('http', { ...params, apiContextId: id }, scopedSignal)
    })
  }
  get(url: string, options: NativeAPIRequestOptions = {}): Promise<NativeAPIResponse> {
    return this.dispatch(url, { ...options, method: 'GET' }, 'get')
  }
  head(url: string, options: NativeAPIRequestOptions = {}): Promise<NativeAPIResponse> {
    return this.dispatch(url, { ...options, method: 'HEAD' }, 'head')
  }
  post(url: string, options: NativeAPIRequestOptions = {}): Promise<NativeAPIResponse> {
    return this.dispatch(url, { ...options, method: 'POST' }, 'post')
  }
  put(url: string, options: NativeAPIRequestOptions = {}): Promise<NativeAPIResponse> {
    return this.dispatch(url, { ...options, method: 'PUT' }, 'put')
  }
  patch(url: string, options: NativeAPIRequestOptions = {}): Promise<NativeAPIResponse> {
    return this.dispatch(url, { ...options, method: 'PATCH' }, 'patch')
  }
  delete(url: string, options: NativeAPIRequestOptions = {}): Promise<NativeAPIResponse> {
    return this.dispatch(url, { ...options, method: 'DELETE' }, 'delete')
  }
  fetch(
    url: string | NativeRequest,
    options: NativeAPIRequestOptions = {},
  ): Promise<NativeAPIResponse> {
    return runNativeRetryOperation(() => {
      return this.dispatch(url, options, 'fetch')
    })
  }
  private async dispatch(
    input: string | NativeRequest,
    options: NativeAPIRequestOptions,
    apiName: string,
  ): Promise<NativeAPIResponse> {
    return runNativeRetryOperation(async () => {
      const url = withoutNativeRetryOperation(() =>
        typeof input === 'string' ? input : input.url(),
      )
      if (isNativeHTTPRequest(input)) {
        const inheritedBody = withoutNativeRetryOperation(() => input.postDataBuffer())
        const inheritedHeaders = await runNativeMatcherUserPromise(() => input.allHeaders())
        options = withoutNativeRetryOperation(() => ({
          method: withoutNativeRetryOperation(() => input.method()),
          ...(inheritedBody === null ? {} : { data: inheritedBody }),
          ...options,
          headers: mergeHeaders(inheritedHeaders, options.headers ?? {}),
        }))
      }

      const config = withoutNativeRetryOperation(() => OptionsSchema.parse(options))
      const lookup = config.lookup
      const traceStack = nativeTraceStack()
      if (typeof url !== 'string') throw new Error('API request URL must be a string')
      let target: URL
      try {
        target = new URL(url)
      } catch {
        target = new URL(url, this.options.baseURL)
      }
      if (!['http:', 'https:'].includes(target.protocol))
        throw new Error(`apiRequestContext.${apiName}: Protocol "${target.protocol}" not supported`)
      if (config.params !== undefined) {
        const inputParams = config.params
        withoutNativeRetryOperation(() => {
          const params =
            typeof inputParams === 'string'
              ? new URLSearchParams(inputParams)
              : inputParams instanceof URLSearchParams
                ? inputParams
                : new URLSearchParams(
                    Object.entries(inputParams)
                      .filter(([, value]) => value !== undefined)
                      .map(([name, value]) => [name, String(value)]),
                  )
          for (const [name, value] of params) target.searchParams.append(name, value)
        })
      }
      const headers = mergeHeaders(this.options.extraHTTPHeaders ?? {}, config.headers ?? {})
      if (
        [
          Object.hasOwn(config, 'data'),
          config.form !== undefined,
          config.multipart !== undefined,
        ].filter(Boolean).length > 1
      )
        throw new Error('Provide one of data, form or multipart')
      let body: Buffer | undefined
      let parts: Record<string, UntrustedInput>[] | undefined
      let streamInput: z.infer<typeof OptionsSchema>['multipart']
      if (Object.hasOwn(config, 'data')) {
        if (Buffer.isBuffer(config.data)) {
          body = config.data
          defaultHeader(headers, 'content-type', 'application/octet-stream')
        } else if (typeof config.data === 'string') {
          let content = config.data
          if (
            headerValue(headers, 'content-type')
              ?.split(';')[0]
              ?.trim()
              .toLowerCase()
              .match(/(?:\/|\+)json$/u)
          ) {
            try {
              JSON.parse(content)
            } catch {
              content = JSON.stringify(content)
            }
          }
          body = Buffer.from(content)
          defaultHeader(headers, 'content-type', 'application/octet-stream')
        } else {
          const json = withoutNativeRetryOperation(() => JSON.stringify(config.data))
          if (json === undefined)
            throw new Error(
              `apiRequestContext.${apiName}: Unexpected 'data' type: ${typeof config.data}`,
            )
          body = Buffer.from(json)
          defaultHeader(headers, 'content-type', 'application/json')
        }
      } else if (config.form !== undefined) {
        const inputForm = config.form
        body = withoutNativeRetryOperation(() => {
          let form: URLSearchParams
          if (inputForm instanceof URLSearchParams) form = inputForm
          else if (inputForm instanceof FormData) {
            form = new URLSearchParams()
            inputForm.forEach((value, name) => {
              if (typeof value !== 'string')
                throw new Error('URL-encoded form values must be text; use multipart for files')
              form.append(name, value)
            })
          } else
            form = new URLSearchParams(
              Object.entries(inputForm).map(([name, value]) => [name, String(value)]),
            )
          return Buffer.from(form.toString())
        })
        defaultHeader(headers, 'content-type', 'application/x-www-form-urlencoded')
      } else if (config.multipart !== undefined) {
        const multipart = config.multipart
        if (withoutNativeRetryOperation(() => hasNativeMultipartStream(multipart)))
          streamInput = multipart
        else {
          const input = await withoutNativeRetryOperation(() => multipartInput(multipart))
          body = input.body
          parts = input.parts
        }
        for (const name of Object.keys(headers))
          if (name.toLowerCase() === 'content-type') delete headers[name]
      }
      if (body && body.length > MAX_UPLOAD)
        throw new Error(`apiRequestContext.${apiName}: Native API upload exceeds 8 MiB`)
      const signal = AbortSignal.any([
        this.lifecycle.signal,
        ...(this.source.processSignal ? [this.source.processSignal] : []),
        ...(config.signal ? [config.signal] : []),
      ])
      const params: Record<string, UntrustedInput> = {
        action: parts === undefined ? 'fetch' : 'fetchMultipart',
        ...(parts === undefined ? {} : { multipartParts: parts }),
        apiName,
        traceStack,
        url: target.href,
        method: (config.method ?? 'GET').toUpperCase(),
        headers: headersArray(headers),
        maxRedirects: config.maxRedirects ?? this.options.maxRedirects ?? 20,
        maxRetries: config.maxRetries ?? this.options.maxRetries ?? 0,
        ...(config.ignoreHTTPSErrors === undefined
          ? {}
          : { ignoreHTTPSErrors: config.ignoreHTTPSErrors }),
        failOnStatusCode: config.failOnStatusCode ?? this.options.failOnStatusCode ?? false,
        timeoutMs: config.timeout ?? this.defaultTimeout(),
      }
      if (streamInput) {
        await this.id()
        this.assertOpen()
        const job = new NativeMultipartStreamJob(streamInput),
          uploadId = randomUUID(),
          cancel = new AbortController()
        const scoped = AbortSignal.any([signal, cancel.signal, job.abort.signal])
        const run = async (lookupKey?: string): Promise<UntrustedInput> => {
          const pending = this.command(
            {
              ...params,
              action: 'prepareMultipart',
              uploadId,
              ...(lookupKey === undefined ? {} : { lookupKey }),
            },
            scoped,
          )
          const outcome = pending.catch((error) => {
            job.stop(error instanceof Error ? error : new Error('Native multipart request failed'))
            throw error
          })
          let preparationError: Error | undefined
          const appendChunk = bindNativeRetryContinuation(
            async (chunk: Buffer, streamSignal: AbortSignal) => {
              await this.command(
                {
                  action: 'appendPreparedMultipart',
                  uploadId,
                  body: withoutNativeRetryOperation(() => chunk.toString('base64')),
                },
                AbortSignal.any([scoped, streamSignal]),
              )
            },
          )
          const preparation = withoutNativeRetryOperation(() => job.pump(appendChunk))
            .then(async (multipartParts) => {
              await this.command({ action: 'commitMultipart', uploadId, multipartParts }, scoped)
            })
            .catch((error) => {
              preparationError =
                error instanceof Error ? error : new Error('Native multipart preparation failed')
              cancel.abort(preparationError)
              job.stop(preparationError)
            })
          retainNativeRetryOperation(preparation)
          try {
            const result = await outcome
            await preparation
            if (preparationError) throw preparationError
            return result
          } catch (error) {
            job.stop(error instanceof Error ? error : new Error('Native multipart request failed'))
            throw error
          }
        }
        try {
          const result = lookup
            ? await NativeHTTPLookups.forTransport({
                signal: this.source.signal,
                processSignal: this.source.processSignal,
                onHTTPLookupMessage: this.source.onHTTPLookupMessage?.bind(this.source),
                resolveLookup: (event, addresses, error) =>
                  this.source.runAutomation(
                    'http',
                    {
                      action: 'resolveLookup',
                      apiContextId: event.apiContextId,
                      lookupOwner: event.owner,
                      lookupGeneration: event.generation,
                      addresses,
                      error,
                    },
                    this.source.processSignal,
                  ),
              }).withLookup(lookup, run)
            : await run()
          return nativeDispatchedResponse(this, result, (config.method ?? 'GET').toUpperCase())
        } catch (error) {
          job.stop(error instanceof Error ? error : new Error('Native multipart request failed'))
          throw error
        }
      }
      if (body && body.length > 24 * 1024) {
        const id = randomUUID()
        for (let offset = 0; offset < body.length; offset += 24 * 1024)
          await this.command(
            {
              action: 'upload',
              uploadId: id,
              body: body.subarray(offset, offset + 24 * 1024).toString('base64'),
            },
            signal,
          )
        params.uploadId = id
      } else if (body) params.body = body.toString('base64')
      return nativeDispatchedResponse(
        this,
        lookup
          ? await NativeHTTPLookups.forTransport({
              signal: this.source.signal,
              processSignal: this.source.processSignal,
              onHTTPLookupMessage: this.source.onHTTPLookupMessage?.bind(this.source),
              resolveLookup: (event, addresses, error) =>
                this.source.runAutomation(
                  'http',
                  {
                    action: 'resolveLookup',
                    apiContextId: event.apiContextId,
                    lookupOwner: event.owner,
                    lookupGeneration: event.generation,
                    addresses,
                    error,
                  },
                  this.source.processSignal,
                ),
            }).withLookup(lookup, (lookupKey) => this.command({ ...params, lookupKey }, signal))
          : await this.command(params, signal),
        (config.method ?? 'GET').toUpperCase(),
      )
    })
  }
  dispose(): Promise<void> {
    if (this.disposing) return this.disposing
    if (this.disposed) return Promise.resolve()
    this.markContextClosed()
    return (this.disposing = (async () => {
      if (this.creation) {
        const id = await this.creation.catch(() => undefined)
        if (id !== undefined && !this.source.processSignal?.aborted)
          await this.source.runAutomation('http', {
            action: 'dispose',
            apiContextId: id,
          })
      }
    })())
  }
}
