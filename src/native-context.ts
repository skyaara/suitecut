import * as z from 'zod'

import {
  nativeBindingBootstrapSource,
  type NativeBindingCallback,
  type NativeBindingSource,
  type NativeExposedBinding,
} from './native-bindings.js'
import { type NativeBrowser } from './native-browser.js'
import {
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import {
  NativeContextConsoleInfoSchema,
  type NativeContextConsoleInfo,
} from './native-context-console-protocol.js'
import { type NativeContextNetworkInfo } from './native-context-network-protocol.js'
import { NativeContextNetwork } from './native-context-network.js'
import {
  dispatchNativeContextPageListeners,
  emitNativeContextPage,
  nativeContextPageBinding,
  nativeContextPageBindings,
  registerNativeContextPageFactory,
  type NativeContextPageBinding,
} from './native-context-page.js'
import {
  NativeContextRouteInfoSchema,
  type NativeContextRouteInfo,
  type NativeContextRouteMutation,
} from './native-context-route-protocol.js'
import { NativeContextRoutes } from './native-context-routes.js'
import {
  NativeContextWorkerEvents,
  type NativeContextWorkerTransport,
} from './native-context-workers.js'
import { nativeCookieFilter, type NativeCookieFilter } from './native-cookie-filter.js'
import { NativeCredentials } from './native-credentials.js'
import {
  finishNativeDownloadContextClose,
  prepareNativeDownloadContextClose,
} from './native-download-lifecycle.js'
import { NativeGeolocationSchema } from './native-geolocation.js'
import {
  NativeRecordHARSchema,
  routeNativeFromHAR,
  type NativeRouteFromHAROptions,
} from './native-har.js'
import { NativeAPIRequestContext } from './native-http.js'
import { nativeInitScriptSource, type NativeInitScript } from './native-init-script.js'
import { prepareNativeNavigationClose } from './native-navigation-lifecycle.js'
import {
  type NativeRequest,
  type NativeResponse,
  NativeNetwork,
  type NativeNetworkEventName,
  type NativeNetworkURLMatcher,
  type NativeRouteHandler,
} from './native-network.js'
import { NativeConsoleMessage, NativeJSHandle, NativePage } from './native-page.js'
import { type NativeContextEvent } from './native-protocol.js'
import { type NativeRouteMatcherRegistration } from './native-route-url-matcher.js'
import {
  captureNativeStorageState,
  restoreNativeStorageState,
  NativeStorageStateInputSchema,
  type NativeStorageState,
} from './native-storage-state.js'
import { getNativeDefaultTimeout, setNativeDefaultTimeout } from './native-timeouts.js'
import { nativeTraceStack } from './native-trace-metadata.js'
import { NativeTracing } from './native-tracing.js'
import { NativeVideoDescriptorSchema, type NativeVideoDescriptor } from './native-video-protocol.js'
import { NativeVideo, NativeRecordVideoSchema, nativeVideoAbsolutePath } from './native-video.js'
import { type NativeWebSocketRouteInfo } from './native-websocket-route-protocol.js'
import {
  NativeWebSocketRoutes,
  type NativeWebSocketRouteHandler,
  type NativeWebSocketRouteURLMatcher,
  type NativeWebSocketRouteRemovalOptions,
} from './native-websocket-routes.js'
import { type NativeWorkerInfo } from './native-worker-protocol.js'
import { type NativeWorker } from './native-worker.js'
import { type UntrustedInput } from './untrusted.js'

export type { NativeContextEvent } from './native-protocol.js'

/** One native process transport; pageSource routes to individual CEF browsers. */
export interface NativeContextTransport extends NativeContextWorkerTransport {
  readonly signal: AbortSignal
  readonly processSignal?: AbortSignal
  runContext(
    action: string,
    params?: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
  pageSource(pageId: number): NativeBrowser
  onContextEvent(listener: (event: NativeContextEvent) => void): () => void
  takeContextNetworkBody?(context: number, owner: string, bodyId: string): Buffer
  contextNetworkSequence?(): number
  onContextRoute?(listener: (info: NativeContextRouteInfo) => void): () => void
  onWebSocketRouteMessage?(listener: (info: NativeWebSocketRouteInfo) => void): () => void
  onContextRouteMutation?(listener: (info: NativeContextRouteMutation) => void): () => void
  onContextNetwork?(listener: (info: NativeContextNetworkInfo) => void): () => void
  contextConsoleSequence?(): number
  onContextConsole?(listener: (info: NativeContextConsoleInfo) => void): () => void
  contextSequence?(): number
}

const ContextId = z.object({ contextId: z.number().int().positive() })
const PageId = ContextId.extend({
  pageId: z.number().int().positive(),
  video: NativeVideoDescriptorSchema.optional(),
})
const CookieSchema = z.strictObject({
  name: z.string().max(4096),
  value: z.string().max(4096),
  url: z.url().optional(),
  domain: z.string().max(1024).optional(),
  path: z.string().max(8192).optional(),
  expires: z.number().finite().min(-1).max(253402300799).optional(),
  httpOnly: z.boolean().optional(),
  secure: z.boolean().optional(),
  sameSite: z.enum(['Strict', 'Lax', 'None']).optional(),
  partitionKey: z.string().max(8192).optional(),
  partitionKeyHasCrossSiteAncestor: z.boolean().optional(),
})
export type NativeCookieInput = z.input<typeof CookieSchema>
export interface NativeCookie {
  name: string
  value: string
  domain: string
  path: string
  expires: number
  httpOnly: boolean
  secure: boolean
  sameSite: 'Strict' | 'Lax' | 'None'
  partitionKey?: string
  partitionKeyHasCrossSiteAncestor?: boolean
}
const ReturnedCookie = CookieSchema.omit({
  url: true,
  partitionKey: true,
  partitionKeyHasCrossSiteAncestor: true,
})
  .required()
  .extend({
    partitionKey: z.string().max(8192).exactOptional(),
    partitionKeyHasCrossSiteAncestor: z.boolean().exactOptional(),
  })
const ContextCredentialsSchema = z.strictObject({
  username: z.string().max(4096),
  password: z.string().max(4096),
  origin: z.url().optional(),
  send: z.enum(['always', 'unauthorized']).optional(),
})
const ContextOptions = z.strictObject({
  recordVideo: NativeRecordVideoSchema.optional(),
  recordHAR: NativeRecordHARSchema.optional(),
  recordHar: NativeRecordHARSchema.optional(),
  baseURL: z.url().optional(),
  storageState: z.union([z.string(), NativeStorageStateInputSchema]).optional(),
  acceptDownloads: z.boolean().optional(),
  offline: z.boolean().optional(),
  extraHTTPHeaders: z.record(z.string(), z.string()).optional(),
  viewport: z
    .strictObject({
      width: z.number().int().min(1).max(3840),
      height: z.number().int().min(1).max(2160),
    })
    .nullable()
    .optional(),
  screen: z
    .strictObject({
      width: z.number().int().min(1).max(3840),
      height: z.number().int().min(1).max(2160),
    })
    .optional(),
  deviceScaleFactor: z.number().finite().min(0.25).max(4).optional(),
  hasTouch: z.boolean().optional(),
  isMobile: z.boolean().optional(),
  locale: z.string().min(1).max(128).optional(),
  timezoneId: z.string().min(1).max(256).optional(),
  userAgent: z.string().min(1).max(4096).optional(),
  colorScheme: z.enum(['light', 'dark', 'no-preference']).nullable().optional(),
  reducedMotion: z.enum(['reduce', 'no-preference']).nullable().optional(),
  forcedColors: z.enum(['active', 'none']).nullable().optional(),
  contrast: z.enum(['more', 'no-preference']).nullable().optional(),
  geolocation: NativeGeolocationSchema.optional(),
  permissions: z.array(z.string()).max(32).optional(),
  httpCredentials: z
    .union([ContextCredentialsSchema, z.array(ContextCredentialsSchema).max(64)])
    .optional(),
  ignoreHTTPSErrors: z.boolean().optional(),
  proxy: z
    .strictObject({
      server: z.string().min(1).max(4096),
      bypass: z.string().max(4096).optional(),
      username: z.string().max(4096).optional(),
      password: z.string().max(4096).optional(),
    })
    .optional(),
})
function nativeContextWireOptions(
  config: z.infer<typeof ContextOptions>,
): Record<string, UntrustedInput> {
  const { storageState: ignored, recordHar, ...native } = config
  void ignored
  if (recordHar && native.recordHAR) throw new Error('Use one HAR recording option')
  return {
    ...native,
    ...(recordHar === undefined ? {} : { recordHAR: recordHar }),
    ...(native.recordVideo === undefined
      ? {}
      : {
          recordVideo: {
            ...native.recordVideo,
            dir: nativeVideoAbsolutePath(native.recordVideo.dir),
          },
        }),
  }
}
export type NativeBrowserContextOptions = Omit<z.input<typeof ContextOptions>, 'geolocation'> & {
  geolocation?: z.output<typeof NativeGeolocationSchema> | undefined
}
type ContextListener = (page: NativePage) => void
type ContextConsoleListener = (message: NativeConsoleMessage) => void
type ContextNetworkListener = (value: NativeRequest | NativeResponse) => void
type ContextWorkerEventName = 'serviceworker' | 'sharedworker'
type ContextWorkerListener = (worker: NativeWorker) => void
type ContextEventName =
  'page' | 'close' | 'console' | NativeNetworkEventName | ContextWorkerEventName
function isNetworkEvent(event: ContextEventName): event is NativeNetworkEventName {
  return (
    event === 'request' ||
    event === 'response' ||
    event === 'requestfinished' ||
    event === 'requestfailed'
  )
}
type ContextCloseListener = (context: NativeBrowserContext) => void
const sourcePages = new WeakMap<NativeBrowser, WeakRef<NativePage>>()
const processControllers = new WeakMap<AbortSignal, NativeBrowserContexts>()
const configuredDeviceScales = new WeakMap<NativeBrowserContext, number>()
/** Exact native context scale, before capture dimensions have been rounded to pixels. */
export function nativeContextDeviceScaleFactor(context: NativeBrowserContext): number | undefined {
  return configuredDeviceScales.get(context)
}
/** Owns isolated, native CEF request contexts within a single helper process. */
export class NativeBrowserContexts {
  private readonly contextObjects = new Map<number, NativeBrowserContext>()
  private readonly detach: () => void = () => undefined
  constructor(readonly transport: NativeContextTransport) {
    const processSignal =
      transport.processSignal ?? transport.pageSource(1).processSignal ?? transport.signal
    const existing = processControllers.get(processSignal)
    if (existing) return existing
    if (transport.signal !== processSignal) {
      this.transport = {
        signal: processSignal,
        runContext: transport.runContext.bind(transport),
        pageSource: transport.pageSource.bind(transport),
        onContextEvent: transport.onContextEvent.bind(transport),
        ...(transport.onContextConsole
          ? { onContextConsole: transport.onContextConsole.bind(transport) }
          : {}),
        ...(transport.contextConsoleSequence
          ? { contextConsoleSequence: transport.contextConsoleSequence.bind(transport) }
          : {}),
        ...(transport.takeContextNetworkBody
          ? { takeContextNetworkBody: transport.takeContextNetworkBody.bind(transport) }
          : {}),
        ...(transport.onWebSocketRouteMessage
          ? { onWebSocketRouteMessage: transport.onWebSocketRouteMessage.bind(transport) }
          : {}),
        ...(transport.onContextRoute
          ? { onContextRoute: transport.onContextRoute.bind(transport) }
          : {}),
        ...(transport.onContextRouteMutation
          ? { onContextRouteMutation: transport.onContextRouteMutation.bind(transport) }
          : {}),
        ...(transport.onContextNetwork
          ? { onContextNetwork: transport.onContextNetwork.bind(transport) }
          : {}),
        ...(transport.contextNetworkSequence
          ? { contextNetworkSequence: transport.contextNetworkSequence.bind(transport) }
          : {}),
        ...(transport.contextWorkerInfos
          ? { contextWorkerInfos: transport.contextWorkerInfos.bind(transport) }
          : {}),
        ...(transport.contextWorkerSequence
          ? { contextWorkerSequence: transport.contextWorkerSequence.bind(transport) }
          : {}),
        ...(transport.onContextWorkerMessage
          ? { onContextWorkerMessage: transport.onContextWorkerMessage.bind(transport) }
          : {}),
        ...(transport.onContextWorkerPredicate
          ? { onContextWorkerPredicate: transport.onContextWorkerPredicate.bind(transport) }
          : {}),

        ...(transport.contextSequence
          ? { contextSequence: transport.contextSequence.bind(transport) }
          : {}),
      }
      transport = this.transport
    }
    const defaultContext = this.contextObject(1)
    const defaultSource = transport.pageSource(1)
    if (!defaultSource.signal.aborted && defaultSource.state !== 'closed')
      defaultContext.adoptPage(1, undefined, false, defaultSource.videoDescriptor?.())
    this.detach = transport.onContextEvent((event) => {
      if (event.event === 'context-closed') {
        this.contextObjects.get(event.contextId)?.markClosed(event.closedReason)
        this.contextObjects.delete(event.contextId)
        return
      }
      const context = this.contextObjects.get(event.contextId)
      if (!context || event.pageId === undefined) return
      if (event.event === 'page-created')
        context.adoptPage(event.pageId, event.openerId, true, event.video)
      else context.markPageClosed(event.pageId)
    })
    transport.signal.addEventListener('abort', this.aborted, { once: true })
    if (transport.signal.aborted) this.aborted()
    processControllers.set(processSignal, this)
  }
  private readonly aborted = (): void => {
    this.detach()
    this.transport.signal.removeEventListener('abort', this.aborted)
    for (const context of this.contextObjects.values()) context.markClosed()
    this.contextObjects.clear()
  }
  private contextObject(id: number): NativeBrowserContext {
    let context = this.contextObjects.get(id)
    if (!context) {
      context = new NativeBrowserContext(this.transport, id)
      this.contextObjects.set(id, context)
    }
    return context
  }
  defaultContext(): NativeBrowserContext {
    const context = this.contextObjects.get(1)
    if (!context) throw new Error('Default browser context is closed')
    return context
  }
  contexts(): NativeBrowserContext[] {
    return [...this.contextObjects.values()]
  }
  async newContext(options: NativeBrowserContextOptions = {}): Promise<NativeBrowserContext> {
    return runNativeRetryOperation(async () => {
      let config: z.infer<typeof ContextOptions>
      try {
        config = withoutNativeRetryOperation(() => ContextOptions.parse(options))
      } catch (error) {
        if (
          error instanceof z.ZodError &&
          error.issues.some((issue) => issue.path[0] === 'storageState')
        )
          throw new Error(`browser.newContext: Error setting storage state:\n${error.message}`, {
            cause: error,
          })
        throw error
      }
      const result = ContextId.parse(
        await this.transport.runContext('create', nativeContextWireOptions(config)),
      )
      const context = this.contextObject(result.contextId)
      context.initializeOptions(config)
      try {
        if (config.storageState !== undefined) await context.setStorageState(config.storageState)
        return context
      } catch (error) {
        await context.close().catch(() => undefined)
        throw new Error(
          `browser.newContext: Error setting storage state:\n${error instanceof Error ? error.message : String(error)}`,
          { cause: error },
        )
      }
    })
  }
  /** Creates a real native context whose lifetime belongs to this page. */
  async newPage(options: NativeBrowserContextOptions = {}): Promise<NativePage> {
    return runNativeRetryOperation(async () => {
      const config = withoutNativeRetryOperation(() => ContextOptions.parse(options))
      if (config.storageState !== undefined) {
        const context = await this.newContext(config)
        try {
          const result = PageId.parse(
            await this.transport.runContext('newPage', { contextId: context.id, owned: true }),
          )
          return context.adoptPage(result.pageId, undefined, false, result.video)
        } catch (error) {
          await context.close().catch(() => undefined)
          throw error
        }
      }
      const result = PageId.parse(
        await this.transport.runContext('newOwnedPage', nativeContextWireOptions(config)),
      )
      const context = this.contextObject(result.contextId)
      context.initializeOptions(config)
      return context.adoptPage(result.pageId, undefined, false, result.video)
    })
  }
}

export class NativeBrowserContext {
  private nativeCredentials: NativeCredentials | undefined
  private nativeRequestContext: NativeAPIRequestContext | undefined
  private readonly inheritedBindings = new Map<
    string,
    {
      callback: NativeBindingCallback
      binding: boolean
      handle: boolean
      entryId: number | undefined
    }
  >()
  private readonly pageObjects = new Map<number, NativePage>()
  private readonly closedPages = new Map<number, WeakRef<NativePage>>()
  baseURL: string | undefined
  private requestDefaults: Pick<NativeBrowserContextOptions, 'userAgent'> = {}
  /** Records options accepted by the native context before its first page exists. */
  initializeOptions(config: NativeBrowserContextOptions): void {
    this.baseURL = config.baseURL
    this.requestDefaults = config.userAgent === undefined ? {} : { userAgent: config.userAgent }
    if (config.deviceScaleFactor !== undefined)
      configuredDeviceScales.set(this, config.deviceScaleFactor)
  }
  async setHTTPCredentials(
    credentials:
      z.input<typeof ContextCredentialsSchema> | z.input<typeof ContextCredentialsSchema>[] | null,
  ): Promise<void> {
    this.assertOpen()
    const config = withoutNativeRetryOperation(() =>
      z
        .union([ContextCredentialsSchema, z.array(ContextCredentialsSchema).max(64)])
        .nullable()
        .parse(credentials),
    )
    await this.transport.runContext('setHTTPCredentials', {
      contextId: this.id,
      credentials: config,
    })
  }
  private readonly pageListeners: ContextListener[] = []
  private nativeNetwork: NativeContextNetwork | undefined
  private workerNetwork: NativeNetwork | undefined
  private nativeRoutes: NativeContextRoutes | undefined
  private nativeWebSocketRoutes: NativeWebSocketRoutes | undefined
  private detachRoutes: (() => void) | undefined
  private readonly workerLifetime = new AbortController()
  private contextWorkers: NativeContextWorkerEvents | undefined
  private workers(): NativeContextWorkerEvents {
    return (this.contextWorkers ??= new NativeContextWorkerEvents(
      this.transport,
      this.id,
      AbortSignal.any([this.transport.signal, this.workerLifetime.signal]),
      () => getNativeDefaultTimeout(this),
      (value) => value instanceof NativeJSHandle,
      (owner, info) => new NativeConsoleMessage(owner, info),
    ))
  }
  workerFromNetwork(info: NativeWorkerInfo): NativeWorker {
    return this.workers().fromObservedInfo(info)
  }
  serviceWorkers(): NativeWorker[] {
    return this.workers().list('service_worker')
  }
  sharedWorkers(): NativeWorker[] {
    return this.workers().list('shared_worker')
  }
  waitForServiceWorker(
    options: { timeout?: number; signal?: AbortSignal } = {},
  ): Promise<NativeWorker> {
    return this.workers().wait('service_worker', options)
  }
  waitForSharedWorker(
    options: { timeout?: number; signal?: AbortSignal } = {},
  ): Promise<NativeWorker> {
    return this.workers().wait('shared_worker', options)
  }
  private readonly consoleListeners: ContextConsoleListener[] = []
  private detachConsole: (() => void) | undefined
  private readonly closeListeners: ContextCloseListener[] = []
  private nativeClosedReason: string | undefined
  private closed = false
  private closing: Promise<void> | undefined
  constructor(
    private readonly transport: NativeContextTransport,
    readonly id: number,
  ) {}
  private nativeTracing: NativeTracing | undefined
  get tracing(): NativeTracing {
    this.assertOpen()
    return (this.nativeTracing ??= new NativeTracing({
      signal: this.transport.signal,
      defaultTimeout: () => getNativeDefaultTimeout(this),
      runTrace: (params) => {
        this.assertOpen()
        return this.transport.runContext('trace', { contextId: this.id, ...params })
      },
    }))
  }
  private assertOpen(): void {
    if (this.closed || this.closing || this.transport.signal.aborted)
      throw new Error('Native browser context is closed')
  }
  private routesController(): NativeContextRoutes {
    this.assertOpen()
    if (this.nativeRoutes) return this.nativeRoutes
    if (!this.transport.onContextRoute)
      throw new Error('Native context route transport is unavailable')
    this.nativeRoutes = new NativeContextRoutes(
      (params) =>
        this.transport.runContext(
          'routes',
          {
            ...params,
            contextId: this.id,
            timeoutMs: getNativeDefaultTimeout(this),
          },
          this.workerLifetime.signal,
        ),
      this.transport.onContextRouteMutation
        ? (listener) =>
            this.transport.onContextRouteMutation?.((record) => {
              if (record.contextId === this.id) listener(record)
            }) ?? (() => undefined)
        : undefined,
    )
    this.detachRoutes = this.transport.onContextRoute((input) => {
      if (input.contextId !== this.id) return
      const record = withoutNativeRetryOperation(() => NativeContextRouteInfoSchema.parse(input))
      const invocation =
        record.network.event === 'routematch'
          ? record.network.matcher
          : record.network.event === 'route'
            ? record.network.route
            : undefined
      if (!invocation) throw new Error('Native route envelope has no actual invocation')
      const scope = invocation.callbackScope
      if (!scope) throw new Error('Native route envelope has no actual callback scope')
      const command = (params: Record<string, UntrustedInput>) =>
        this.transport.runContext(
          'route',
          {
            ...params,
            contextId: this.id,
            owner: record.owner,
            callbackScope: scope,
            timeoutMs: getNativeDefaultTimeout(this),
          },
          this.workerLifetime.signal,
        )
      const network =
        'pageId' in record
          ? (this.pageById(record.pageId) ?? this.adoptPage(record.pageId)).networkController()
          : this.workerNetworkController()
      network.receiveRoute(record.network, command)
    })
    return this.nativeRoutes
  }
  route(
    url: NativeNetworkURLMatcher,
    handler: NativeRouteHandler,
    options: { times?: number } = {},
  ): Promise<void> {
    return this.routesController().route(url, handler, options)
  }
  unroute(url: NativeNetworkURLMatcher, handler?: NativeRouteHandler): Promise<void> {
    return this.routesController().unroute(url, handler)
  }
  unrouteAll(options: { behavior?: 'default' | 'wait' | 'ignoreErrors' } = {}): Promise<void> {
    return this.routesController().unrouteAll(options)
  }
  private webSocketRoutes(): NativeWebSocketRoutes {
    this.assertOpen()
    if (!this.transport.onWebSocketRouteMessage)
      throw new Error(
        'Native WebSocket route event transport is unavailable; rebuild the native SDK/helper',
      )
    return (this.nativeWebSocketRoutes ??= new NativeWebSocketRoutes({
      signal: this.workerLifetime.signal,
      processSignal: this.transport.processSignal ?? this.transport.signal,
      baseURL: () => this.baseURL,
      run: (params) =>
        this.transport.runContext(
          'websocket',
          { contextId: this.id, ...params, timeoutMs: getNativeDefaultTimeout(this) },
          this.workerLifetime.signal,
        ),
      cleanup: (params) =>
        this.closed
          ? Promise.resolve(undefined)
          : this.transport.runContext(
              'websocket',
              { contextId: this.id, ...params, timeoutMs: getNativeDefaultTimeout(this) },
              this.transport.processSignal ?? this.transport.signal,
            ),
      callback: async (action, lease) => {
        await this.transport
          .pageSource(1)
          .runAutomation(
            'routeCallback',
            { action, ...lease },
            this.transport.processSignal ?? this.transport.signal,
          )
      },
      subscribe: (listener) =>
        this.transport.onWebSocketRouteMessage?.((info) => {
          if (info.scope === 'context' && info.contextId === this.id) listener(info)
        }) ?? (() => undefined),
    }))
  }
  routeWebSocket(
    url: NativeWebSocketRouteURLMatcher,
    handler: NativeWebSocketRouteHandler,
  ): Promise<void> {
    return this.webSocketRoutes().route(url, handler)
  }
  unrouteWebSocket(
    url: NativeWebSocketRouteURLMatcher,
    handler?: NativeWebSocketRouteHandler,
  ): Promise<void> {
    return this.webSocketRoutes().unroute(url, handler)
  }
  unrouteAllWebSockets(options: NativeWebSocketRouteRemovalOptions = {}): Promise<void> {
    return this.webSocketRoutes().unrouteAll(options)
  }
  webSocketRouteErrors(): Error[] {
    return this.nativeWebSocketRoutes?.errors() ?? []
  }
  contextRouteMatcher(id: string): NativeRouteMatcherRegistration | undefined {
    return this.nativeRoutes?.matcher(id)
  }
  contextRouteHandler(id: string, exhausted: boolean): NativeRouteHandler | undefined {
    return this.nativeRoutes?.resolveInvocation(id, exhausted)
  }
  routeFromHAR(path: string, options: NativeRouteFromHAROptions = {}): Promise<void> {
    this.assertOpen()
    return routeNativeFromHAR(
      {
        runAutomation: (command, params) =>
          this.transport.runContext(
            command,
            { contextId: this.id, ...params },
            this.workerLifetime.signal,
          ),
      },
      path,
      options,
      getNativeDefaultTimeout(this),
    )
  }
  setDefaultTimeout(timeout: number): void {
    setNativeDefaultTimeout(this, timeout)
  }
  setDefaultNavigationTimeout(timeout: number): void {
    setNativeDefaultTimeout(this, timeout, true)
  }
  get credentials(): NativeCredentials {
    this.assertOpen()
    return (this.nativeCredentials ??= new NativeCredentials(this.transport, this.id, () =>
      this.assertOpen(),
    ))
  }
  get request(): NativeAPIRequestContext {
    const request = (this.nativeRequestContext ??= new NativeAPIRequestContext(
      this.transport.pageSource(1),
      { ...this.requestDefaults, ...(this.baseURL === undefined ? {} : { baseURL: this.baseURL }) },
      'context',
      this.id,
      () => getNativeDefaultTimeout(this),
    ))
    if (this.closed || this.closing) request.markContextClosed(this.nativeClosedReason)
    return request
  }
  adoptPage(
    id: number,
    openerId?: number,
    emit = false,
    video?: NativeVideoDescriptor,
  ): NativePage {
    if (this.closed || this.closing) throw new Error('Native browser context is closed')
    if (this.closedPages.has(id)) throw new Error('Native Page is already closed')
    let page = this.pageObjects.get(id)
    if (!page) {
      page = new NativePage(this.transport.pageSource(id))
      const entry: NativeContextPageBinding = {
        id,
        context: this,
        closed: false,
        listeners: new Map(),
      }
      nativeContextPageBindings.set(page, entry)
      this.pageObjects.set(id, page)
      sourcePages.set(page.source, new WeakRef(page))
      for (const [name, entry] of this.inheritedBindings)
        page.adoptNativeContextBinding(name, entry.callback, entry.binding, entry.handle)
    }
    const entry = nativeContextPageBinding(page)
    if (video !== undefined) {
      if (entry.videoId !== undefined && entry.videoId !== video.id)
        throw new Error('Native Page announcement has a different actual video')
      if (entry.video === undefined) {
        entry.videoId = video.id
        entry.video = new NativeVideo(this.transport, this.id, video)
      }
    }
    if (openerId !== undefined) {
      if (entry.openerId !== undefined && entry.openerId !== openerId)
        throw new Error('Native Page announcement has a different actual opener')
      entry.openerId = openerId
    }
    if (emit && !entry.announced) {
      // Actual owner metadata can precede its genuine Page-ready announcement.
      // Set the receipt before callbacks so a reentrant duplicate cannot emit twice.
      entry.announced = true
      dispatchNativeContextPageListeners(this.pageListeners, page)
      const opener = entry.openerId === undefined ? undefined : this.pageObjects.get(entry.openerId)
      if (opener) emitNativeContextPage(opener, 'popup', page)
    }
    return page
  }
  pageById(id: number): NativePage | null {
    return this.pageObjects.get(id) ?? this.closedPages.get(id)?.deref() ?? null
  }
  pages(): NativePage[] {
    return [...this.pageObjects.values()]
  }
  async newPage(): Promise<NativePage> {
    this.assertOpen()
    const result = PageId.parse(await this.transport.runContext('newPage', { contextId: this.id }))
    return this.adoptPage(result.pageId, undefined, false, result.video)
  }
  async waitForPageEvent(
    pageId: number,
    event: 'popup' | 'close',
    options: { timeout?: number | undefined } = {},
  ): Promise<NativePage> {
    const config = withoutNativeRetryOperation(() =>
      z.strictObject({ timeout: z.number().int().min(0).max(120000).optional() }).parse(options),
    )
    const result = z.object({ pageId: z.number().int().positive() }).parse(
      await this.transport.runContext('waitEvent', {
        event,
        pageId,
        contextId: this.id,
        afterSequence: this.transport.contextSequence?.() ?? 0,
        timeoutMs: config.timeout ?? getNativeDefaultTimeout(this),
      }),
    )
    const page = this.pageById(result.pageId)
    if (!page) throw new Error('Native event page is no longer available')
    return page
  }
  async closePage(
    pageId: number,
    options: { reason?: string; runBeforeUnload?: boolean } = {},
  ): Promise<void> {
    const config = withoutNativeRetryOperation(() =>
      z
        .strictObject({
          reason: z.string().max(4096).optional(),
          runBeforeUnload: z.boolean().optional(),
        })
        .parse(options),
    )
    const page = this.pageObjects.get(pageId)
    if (page) prepareNativeNavigationClose(page.source)
    await this.transport.runContext('closePage', {
      pageId,
      ...config,
      traceStack: nativeTraceStack(),
    })
    if (!config.runBeforeUnload) this.markPageClosed(pageId)
  }
  markPageClosed(id: number): void {
    const page = this.pageObjects.get(id)
    if (!page) return
    this.pageObjects.delete(id)
    this.closedPages.set(id, new WeakRef(page))
    if (this.closedPages.size > 1024) {
      const first = this.closedPages.keys().next().value
      if (first !== undefined) this.closedPages.delete(first)
    }
    const entry = nativeContextPageBinding(page)
    entry.closed = true
    emitNativeContextPage(page, 'close', page)
    entry.listeners.clear()
  }
  markClosed(reason?: string): void {
    if (reason !== undefined) this.nativeClosedReason = reason
    this.nativeRequestContext?.markContextClosed(this.nativeClosedReason)
    if (this.closed) return
    this.closed = true
    this.workerLifetime.abort(new Error(reason ?? 'Native browser context is closed'))
    finishNativeDownloadContextClose(this)
    for (const id of this.pageObjects.keys()) this.markPageClosed(id)
    for (const listener of [...this.closeListeners]) {
      try {
        listener(this)
      } catch (error) {
        // Author failures stay visible after all actual close observers and
        // mandatory listener/network/route retirement have finished.
        queueMicrotask(() => {
          throw error
        })
      }
    }
    this.detachConsole?.()
    this.detachConsole = undefined
    this.nativeNetwork?.close()
    this.detachRoutes?.()
    this.detachRoutes = undefined
    this.nativeRoutes?.close()
    this.nativeWebSocketRoutes?.close()
    this.consoleListeners.length = 0
    this.closeListeners.length = 0
    this.pageListeners.length = 0
    this.inheritedBindings.clear()
  }
  close(options: { reason?: string } = {}): Promise<void> {
    const config = withoutNativeRetryOperation(() =>
      z.strictObject({ reason: z.string().max(4096).optional() }).parse(options),
    )
    if (this.closed) return Promise.resolve()
    prepareNativeDownloadContextClose(this)
    for (const page of this.pageObjects.values()) prepareNativeNavigationClose(page.source)
    return (this.closing ??= this.transport
      .runContext('close', { contextId: this.id, ...config })
      .then((result) =>
        this.markClosed(
          withoutNativeRetryOperation(() =>
            z.object({ closedReason: z.string().optional() }).nullish().parse(result),
          )?.closedReason,
        ),
      ))
  }
  async cookies(urls: string | string[] = []): Promise<NativeCookie[]> {
    this.assertOpen()
    const filters = withoutNativeRetryOperation(() =>
      z
        .array(z.url())
        .max(128)
        .parse(typeof urls === 'string' ? [urls] : urls),
    )
    const result = z
      .object({ cookies: z.array(ReturnedCookie).max(4096) })
      .parse(await this.transport.runContext('cookies', { contextId: this.id, urls: filters }))
    return result.cookies
  }
  async addCookies(cookies: NativeCookieInput[]): Promise<void> {
    this.assertOpen()
    const values = withoutNativeRetryOperation(() => z.array(CookieSchema).max(1024).parse(cookies))
    await this.transport.runContext('addCookies', { contextId: this.id, cookies: values })
  }
  async clearCookies(options: NativeCookieFilter = {}): Promise<void> {
    this.assertOpen()
    const filter = nativeCookieFilter(options)
    await this.transport.runContext('clearCookies', { contextId: this.id, filter })
  }
  async setExtraHTTPHeaders(headers: Record<string, string>): Promise<void> {
    this.assertOpen()
    await this.transport.runContext('setExtraHTTPHeaders', {
      contextId: this.id,
      headers: withoutNativeRetryOperation(() => z.record(z.string(), z.string()).parse(headers)),
      timeoutMs: getNativeDefaultTimeout(this),
    })
  }
  async setGeolocation(
    geolocation: { latitude: number; longitude: number; accuracy?: number } | null,
  ): Promise<void> {
    this.assertOpen()
    const value = withoutNativeRetryOperation(() => NativeGeolocationSchema.parse(geolocation))
    await this.transport.runContext('setGeolocation', {
      contextId: this.id,
      geolocation: value,
      timeoutMs: getNativeDefaultTimeout(this),
    })
  }
  async setOffline(offline: boolean): Promise<void> {
    this.assertOpen()
    await this.transport.runContext('setOffline', {
      contextId: this.id,
      offline: withoutNativeRetryOperation(() => z.boolean().parse(offline)),
      timeoutMs: getNativeDefaultTimeout(this),
    })
  }
  async grantPermissions(permissions: string[], options: { origin?: string } = {}): Promise<void> {
    this.assertOpen()
    const values = withoutNativeRetryOperation(() => z.array(z.string()).max(32).parse(permissions))
    const config = withoutNativeRetryOperation(() =>
      z.strictObject({ origin: z.url().optional() }).parse(options),
    )
    await this.transport.runContext('grantPermissions', {
      contextId: this.id,
      permissions: values,
      ...config,
    })
  }
  async storageState(
    options: { path?: string; indexedDB?: boolean; opfs?: boolean; credentials?: boolean } = {},
  ): Promise<NativeStorageState> {
    this.assertOpen()
    return captureNativeStorageState(this.transport, this.id, options)
  }
  async setStorageState(input: NativeStorageState | string): Promise<void> {
    return runNativeRetryOperation(async () => {
      this.assertOpen()
      await restoreNativeStorageState(this.transport, this.id, input)
    })
  }
  exposeFunction(name: string, callback: NativeBindingCallback): Promise<NativeExposedBinding> {
    return this.exposeContextBinding(name, callback, false, false)
  }
  exposeBinding(
    name: string,
    callback: (
      source: NativeBindingSource,
      ...args: never[]
    ) => UntrustedInput | Promise<UntrustedInput>,
    options: { handle?: boolean } = {},
  ): Promise<NativeExposedBinding> {
    return runNativeRetryOperation(() => {
      const config = withoutNativeRetryOperation(() =>
        z.strictObject({ handle: z.boolean().optional() }).parse(options),
      )
      return this.exposeContextBinding(name, callback, true, config.handle ?? false)
    })
  }
  private async exposeContextBinding(
    name: string,
    callback: NativeBindingCallback,
    binding: boolean,
    handle: boolean,
  ): Promise<NativeExposedBinding> {
    this.assertOpen()
    withoutNativeRetryOperation(() => z.string().min(1).max(256).parse(name))
    if (typeof callback !== 'function')
      throw new Error('Native binding callback must be a function')
    if (this.inheritedBindings.has(name))
      throw new Error(`Function "${name}" has been already registered`)
    if (this.inheritedBindings.size >= 64)
      throw new Error('Native context binding capacity exceeded')
    const entry = { callback, binding, handle, entryId: undefined as number | undefined }
    // Host ownership is ready before native registration can emit early calls
    // or announce a popup/new page. Native owns identity and actual installation.
    this.inheritedBindings.set(name, entry)
    try {
      for (const page of this.pageObjects.values())
        page.adoptNativeContextBinding(name, callback, binding, handle)
      const result = z.object({ entryId: z.number().int().positive() }).parse(
        await this.transport.runContext('exposeBinding', {
          contextId: this.id,
          name,
          ...nativeBindingBootstrapSource(),
          handle,
        }),
      )
      this.assertOpen()
      entry.entryId = result.entryId
    } catch (error) {
      if (this.inheritedBindings.get(name) === entry) this.inheritedBindings.delete(name)
      for (const page of this.pageObjects.values()) page.dropNativeContextBinding(name)
      throw error
    }
    let disposed = false
    return {
      dispose: async () => {
        if (disposed) return
        if (!this.closed) {
          await this.transport.runContext('removeBinding', {
            contextId: this.id,
            name,
            entryId: entry.entryId,
          })
          if (this.inheritedBindings.get(name) === entry) {
            this.inheritedBindings.delete(name)
            for (const page of this.pageObjects.values()) page.dropNativeContextBinding(name)
          }
        }
        disposed = true
      },
    }
  }
  async addInitScript<A = undefined>(script: NativeInitScript<A>, arg?: A): Promise<void> {
    return runNativeRetryOperation(async () => {
      this.assertOpen()
      const source = await nativeInitScriptSource(script, arg)
      await this.transport.runContext('addInitScript', { contextId: this.id, source })
    })
  }
  async clearPermissions(): Promise<void> {
    this.assertOpen()
    await this.transport.runContext('clearPermissions', { contextId: this.id })
  }
  projectWorkerNetworkEvent(
    _worker: NativeWorkerInfo,
    event: NativeNetworkEventName | 'requestupdated',
    value: UntrustedInput,
  ): NativeRequest | NativeResponse {
    return this.workerNetworkController().eventValue(event, value)
  }
  private workerNetworkController(): NativeNetwork {
    return (this.workerNetwork ??= new NativeNetwork(undefined, undefined, {
      signal: AbortSignal.any([this.transport.signal, this.workerLifetime.signal]),
      processSignal: this.transport.processSignal ?? this.transport.signal,
      defaultTimeout: () => getNativeDefaultTimeout(this),
      command: async (config, signal) => {
        const result = await this.transport.runContext(
          'workerNetwork',
          { ...config, contextId: this.id },
          signal,
        )
        return config.action === 'response' && result === undefined ? null : result
      },
      body: (_info, request) => {
        if (!request) throw new Error('Native worker response request identity is unavailable')
        return this.transport.runContext('workerNetwork', {
          contextId: this.id,
          action: 'body',
          requestId: request.info.id,
          timeoutMs: getNativeDefaultTimeout(this),
        })
      },
      worker: (info) => this.workerFromNetwork(info),
      contextRoute: (id, exhausted) => this.contextRouteHandler(id, exhausted),
      contextMatcher: (id) => this.contextRouteMatcher(id),
      routeCallback: async (action, owner) => {
        await this.transport
          .pageSource(1)
          .runAutomation(
            'routeCallback',
            { action, ...owner },
            this.transport.processSignal ?? this.transport.signal,
          )
      },
      takeBody: (id, info) =>
        info.postDataOwner
          ? this.transport.takeContextNetworkBody?.(this.id, info.postDataOwner, id)
          : undefined,
    }))
  }
  projectNetworkEvent(
    pageId: number,
    event: NativeNetworkEventName,
    value: UntrustedInput,
  ): NativeRequest | NativeResponse {
    const page = this.pageById(pageId)
    if (!page) throw new Error('Native network event page is no longer available')
    return page.networkController().eventValue(event, value)
  }
  private networkController(): NativeContextNetwork {
    if (this.nativeNetwork) return this.nativeNetwork
    if (!this.transport.onContextNetwork || !this.transport.contextNetworkSequence)
      throw new Error('Native context network transport is unavailable')
    return (this.nativeNetwork = new NativeContextNetwork(this, {
      signal: this.transport.signal,
      onContextNetwork: this.transport.onContextNetwork.bind(this.transport),
      contextNetworkSequence: this.transport.contextNetworkSequence.bind(this.transport),
      runContextNetwork: (params, signal) =>
        this.transport.runContext('waitNetwork', params, signal),
    }))
  }
  private consoleFromRecord(value: UntrustedInput): NativeConsoleMessage {
    const info = withoutNativeRetryOperation(() => NativeContextConsoleInfoSchema.parse(value))
    if (info.contextId !== this.id)
      throw new Error('Native console message belongs to another browser context')
    if (info.pageId === null) return this.workers().consoleFromInfo(info.console)
    const page = this.pageById(info.pageId)
    if (!page) throw new Error('Native console message page is no longer available')
    return new NativeConsoleMessage(page, info.console)
  }
  once(event: ContextWorkerEventName, listener: ContextWorkerListener): this
  once(event: 'page', listener: ContextListener): this
  once(event: 'close', listener: ContextCloseListener): this
  once(event: 'console', listener: ContextConsoleListener): this
  once(event: NativeNetworkEventName, listener: ContextNetworkListener): this
  once(
    event: ContextEventName,
    listener:
      | ContextListener
      | ContextCloseListener
      | ContextConsoleListener
      | ContextNetworkListener
      | ContextWorkerListener,
  ): this {
    withoutNativeRetryOperation(() =>
      z
        .enum([
          'serviceworker',
          'sharedworker',
          'page',
          'close',
          'console',
          'request',
          'response',
          'requestfinished',
          'requestfailed',
        ])
        .parse(event),
    )
    if (event === 'serviceworker' || event === 'sharedworker') {
      const once = (worker: NativeWorker) => {
        this.off(event, once)
        ;(listener as ContextWorkerListener)(worker)
      }
      return this.on(event, once)
    }
    if (isNetworkEvent(event)) {
      const once = (value: NativeRequest | NativeResponse): void => {
        this.networkController().off(event, once)
        ;(listener as ContextNetworkListener)(value)
      }
      this.networkController().on(event, once)
      return this
    }
    if (event === 'console') {
      const once = (message: NativeConsoleMessage): void => {
        this.off('console', once)
        ;(listener as ContextConsoleListener)(message)
      }
      return this.on('console', once)
    }
    if (event === 'page') {
      const once = (page: NativePage): void => {
        this.off('page', once)
        ;(listener as ContextListener)(page)
      }
      return this.on('page', once)
    }
    const once = (context: NativeBrowserContext): void => {
      this.off('close', once)
      ;(listener as ContextCloseListener)(context)
    }
    return this.on('close', once)
  }
  waitForEvent(event: ContextWorkerEventName, options?: { timeout?: number }): Promise<NativeWorker>
  waitForEvent(event: 'page', options?: { timeout?: number }): Promise<NativePage>
  waitForEvent(event: 'close', options?: { timeout?: number }): Promise<NativeBrowserContext>
  waitForEvent(event: 'console', options?: { timeout?: number }): Promise<NativeConsoleMessage>
  waitForEvent(event: 'response', options?: { timeout?: number }): Promise<NativeResponse>
  waitForEvent(
    event: Exclude<NativeNetworkEventName, 'response'>,
    options?: { timeout?: number },
  ): Promise<NativeRequest>
  async waitForEvent(
    event: ContextEventName,
    options: { timeout?: number } = {},
  ): Promise<
    | NativePage
    | NativeBrowserContext
    | NativeConsoleMessage
    | NativeRequest
    | NativeResponse
    | NativeWorker
  > {
    withoutNativeRetryOperation(() =>
      z
        .enum([
          'serviceworker',
          'sharedworker',
          'page',
          'close',
          'console',
          'request',
          'response',
          'requestfinished',
          'requestfailed',
        ])
        .parse(event),
    )
    if (this.closed || this.transport.signal.aborted)
      throw new Error('Native browser context is closed')
    const config = withoutNativeRetryOperation(() =>
      z.strictObject({ timeout: z.number().int().min(0).max(120000).optional() }).parse(options),
    )
    if (event === 'serviceworker' || event === 'sharedworker')
      return this.workers().wait(
        event === 'serviceworker' ? 'service_worker' : 'shared_worker',
        config,
      )
    if (event === 'response') return this.networkController().waitForEvent(event, options)
    if (isNetworkEvent(event)) return this.networkController().waitForEvent(event, options)
    if (event === 'console')
      return this.consoleFromRecord(
        await this.transport.runContext('waitConsole', {
          contextId: this.id,
          afterSequence: this.transport.contextConsoleSequence?.() ?? 0,
          timeoutMs: config.timeout ?? getNativeDefaultTimeout(this),
        }),
      )
    const result = await this.transport.runContext('waitEvent', {
      event: event === 'close' ? 'contextclose' : 'page',
      contextId: this.id,
      afterSequence: this.transport.contextSequence?.() ?? 0,
      timeoutMs: config.timeout ?? getNativeDefaultTimeout(this),
    })
    if (event === 'close') return this
    const info = withoutNativeRetryOperation(() => PageId.parse(result)),
      page = this.pageById(info.pageId)
    if (!page) throw new Error('Native event page is no longer available')
    return page
  }
  on(event: ContextWorkerEventName, listener: ContextWorkerListener): this
  on(event: 'page', listener: ContextListener): this
  on(event: 'close', listener: ContextCloseListener): this
  on(event: 'console', listener: ContextConsoleListener): this
  on(event: NativeNetworkEventName, listener: ContextNetworkListener): this
  on(
    event: ContextEventName,
    listener:
      | ContextListener
      | ContextCloseListener
      | ContextConsoleListener
      | ContextNetworkListener
      | ContextWorkerListener,
  ): this {
    withoutNativeRetryOperation(() =>
      z
        .enum([
          'serviceworker',
          'sharedworker',
          'page',
          'close',
          'console',
          'request',
          'response',
          'requestfinished',
          'requestfailed',
        ])
        .parse(event),
    )
    if (event === 'serviceworker' || event === 'sharedworker') {
      this.workers().on(
        event === 'serviceworker' ? 'service_worker' : 'shared_worker',
        listener as ContextWorkerListener,
      )
      return this
    }
    if (isNetworkEvent(event)) {
      this.assertOpen()
      this.networkController().on(event, listener as ContextNetworkListener)
      return this
    }
    if (event === 'console') {
      this.assertOpen()
      if (!this.transport.onContextConsole)
        throw new Error('Native context console events are unavailable')
      this.consoleListeners.push(listener as ContextConsoleListener)
      this.detachConsole ??= this.transport.onContextConsole((info) => {
        if (info.contextId !== this.id) return
        const message = this.consoleFromRecord(info)
        for (const listener of [...this.consoleListeners]) listener(message)
      })
    } else if (event === 'page') this.pageListeners.push(listener as ContextListener)
    else this.closeListeners.push(listener as ContextCloseListener)
    return this
  }
  off(event: ContextWorkerEventName, listener: ContextWorkerListener): this
  off(event: 'page', listener: ContextListener): this
  off(event: 'close', listener: ContextCloseListener): this
  off(event: 'console', listener: ContextConsoleListener): this
  off(event: NativeNetworkEventName, listener: ContextNetworkListener): this
  off(
    event: ContextEventName,
    listener:
      | ContextListener
      | ContextCloseListener
      | ContextConsoleListener
      | ContextNetworkListener
      | ContextWorkerListener,
  ): this {
    withoutNativeRetryOperation(() =>
      z
        .enum([
          'serviceworker',
          'sharedworker',
          'page',
          'close',
          'console',
          'request',
          'response',
          'requestfinished',
          'requestfailed',
        ])
        .parse(event),
    )
    if (event === 'serviceworker' || event === 'sharedworker') {
      this.workers().off(
        event === 'serviceworker' ? 'service_worker' : 'shared_worker',
        listener as ContextWorkerListener,
      )
      return this
    }
    if (isNetworkEvent(event)) {
      this.nativeNetwork?.off(event, listener as ContextNetworkListener)
      return this
    }
    const listeners =
      event === 'console'
        ? this.consoleListeners
        : event === 'page'
          ? this.pageListeners
          : this.closeListeners
    const index = listeners.lastIndexOf(
      listener as ContextListener & ContextCloseListener & ContextConsoleListener,
    )
    if (index >= 0) listeners.splice(index, 1)
    if (event === 'console' && !this.consoleListeners.length) {
      this.detachConsole?.()
      this.detachConsole = undefined
    }
    return this
  }
}

registerNativeContextPageFactory((source, signal) => {
  const existing = sourcePages.get(source)?.deref()
  if (existing) {
    if (!signal) return existing
    const wrapper = new NativePage(existing.source, signal)
    nativeContextPageBindings.set(wrapper, nativeContextPageBinding(existing))
    return wrapper
  }
  if (source.pageId !== undefined && source.pageId !== 1) return undefined
  if (!source.runContext || !source.pageSource || !source.onContextEvent) return undefined
  const controller = new NativeBrowserContexts({
    signal: source.processSignal ?? source.signal,
    runContext: source.runContext.bind(source),
    pageSource: source.pageSource.bind(source),
    onContextEvent: source.onContextEvent.bind(source),
    ...(source.onWebSocketRouteMessage
      ? { onWebSocketRouteMessage: source.onWebSocketRouteMessage.bind(source) }
      : {}),
    ...(source.onContextRoute ? { onContextRoute: source.onContextRoute.bind(source) } : {}),
    ...(source.onContextRouteMutation
      ? { onContextRouteMutation: source.onContextRouteMutation.bind(source) }
      : {}),
    ...(source.onContextNetwork ? { onContextNetwork: source.onContextNetwork.bind(source) } : {}),
    ...(source.contextNetworkSequence
      ? { contextNetworkSequence: source.contextNetworkSequence.bind(source) }
      : {}),
    ...(source.onContextConsole ? { onContextConsole: source.onContextConsole.bind(source) } : {}),
    ...(source.contextConsoleSequence
      ? { contextConsoleSequence: source.contextConsoleSequence.bind(source) }
      : {}),
    ...(source.contextWorkerInfos
      ? { contextWorkerInfos: source.contextWorkerInfos.bind(source) }
      : {}),
    ...(source.contextWorkerSequence
      ? { contextWorkerSequence: source.contextWorkerSequence.bind(source) }
      : {}),
    ...(source.onContextWorkerMessage
      ? { onContextWorkerMessage: source.onContextWorkerMessage.bind(source) }
      : {}),
    ...(source.onContextWorkerPredicate
      ? { onContextWorkerPredicate: source.onContextWorkerPredicate.bind(source) }
      : {}),
    ...(source.onContextConsole ? { onContextConsole: source.onContextConsole.bind(source) } : {}),
    ...(source.contextSequence ? { contextSequence: source.contextSequence.bind(source) } : {}),
  })
  const canonical = controller.defaultContext().pages()[0]
  if (!canonical) throw new Error('The native default page is closed')
  if (!signal) return canonical
  const page = new NativePage(canonical.source, signal)
  nativeContextPageBindings.set(page, nativeContextPageBinding(canonical))
  return page
})
