import { readFile } from 'node:fs/promises'

import * as z from 'zod'

import { raceWithAbort } from './abort.js'
import { type PresentationLocator, type PresentationPage } from './browser-control.js'
import { subscribeNativeActionHook, type NativeBeforeStableHook } from './native-action-hooks.js'
import {
  captureNativeAriaSnapshot,
  captureNativeAriaSnapshotJSON,
  type NativeAriaJSONNode,
  type NativeAriaSnapshotJSONOptions,
  type NativeAriaSnapshotOptions,
} from './native-aria.js'
import {
  captureNativeScreenshot,
  captureNativeElementScreenshot,
  printNativePDF,
  emulateNativeMedia,
  type NativeScreenshotOptions,
  type NativeLocatorScreenshotOptions,
  type NativePDFOptions,
  type NativeMediaOptions,
} from './native-artifacts.js'
import {
  NativeBindings,
  type NativeBindingCallback,
  type NativeBindingSource,
  type NativeExposedBinding,
} from './native-bindings.js'
import { assertNativeBrowserCallbackAdmission } from './native-browser-operations.js'
import { type NativeBrowser } from './native-browser.js'
import {
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import {
  createNativePageError,
  NativePageErrorInfoSchema,
  nativeConsoleArguments,
  nativeConsoleInspection,
  readNativeConsoleMessages,
  clearNativeConsoleMessages,
  readNativePageErrors,
} from './native-console.js'
import {
  createNativeContextBoundPage,
  getNativePageContext,
  closeNativeContextPage,
  isNativeContextPageClosed,
  nativePageOpener,
  onNativeContextPageEvent,
  offNativeContextPageEvent,
  nativeContextPageBindings,
  waitNativeContextPageEvent,
  resolveNativeContextUrl,
  resolveNativeContextUrlGlob,
} from './native-context-page.js'
import { type NativeBrowserContext } from './native-context.js'
import { NativeCoverage } from './native-coverage.js'
import { NativeCrashEvents } from './native-crash.js'
import { NativeDebuggerSession, type NativeDebuggerOptions } from './native-debugger.js'
import { NativeDialogEvents, type NativeDialog } from './native-dialog.js'
import { NativeDownloadEvents, type NativeDownload } from './native-download.js'
import { performNativeDrag, type NativeDragOptions } from './native-drag.js'
import {
  waitNativeEventPredicate,
  type NativeEventPredicate,
  type NativeEventPredicateOptions,
} from './native-event-predicate.js'
import { NativeFileChooserEvents, type NativeFileChooser } from './native-filechooser.js'
import {
  NativeFormActions,
  parseNativePageFormOptions,
  type NativeFormOptions,
  type NativePageFormOptions,
  type NativeSelectValues,
  type NativeInputFiles,
} from './native-form.js'
import { routeNativeFromHAR, type NativeRouteFromHAROptions } from './native-har.js'
import {
  showNativeHighlight,
  hideNativeHighlight,
  type NativeHighlightDisposable,
  type NativeHighlightOptions,
} from './native-highlight.js'
import { NativeAPIRequestContext } from './native-http.js'
import { emulateNativeIdleState, type NativeIdleState } from './native-idle.js'
import { NativeMouse, NativeKeyboard, NativeTouchscreen } from './native-input.js'
import {
  NativeLocatorHandlers,
  type NativeLocatorHandler,
  type NativeLocatorHandlerOptions,
} from './native-locator-handlers.js'
import {
  decodeNativeLocatorNormalization,
  type NativeLocatorNormalization,
} from './native-locator-normalization.js'
import {
  decodeNativeLocatorSuggestions,
  NativeLocatorPickPointSchema,
  NativeLocatorSuggestionOptionsSchema,
  type NativeLocatorPickPoint,
  type NativeLocatorSuggestionOptions,
  type NativeLocatorSuggestions,
} from './native-locator-suggestion.js'
import { runNativeNavigation, prepareNativeNavigationClose } from './native-navigation-lifecycle.js'
import {
  NativeNetwork,
  type NativeResponse,
  type NativeRequest,
  type NativeRoute,
} from './native-network.js'
import {
  NativeFrameInfoSchema,
  NativeConsoleInfoSchema,
  type NativeConsoleInfo,
  type NativeFrameInfo,
  type NativeAutomationEvent,
} from './native-protocol.js'
import {
  nativeTextPattern as textPattern,
  nativeRoleQuery,
  nativeQueryDescription,
  nativeReferenceQuery,
  nativeTestIdQuery,
  type NativeQueryStep,
  type NativeRoleOptions,
} from './native-query.js'
import { nativeSoftOperationOptions } from './native-soft-assertions.js'
import {
  getNativeDefaultTimeout,
  setNativeDefaultTimeout,
  NativeTimeoutError,
} from './native-timeouts.js'
import { nativeTraceStack } from './native-trace-metadata.js'
import { nativeURLGlobMatcher } from './native-url-glob.js'
import { runNativeURLPredicate, type NativeURLPredicate } from './native-url-predicate.js'
import { createNativeValueCodec } from './native-values.js'
import { type NativeVideo } from './native-video.js'
import { NativeVirtualTime } from './native-virtual-time.js'
import {
  NativeWebSocketRoutes,
  type NativeWebSocketRouteHandler,
  type NativeWebSocketRouteURLMatcher,
  type NativeWebSocketRouteRemovalOptions,
} from './native-websocket-routes.js'
import { NativeWebSocketRouting } from './native-websocket-routing.js'
import { NativeWebSockets, type NativeWebSocket } from './native-websocket.js'
import { type NativeWorkerInfo } from './native-worker-protocol.js'
import { NativeWorkerEvents, NativeWorkerHandle, type NativeWorker } from './native-worker.js'
import { type SuiteCutRect } from './types/geometry.js'
import { type UntrustedInput } from './untrusted.js'

export { NativeResponse } from './native-network.js'

const EvaluationResult = z.object({
  result: z
    .object({ value: z.unknown().optional(), unserializableValue: z.string().optional() })
    .optional(),
  exceptionDetails: z
    .object({
      text: z.string(),
      exception: z
        .object({
          description: z.string().optional(),
          value: z.unknown().optional(),
          unserializableValue: z.string().optional(),
          subtype: z.string().optional(),
        })
        .optional(),
    })
    .optional(),
})

function evaluationError(
  details: NonNullable<z.infer<typeof EvaluationResult>['exceptionDetails']>,
): string {
  const exception = details.exception
  if (exception?.description !== undefined) return exception.description
  if (exception?.unserializableValue !== undefined) return exception.unserializableValue
  if (exception && Object.hasOwn(exception, 'value')) return String(exception.value)
  if (exception?.subtype === 'null') return 'null'
  return details.text
}

function evaluationBody(fn: { toString(): string } | string): string {
  return withoutNativeRetryOperation(() => {
    let body = typeof fn === 'string' ? fn : fn.toString()
    if (typeof fn === 'string') return body
    if (!/^async\s+function\b/u.test(body) && /^async\s+[\p{L}\p{N}_$]+\s*\(/u.test(body))
      body = body.replace(/^async\s+/u, 'async function ')
    else if (
      /^[\p{L}\p{N}_$]+\s*\(/u.test(body) &&
      !body.startsWith('function') &&
      !/^async\s*\(/u.test(body)
    )
      body = `function ${body}`
    return body
  })
}

function evaluationArgument(page: NativePage, value: UntrustedInput) {
  return withoutNativeRetryOperation(() => {
    const handles: NativeJSHandle[] = []
    const encoded = createNativeValueCodec().encode(value, 'argument', (item) => {
      if (item instanceof NativeWorkerHandle)
        throw new Error('Worker handles cannot be passed into a page or frame execution context')
      if (!(item instanceof NativeJSHandle)) return undefined
      item.assertUsable(page.source)
      let index = handles.indexOf(item)
      if (index < 0) {
        index = handles.length
        handles.push(item)
      }
      return index
    })
    return { encoded, handles: handles.map((handle) => handle.id) }
  })
}

function createHandle(page: NativePage, response: UntrustedInput): NativeJSHandle {
  const result = z
    .object({
      handle: z.number().int().positive(),
      preview: z.string(),
      node: z.boolean(),
      guid: z.string().max(128).optional(),
      frame: z.string().max(128).optional(),
    })
    .parse(response)
  const currentFrame = page instanceof NativeFrame ? page.identity() : page.mainFrame().identity()
  if (result.frame && result.frame !== currentFrame) {
    const target = page.frames().find((frame) => frame.identity() === result.frame)
    if (!target) throw new Error('Native handle frame was detached before delivery')
    page = target
  }
  return result.node
    ? new NativeElementHandle(page, result.handle, result.preview, result.guid)
    : new NativeJSHandle(page, result.handle, result.preview, result.guid)
}

export interface NativeWorkerConsoleOwner {
  source: Pick<NativeBrowser, 'signal' | 'runAutomation'>
  workerFromConsole(info: NativeWorkerInfo): NativeWorker
}
const consoleIdentities = new WeakMap<
  Pick<NativeBrowser, 'signal' | 'runAutomation'>,
  Map<number, WeakRef<NativeConsoleMessage>>
>()

/** A console message emitted by the native browser. */
export class NativeConsoleMessage {
  private argumentHandles: (NativeJSHandle | NativeWorkerHandle)[] | undefined
  private workerObject: NativeWorker | null | undefined
  private workerChannelEpoch: number | undefined
  private workerResolutionError: Error | undefined
  private argumentOwnershipError: Error | undefined
  constructor(
    private readonly owner: NativePage | NativeWorkerConsoleOwner,
    private readonly info: NativeConsoleInfo,
  ) {
    this.owner = owner instanceof NativeFrame ? owner.page() : owner
    const sequence = info.sequence
    if (sequence !== undefined) {
      let identities = consoleIdentities.get(owner.source)
      if (!identities) {
        identities = new Map()
        consoleIdentities.set(owner.source, identities)
      }
      const existing = identities.get(sequence)?.deref()
      if (existing) return existing
      identities.set(sequence, new WeakRef(this))
      while (identities.size > 256) {
        const oldest = identities.keys().next().value
        if (oldest === undefined) break
        identities.delete(oldest)
      }
    }
    // Bind argument ownership at the actual console observation, so delayed
    // args() cannot adopt old opaque handles into a later control channel.
    if (
      info.arguments &&
      !info.argumentsError &&
      info.workerInfo &&
      info.workerInfo.id === info.worker
    ) {
      try {
        const worker = this.worker()
        if (worker) {
          try {
            worker.assertNativeControl(info.workerInfo.control)
            worker.assertUsable()
          } catch (error) {
            this.argumentOwnershipError =
              error instanceof Error
                ? error
                : new Error('Native console argument channel is unavailable', { cause: error })
          }
        }
      } catch (error) {
        this.workerResolutionError =
          error instanceof Error
            ? error
            : new Error('Native console worker ownership is unavailable', { cause: error })
      }
    }
  }
  text(): string {
    return this.info.text
  }
  timestamp(): number {
    if (this.info.timestamp === undefined)
      throw new Error('Native console timestamp is unavailable')
    return this.info.timestamp
  }
  worker(): NativeWorker | null {
    if (this.workerResolutionError) throw this.workerResolutionError
    if (this.workerObject !== undefined) return this.workerObject
    if (!this.info.workerInfo) {
      if (this.info.worker) throw new Error('Native console worker target identity is unavailable')
      return (this.workerObject = null)
    }
    if (this.info.workerInfo.id !== this.info.worker)
      throw new Error('Native console worker target identity is inconsistent')
    this.workerObject = this.owner.workerFromConsole(this.info.workerInfo)
    this.workerChannelEpoch = this.workerObject.captureChannelEpoch()
    return this.workerObject
  }
  args(): (NativeJSHandle | NativeWorkerHandle)[] {
    if (this.argumentOwnershipError) throw this.argumentOwnershipError
    if (this.argumentHandles && this.workerObject && this.workerChannelEpoch !== undefined)
      this.workerObject.assertChannelEpoch(this.workerChannelEpoch, this.info.workerInfo?.control)
    this.argumentHandles ??= nativeConsoleArguments(
      this.info,
      (value) => {
        const worker = this.worker()
        if (worker) {
          if (this.workerChannelEpoch !== undefined)
            worker.assertChannelEpoch(this.workerChannelEpoch, this.info.workerInfo?.control)
          return worker.adoptHandle(value)
        }
        if (this.owner instanceof NativePage) return createHandle(this.owner, value)
        throw new Error('Context worker console has no actual worker identity')
      },
      this.owner.source,
    )
    return [...this.argumentHandles]
  }
  [nativeConsoleInspection](): string {
    return this.text()
  }

  type(): string {
    return this.info.type
  }
  location(): NativeConsoleInfo['location'] {
    return { ...this.info.location }
  }
  page(): NativePage | null {
    return this.owner instanceof NativePage ? this.owner : null
  }
}

export type NativeLoadState = 'domcontentloaded' | 'load' | 'networkidle'
export interface NativeNavigationOptions {
  timeout?: number
  waitUntil?: NativeLoadState | 'commit'
  referer?: string
}
const NavigationOptionsSchema = z.strictObject({
  timeout: z.number().int().min(0).max(120_000).optional(),
  waitUntil: z.enum(['commit', 'domcontentloaded', 'load', 'networkidle']).optional(),
  referer: z.string().max(8192).optional(),
})
type FrameEventName = NativeAutomationEvent['event']
type RequestEventName = 'request' | 'requestfinished' | 'requestfailed'
type PageEventName =
  | FrameEventName
  | 'pageerror'
  | 'crash'
  | 'console'
  | 'response'
  | RequestEventName
  | 'popup'
  | 'close'
  | 'dialog'
  | 'dialogclosed'
  | 'filechooser'
  | 'download'
  | 'worker'
  | 'websocket'
type FrameListener = (frame: NativeFrame) => void
type PageErrorListener = (error: Error) => void
type ConsoleListener = (message: NativeConsoleMessage) => void
type RequestListener = (request: NativeRequest) => void
type ResponseListener = (response: NativeResponse) => void
type PageListener = (page: NativePage) => void
type FileChooserListener = (chooser: NativeFileChooser) => void
type DownloadListener = (download: NativeDownload) => void
type DialogListener = (dialog: NativeDialog) => void
type PageListenerCallback =
  | FrameListener
  | PageErrorListener
  | ConsoleListener
  | RequestListener
  | ResponseListener
  | PageListener
  | DialogListener
  | FileChooserListener
  | DownloadListener
  | ((worker: NativeWorker) => void)
  | ((socket: NativeWebSocket) => void)

const nativeArtifactLifetimes = new WeakMap<object, AbortSignal>()

/** Internal actual Page/projection lifetime for prepared artifact adapters. */
export function nativeArtifactLifetime(page: object): AbortSignal | undefined {
  return nativeArtifactLifetimes.get(page)
}

/** A small native browser-control API for embedded CEF. */
export class NativePage implements PresentationPage {
  readonly mouse: NativeMouse
  readonly keyboard: NativeKeyboard
  readonly touchscreen: NativeTouchscreen
  private nativeWebSocketRouting: NativeWebSocketRouting | undefined
  private nativeWebSocketRoutes: NativeWebSocketRoutes | undefined
  private nativeCoverage: NativeCoverage | undefined
  private readonly signal: AbortSignal
  private rootFrame: NativeFrame | undefined
  private readonly frameObjects = new Map<string, NativeFrame>()
  private readonly detachedFrames = new Map<string, WeakRef<NativeFrame>>()
  private readonly frameListeners = new Map<
    NativeAutomationEvent['event'],
    ((frame: NativeFrame) => void)[]
  >()
  private detachFrameEvents: (() => void) | undefined
  private readonly pageErrorListeners: PageErrorListener[] = []
  private detachPageErrorEvents: (() => void) | undefined
  private readonly consoleListeners: ConsoleListener[] = []
  private nativeLocatorHandlers: NativeLocatorHandlers | undefined
  private nativeBindings: NativeBindings | undefined
  private nativeNetwork: NativeNetwork | undefined
  private nativeRequestContext: NativeAPIRequestContext | undefined
  private viewport: { width: number; height: number } | undefined
  private detachConsoleEvents: (() => void) | undefined
  private readonly socketListeners: ((socket: NativeWebSocket) => void)[] = []
  private detachSocketEvents: (() => void) | undefined
  constructor(
    readonly source: NativeBrowser,
    signal?: AbortSignal,
    protected readonly frameId?: string,
  ) {
    const lifetime = source.pageSignal ?? source.signal
    this.signal = signal ? AbortSignal.any([signal, lifetime]) : lifetime
    nativeArtifactLifetimes.set(this, this.signal)
    this.mouse = new NativeMouse((params) => this.runAutomation('input', params))
    this.keyboard = new NativeKeyboard((params) => this.runAutomation('input', params))
    this.touchscreen = new NativeTouchscreen((params) => this.runAutomation('input', params))
  }

  /** Version-probed native root-target WebSocket transport control. */
  get webSocketRouting(): NativeWebSocketRouting {
    if (this.frameId) throw new Error('Native WebSocket routing requires the root page target')
    return (this.nativeWebSocketRouting ??= new NativeWebSocketRouting({
      run: (params, signal) => this.runAutomation('websocket', params, signal),
      cleanup: (params) =>
        this.source.state === 'running'
          ? this.source.runAutomation(
              'websocket',
              params,
              this.source.processSignal ?? this.source.signal,
            )
          : Promise.resolve(undefined),
      timeout: () => this.defaultTimeout(),
    }))
  }

  private webSocketRoutes(): NativeWebSocketRoutes {
    if (this.frameId)
      throw new Error('Native WebSocket route registration belongs to a page, not a frame facade')
    if (!this.source.onWebSocketRouteMessage)
      throw new Error(
        'Native WebSocket route event transport is unavailable; rebuild the native SDK/helper',
      )
    return (this.nativeWebSocketRoutes ??= new NativeWebSocketRoutes({
      signal: this.signal,
      processSignal: this.source.processSignal ?? this.source.signal,
      baseURL: () => nativeContextPageBindings.get(this)?.context.baseURL,
      run: (params) => this.runAutomation('websocket', params),
      cleanup: (params) =>
        this.source.state === 'running'
          ? this.source.runAutomation(
              'websocket',
              params,
              this.source.processSignal ?? this.source.signal,
            )
          : Promise.resolve(undefined),
      callback: async (action, lease) => {
        await this.source.runAutomation(
          'routeCallback',
          { action, ...lease },
          this.source.processSignal ?? this.source.signal,
        )
      },
      subscribe: (listener) =>
        this.source.onWebSocketRouteMessage?.((info) => {
          if (info.scope === 'page' && info.pageId === (this.source.pageId ?? 1)) listener(info)
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

  get coverage(): NativeCoverage {
    if (this.frameId)
      throw new Error('Native coverage belongs to an actual page, not a frame facade')
    const sourceSignal = () => this.source.signal
    return (this.nativeCoverage ??= new NativeCoverage({
      get signal() {
        return sourceSignal()
      },
      defaultTimeout: () => this.defaultTimeout(),
      runCoverage: (params) => this.runAutomation('coverage', params),
    }))
  }

  /**
   * Explicit native control of this actual document; no Inspector window is opened.
   * Frame sessions support observed pauses and resume; agent-wide pause/step remain unsupported.
   */
  createDebuggerSession(options: NativeDebuggerOptions = {}): Promise<NativeDebuggerSession> {
    const lifetime = this.signal,
      frame = this.frameId
    return NativeDebuggerSession.create(
      {
        signal: lifetime,
        cleanupOwner: this.source,
        agentControls: !frame,
        cleanupDebugger: async (session) => {
          try {
            return await this.source.runAutomation(
              'debugger',
              { action: 'close', session, timeoutMs: 0, ...(frame ? { frame } : {}) },
              this.source.pageSignal ?? this.source.signal,
            )
          } catch (error) {
            if (this.source.state === 'closing' || this.source.state === 'closed') {
              await this.source.closed
              return {}
            }
            throw error
          }
        },
        defaultTimeout: () => this.defaultTimeout(),
        runDebugger: (params, signal) => this.runAutomation('debugger', params, signal),
      },
      options,
    )
  }

  private socketEvents(): NativeWebSockets {
    return NativeWebSockets.forSource(this.source, () => this.defaultTimeout())
  }
  private workerEvents(): NativeWorkerEvents {
    return NativeWorkerEvents.forOwner({
      source: this.source,
      runWorker: (params, signal) => this.runAutomation('worker', params, signal),
      runDebugger: (params, signal) => this.runAutomation('debugger', params, signal),
      defaultTimeout: () => this.defaultTimeout(),
      isPageHandle: (value) => value instanceof NativeJSHandle,
      createConsole: (info) => new NativeConsoleMessage(this, info),
    })
  }
  workerFromConsole(info: UntrustedInput): NativeWorker {
    return this.workerEvents().fromConsole(info)
  }
  workers(): NativeWorker[] {
    return this.workerEvents().list()
  }
  private downloadEvents(): NativeDownloadEvents {
    return NativeDownloadEvents.forPage(this)
  }
  private fileChooserEvents(): NativeFileChooserEvents {
    return NativeFileChooserEvents.forPage(this, (owner, metadata) => {
      const element = createHandle(owner, metadata).asElement()
      if (!element) throw new Error('Native file chooser did not resolve to an element handle')
      return element
    })
  }
  setDefaultTimeout(timeout: number): void {
    setNativeDefaultTimeout(this.source, timeout)
  }
  setDefaultNavigationTimeout(timeout: number): void {
    setNativeDefaultTimeout(this.source, timeout, true)
  }
  defaultTimeout(navigation = false): number {
    const page = this instanceof NativeFrame ? this.page() : this
    return getNativeDefaultTimeout(
      this.source,
      nativeContextPageBindings.get(page)?.context,
      navigation,
    )
  }
  goto(url: string, options: NativeNavigationOptions = {}): Promise<NativeResponse | null> {
    return runNativeNavigation(this.source, () => this.navigateTo(url, options))
  }
  private async navigateTo(
    url: string,
    options: NativeNavigationOptions,
  ): Promise<NativeResponse | null> {
    this.signal.throwIfAborted()
    const config = NavigationOptionsSchema.parse(options)
    const page = this instanceof NativeFrame ? this.page() : this
    url = resolveNativeContextUrl(page, url)
    const network = this.networkController()
    const result = await this.runAutomation('navigateFrame', {
      url,
      waitUntil: config.waitUntil ?? 'load',
      timeoutMs: config.timeout,
      api: this instanceof NativeFrame ? 'frame.goto' : 'page.goto',
      ...(config.referer !== undefined ? { referer: config.referer } : {}),
    })
    return result === null ? null : network.navigationResponse(result)
  }

  url(): string {
    return this.source.url
  }
  ariaSnapshot(options: NativeAriaSnapshotOptions = {}): Promise<string> {
    return captureNativeAriaSnapshot(this.locator('body, frameset'), options)
  }
  ariaSnapshotJSON(options: NativeAriaSnapshotJSONOptions = {}): Promise<NativeAriaJSONNode[]> {
    return captureNativeAriaSnapshotJSON(this.locator('body, frameset'), options)
  }
  title(): Promise<string> {
    return this.evaluate(() => document.title)
  }
  reload(options: Omit<NativeNavigationOptions, 'referer'> = {}): Promise<NativeResponse | null> {
    return runNativeNavigation(this.source, () => this.reloadPage(options))
  }
  private async reloadPage(
    options: Omit<NativeNavigationOptions, 'referer'> = {},
  ): Promise<NativeResponse | null> {
    const config = NavigationOptionsSchema.omit({ referer: true }).parse(options)
    const result =
      this.source.state === 'failed'
        ? await this.source.runAutomation(
            'recoverPage',
            {
              waitUntil: config.waitUntil ?? 'load',
              timeoutMs: config.timeout ?? this.defaultTimeout(true),
              api: 'page.reload',
            },
            this.signal,
          )
        : await this.runAutomation('reload', {
            waitUntil: config.waitUntil ?? 'load',
            timeoutMs: config.timeout,
            api: 'page.reload',
          })
    return result === null ? null : this.networkController().navigationResponse(result)
  }
  goBack(options: Omit<NativeNavigationOptions, 'referer'> = {}): Promise<NativeResponse | null> {
    return runNativeNavigation(this.source, () =>
      this.navigateHistory('historyBack', 'page.goBack', options),
    )
  }
  goForward(
    options: Omit<NativeNavigationOptions, 'referer'> = {},
  ): Promise<NativeResponse | null> {
    return runNativeNavigation(this.source, () =>
      this.navigateHistory('historyForward', 'page.goForward', options),
    )
  }
  private async navigateHistory(
    command: 'historyBack' | 'historyForward',
    api: 'page.goBack' | 'page.goForward',
    options: Omit<NativeNavigationOptions, 'referer'>,
  ): Promise<NativeResponse | null> {
    const config = NavigationOptionsSchema.omit({ referer: true }).parse(options)
    const network = this.networkController()
    const result = await this.runAutomation(command, {
      waitUntil: config.waitUntil ?? 'load',
      timeoutMs: config.timeout,
      api,
    })
    return result === null ? null : network.navigationResponse(result)
  }
  async setViewportSize(size: { width: number; height: number }): Promise<void> {
    const config = z
      .strictObject({ width: z.number().int().min(1), height: z.number().int().min(1) })
      .parse(size)
    const result = await this.runAutomation('viewport', config)
    this.viewport = z
      .object({ width: z.number().int().positive(), height: z.number().int().positive() })
      .parse(result)
  }
  viewportSize(): { width: number; height: number } | null {
    const viewport = this.viewport ?? this.source.viewport
    return viewport ? { ...viewport } : null
  }
  /** Completes after this page's native target acknowledges activation. */
  async bringToFront(): Promise<void> {
    if (this.frameId) throw new Error('bringToFront requires a native page, not a frame')
    await this.runAutomation('bringToFront', { api: 'page.bringToFront' })
  }
  async requestGC(): Promise<void> {
    await this.runAutomation('gc', {})
  }
  screenshot(options: NativeScreenshotOptions = {}): Promise<Buffer> {
    return captureNativeScreenshot(
      (params, signal) => this.runAutomation('artifact', params, signal),
      options,
      this instanceof NativeFrame ? this.page() : this,
      this.source,
      this.signal,
    )
  }
  pdf(options: NativePDFOptions = {}): Promise<Buffer> {
    return printNativePDF(
      (params, signal) => this.runAutomation('artifact', params, signal),
      options,
      this.source,
      this.signal,
    )
  }
  /** Native current-document time budgets; capability limits are explicit. */
  get virtualTime(): NativeVirtualTime {
    if (this.frameId) throw new Error('Native virtual-time ownership requires a page, not a frame')
    return new NativeVirtualTime({
      runAutomation: (command, params, signal) => this.runAutomation(command, params, signal),
    })
  }

  /** Stable native recording handle; remains available after actual page closure. */
  video(): NativeVideo | null {
    return nativeContextPageBindings.get(this)?.video ?? null
  }

  /** Shared native request ownership, created only when network APIs are used. */
  get request(): NativeAPIRequestContext {
    if (nativeContextPageBindings.has(this)) return getNativePageContext(this).request
    return (this.nativeRequestContext ??= NativeAPIRequestContext.forPage(this.source))
  }
  routeFromHAR(path: string, options: NativeRouteFromHAROptions = {}): Promise<void> {
    return routeNativeFromHAR(
      { runAutomation: (command, params) => this.runAutomation(command, params) },
      path,
      options,
      this.defaultTimeout(),
    )
  }
  networkController(): NativeNetwork {
    return (this.nativeNetwork ??= NativeNetwork.forPage(this))
  }
  private nativeDOMReader(selector: string, options: NativePageDOMReadOptions) {
    const { strict, ...readOptions } = PageDOMReadOptionsSchema.parse(options)
    return {
      locator: new NativeLocator(
        this,
        z.string().parse(selector),
        undefined,
        undefined,
        undefined,
        strict ?? false,
      ),
      options: readOptions,
    }
  }
  async innerHTML(selector: string, options: NativePageDOMReadOptions = {}): Promise<string> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.innerHTML(read.options)
  }
  async getAttribute(
    selector: string,
    name: string,
    options: NativePageDOMReadOptions = {},
  ): Promise<string | null> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.getAttribute(name, read.options)
  }
  async isVisible(selector: string, options: NativePageDOMReadOptions = {}): Promise<boolean> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.isVisible(read.options)
  }
  async isHidden(selector: string, options: NativePageDOMReadOptions = {}): Promise<boolean> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.isHidden(read.options)
  }
  async isEnabled(selector: string, options: NativePageDOMReadOptions = {}): Promise<boolean> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.isEnabled(read.options)
  }
  async isDisabled(selector: string, options: NativePageDOMReadOptions = {}): Promise<boolean> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.isDisabled(read.options)
  }
  async isEditable(selector: string, options: NativePageDOMReadOptions = {}): Promise<boolean> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.isEditable(read.options)
  }
  route(
    url: Parameters<NativeNetwork['route']>[0],
    handler: (route: NativeRoute, request: NativeRequest) => void | Promise<void>,
    options: { times?: number } = {},
  ): Promise<void> {
    return this.networkController().route(url, handler, options)
  }
  unroute(
    url: Parameters<NativeNetwork['unroute']>[0],
    handler?: (route: NativeRoute, request: NativeRequest) => void | Promise<void>,
  ): Promise<void> {
    return this.networkController().unroute(url, handler)
  }
  unrouteAll(options: { behavior?: 'default' | 'wait' | 'ignoreErrors' } = {}): Promise<void> {
    return this.networkController().unrouteAll(options)
  }
  setExtraHTTPHeaders(headers: Record<string, string>): Promise<void> {
    return this.networkController().setExtraHTTPHeaders(headers)
  }
  waitForRequest(
    matcher: string | RegExp | ((request: NativeRequest) => boolean | Promise<boolean>),
    options: { timeout?: number } = {},
  ): Promise<NativeRequest> {
    return this.networkController().waitForRequest(matcher, options)
  }
  waitForResponse(
    matcher: string | RegExp | ((response: NativeResponse) => boolean | Promise<boolean>),
    options: { timeout?: number } = {},
  ): Promise<NativeResponse> {
    return this.networkController().waitForResponse(matcher, options)
  }
  async emulateIdleState(state?: NativeIdleState): Promise<void> {
    if (this.frameId) throw new Error('emulateIdleState requires a native page, not a frame')
    await emulateNativeIdleState((params) => this.runAutomation('artifact', params), state)
  }
  emulateMedia(options: NativeMediaOptions = {}): Promise<void> {
    return emulateNativeMedia((params) => this.runAutomation('artifact', params), options)
  }
  private bindingController(): NativeBindings {
    this.signal.throwIfAborted()
    if (!this.nativeBindings) {
      const projection = this instanceof NativeFrame ? this.page() : this
      const entry = nativeContextPageBindings.get(projection)
      const owner = entry?.context.pageById(entry.id) ?? projection
      if (!owner.source.onBindingMessage) throw new Error('Native binding events are unavailable')
      const subscribe = owner.source.onBindingMessage.bind(owner.source)
      this.nativeBindings = NativeBindings.forSource(
        owner.source,
        () =>
          new NativeBindings(
            {
              get signal() {
                return owner.source.signal
              },
              pageSignal: owner.source.pageSignal ?? owner.source.signal,
              subscribeCrash: owner.source.onCrashMessage?.bind(owner.source),
              run: (params, signal) => owner.runAutomation('bindings', params, signal),
              subscribe,
            },
            (info) => ({ context: owner.context(), page: owner, frame: owner.frameObject(info) }),
            (value) => {
              if (!(value instanceof NativeJSHandle)) return undefined
              value.assertUsable(owner.source)
              return value.id
            },
            (info, value) => createHandle(owner.frameObject(info), value),
          ),
      )
    }
    return this.nativeBindings
  }
  /** Internal host adoption for context-owned native bridge registrations. */
  adoptNativeContextBinding(
    name: string,
    callback: NativeBindingCallback,
    binding: boolean,
    handle: boolean,
  ): void {
    this.bindingController().adoptInherited(name, callback, binding, handle)
  }
  /** Context disposal owns the native operation; this only removes host admission. */
  dropNativeContextBinding(name: string): void {
    this.nativeBindings?.dropInherited(name)
  }
  exposeFunction(name: string, callback: NativeBindingCallback): Promise<NativeExposedBinding> {
    return this.bindingController().expose(name, callback)
  }
  exposeBinding(
    name: string,
    callback: (
      source: NativeBindingSource,
      ...args: never[]
    ) => UntrustedInput | Promise<UntrustedInput>,
    options: { handle?: boolean } = {},
  ): Promise<NativeExposedBinding> {
    const config = z.strictObject({ handle: z.boolean().optional() }).parse(options)
    return this.bindingController().expose(name, callback, true, config.handle ?? false)
  }
  context(): NativeBrowserContext {
    return getNativePageContext(this)
  }
  addLocatorHandler<T extends NativeLocator | NativeElementHandle>(
    locator: T,
    handler: NativeLocatorHandler<T>,
    options: NativeLocatorHandlerOptions = {},
  ): Promise<void> {
    assertNativeBrowserCallbackAdmission()
    if (this.frameId)
      throw new Error('Native locator handlers belong to a page, not a frame facade')
    if (!(locator instanceof NativeLocator) && !(locator instanceof NativeElementHandle))
      throw new TypeError('Native locator handler requires a native locator or element handle')
    const target = locator.nativeHandlerDescriptor(this)
    this.nativeLocatorHandlers ??= new NativeLocatorHandlers(this.source)
    return this.nativeLocatorHandlers.add(locator, target, handler, options)
  }
  removeLocatorHandler(locator: NativeLocator | NativeElementHandle): Promise<void> {
    assertNativeBrowserCallbackAdmission()
    if (this.frameId)
      throw new Error('Native locator handlers belong to a page, not a frame facade')
    if (!(locator instanceof NativeLocator) && !(locator instanceof NativeElementHandle))
      throw new TypeError('Native locator handler requires a native locator or element handle')
    locator.nativeHandlerDescriptor(this)
    return this.nativeLocatorHandlers?.remove(locator) ?? Promise.resolve()
  }
  close(options: { reason?: string; runBeforeUnload?: boolean } = {}): Promise<void> {
    const config = z
      .strictObject({
        reason: z.string().max(4096).optional(),
        runBeforeUnload: z.boolean().optional(),
      })
      .parse(options)
    prepareNativeNavigationClose(this.source)
    if (nativeContextPageBindings.has(this))
      return closeNativeContextPage(this, {
        ...(config.reason === undefined ? {} : { reason: config.reason }),
        ...(config.runBeforeUnload === undefined
          ? {}
          : { runBeforeUnload: config.runBeforeUnload }),
      })
    if (config.runBeforeUnload) {
      if (!this.source.runContext || !this.source.pageId)
        return Promise.reject(
          new Error('Native beforeunload close requires the page context channel'),
        )
      return this.source
        .runContext('closePage', { pageId: this.source.pageId, ...config })
        .then(() => undefined)
    }
    return this.source.close()
  }
  opener(): Promise<NativePage | null> {
    return nativePageOpener(this)
  }
  mainFrame(): NativeFrame {
    if (!this.rootFrame) {
      const info = this.source.frameInfos?.().find((frame) => !frame.parentId)
      this.rootFrame = new NativeFrame(this, info, this.signal)
      if (info) this.frameObjects.set(info.id, this.rootFrame)
    }
    return this.rootFrame
  }
  frames(): NativeFrame[] {
    const infos = this.source.frameInfos?.()
    if (!infos) return [this.mainFrame()]
    const live = new Set(infos.map((info) => info.id))
    for (const [id, frame] of this.frameObjects) {
      if (!live.has(id)) {
        frame.markDetached()
        this.rememberDetached(id, frame)
        this.frameObjects.delete(id)
      }
    }
    return infos.map((info) => this.frameObject(info))
  }
  frame(
    options: string | { name?: string; url?: string | RegExp | ((url: URL) => boolean) },
  ): NativeFrame | null {
    const query = typeof options === 'string' ? { name: options } : options
    const page = this instanceof NativeFrame ? this.page() : this
    const urlPattern =
      typeof query.url === 'string'
        ? nativeURLGlobMatcher(resolveNativeContextUrlGlob(page, query.url))
        : undefined
    return (
      this.frames().find((frame) => {
        if (query.name !== undefined && frame.name() !== query.name) return false
        if (query.url === undefined) return true
        if (typeof query.url === 'string') {
          if (!urlPattern) throw new Error('Native frame URL matcher is unavailable')
          return urlPattern(frame.url())
        }
        if (query.url instanceof RegExp) {
          query.url.lastIndex = 0
          return query.url.test(frame.url())
        }
        return z.boolean().parse(query.url(new URL(frame.url())))
      }) ?? null
    )
  }
  private frameObject(info: NativeFrameInfo): NativeFrame {
    if (!info.parentId) {
      const frame = this.mainFrame()
      frame.update(info)
      return frame
    }
    let frame = this.frameObjects.get(info.id)
    frame ??= this.detachedFrames.get(info.id)?.deref()
    if (!frame) {
      frame = new NativeFrame(this, info, this.signal)
      this.frameObjects.set(info.id, frame)
    }
    frame.update(info)
    return frame
  }
  private rememberDetached(id: string, frame: NativeFrame): void {
    this.detachedFrames.set(id, new WeakRef(frame))
    if (this.detachedFrames.size > 1024) {
      const oldest = this.detachedFrames.keys().next().value
      if (oldest !== undefined) this.detachedFrames.delete(oldest)
    }
  }
  on(event: 'popup' | 'close' | 'crash', listener: PageListener): this
  on(event: 'filechooser', listener: FileChooserListener): this
  on(event: 'worker', listener: (worker: NativeWorker) => void): this
  on(event: 'websocket', listener: (socket: NativeWebSocket) => void): this
  on(event: 'download', listener: DownloadListener): this
  on(event: 'dialog' | 'dialogclosed', listener: DialogListener): this
  on(event: 'response', listener: ResponseListener): this
  on(event: RequestEventName, listener: RequestListener): this
  on(event: 'pageerror', listener: PageErrorListener): this
  on(event: 'console', listener: ConsoleListener): this
  on(event: FrameEventName, listener: FrameListener): this
  on(event: PageEventName, callback: PageListenerCallback): this {
    return this.addPageListener(event, callback)
  }
  prependListener(event: 'popup' | 'close' | 'crash', listener: PageListener): this
  prependListener(event: 'filechooser', listener: FileChooserListener): this
  prependListener(event: 'worker', listener: (worker: NativeWorker) => void): this
  prependListener(event: 'websocket', listener: (socket: NativeWebSocket) => void): this
  prependListener(event: 'download', listener: DownloadListener): this
  prependListener(event: 'dialog' | 'dialogclosed', listener: DialogListener): this
  prependListener(event: 'response', listener: ResponseListener): this
  prependListener(event: RequestEventName, listener: RequestListener): this
  prependListener(event: 'pageerror', listener: PageErrorListener): this
  prependListener(event: 'console', listener: ConsoleListener): this
  prependListener(event: FrameEventName, listener: FrameListener): this
  prependListener(event: PageEventName, callback: PageListenerCallback): this {
    return this.addPageListener(event, callback, true)
  }
  readonly addListener = this.on.bind(this)
  readonly removeListener = this.off.bind(this)
  private addPageListener(
    event: PageEventName,
    callback: PageListenerCallback,
    prepend = false,
  ): this {
    if (event === 'crash') {
      NativeCrashEvents.forPage(this).on(callback as PageListener, prepend)
      return this
    }
    if (event === 'websocket') {
      const listener = callback as (socket: NativeWebSocket) => void
      if (prepend) this.socketListeners.unshift(listener)
      else this.socketListeners.push(listener)
      if (!this.detachSocketEvents) {
        const detach = this.socketEvents().on((socket) => {
          for (const handler of [...this.socketListeners]) handler(socket)
        })
        const aborted = () => {
          detach()
          this.socketListeners.length = 0
          this.detachSocketEvents = undefined
        }
        this.signal.addEventListener('abort', aborted, { once: true })
        this.detachSocketEvents = () => {
          detach()
          this.signal.removeEventListener('abort', aborted)
        }
      }
      return this
    }
    if (event === 'worker') {
      this.workerEvents().on(callback as (worker: NativeWorker) => void, prepend)
      return this
    }
    if (event === 'download') {
      this.downloadEvents().on(callback as DownloadListener, prepend)
      return this
    }
    if (event === 'filechooser') {
      this.fileChooserEvents().on(callback as FileChooserListener, prepend)
      return this
    }

    if (event === 'dialog' || event === 'dialogclosed') {
      NativeDialogEvents.forPage(this).on(
        callback as DialogListener,
        event === 'dialogclosed',
        prepend,
      )
      return this
    }
    if (event === 'popup' || event === 'close') {
      onNativeContextPageEvent(this, event, callback as PageListener, prepend)
      return this
    }
    if (event === 'response') {
      this.networkController().on(event, callback as ResponseListener, prepend)
      return this
    }
    if (event === 'request' || event === 'requestfinished' || event === 'requestfailed') {
      this.networkController().on(event, callback as RequestListener, prepend)
      return this
    }
    if (event === 'pageerror') {
      if (!this.source.onPageErrorMessage) throw new Error('Native page error events unavailable')
      if (prepend) this.pageErrorListeners.unshift(callback as PageErrorListener)
      else this.pageErrorListeners.push(callback as PageErrorListener)
      if (!this.detachPageErrorEvents) {
        const detach = this.source.onPageErrorMessage((info) => {
          const error = createNativePageError(info)
          for (const listener of [...this.pageErrorListeners]) listener(error)
        })
        const aborted = () => {
          detach()
          this.pageErrorListeners.length = 0
          this.detachPageErrorEvents = undefined
        }
        this.signal.addEventListener('abort', aborted, { once: true })
        this.detachPageErrorEvents = () => {
          detach()
          this.signal.removeEventListener('abort', aborted)
        }
      }
      return this
    }
    if (event === 'console') {
      if (!this.source.onConsoleMessage)
        throw new Error('Native console events unavailable on this source')
      const listener = callback as ConsoleListener
      if (prepend) this.consoleListeners.unshift(listener)
      else this.consoleListeners.push(listener)
      if (!this.detachConsoleEvents) {
        const detach = this.source.onConsoleMessage((info) => {
          const message = new NativeConsoleMessage(this, info)
          for (const listener of [...this.consoleListeners]) listener(message)
        })
        const aborted = () => {
          detach()
          this.consoleListeners.length = 0
          this.detachConsoleEvents = undefined
        }
        this.signal.addEventListener('abort', aborted, { once: true })
        this.detachConsoleEvents = () => {
          detach()
          this.signal.removeEventListener('abort', aborted)
        }
      }
      return this
    }
    const listener = callback as FrameListener
    let listeners = this.frameListeners.get(event)
    if (!listeners) {
      listeners = []
      this.frameListeners.set(event, listeners)
    }
    if (prepend) listeners.unshift(listener)
    else listeners.push(listener)
    if (!this.detachFrameEvents) {
      if (!this.source.onAutomationEvent)
        throw new Error('Native frame events unavailable on this source')
      const detach = this.source.onAutomationEvent((event) => {
        const frame = this.frameObject(event.frame)
        if (event.event === 'framedetached') {
          frame.markDetached()
          this.rememberDetached(event.frame.id, frame)
          this.frameObjects.delete(event.frame.id)
        }
        for (const listener of [...(this.frameListeners.get(event.event) ?? [])]) listener(frame)
      })
      const aborted = () => {
        detach()
        this.frameListeners.clear()
        this.frameObjects.clear()
        this.detachedFrames.clear()
        this.detachFrameEvents = undefined
      }
      this.signal.addEventListener('abort', aborted, { once: true })
      this.detachFrameEvents = () => {
        detach()
        this.signal.removeEventListener('abort', aborted)
      }
    }
    return this
  }
  off(event: 'popup' | 'close' | 'crash', listener: PageListener): this
  off(event: 'filechooser', listener: FileChooserListener): this
  off(event: 'worker', listener: (worker: NativeWorker) => void): this
  off(event: 'websocket', listener: (socket: NativeWebSocket) => void): this
  off(event: 'download', listener: DownloadListener): this
  off(event: 'dialog' | 'dialogclosed', listener: DialogListener): this
  off(event: 'response', listener: ResponseListener): this
  off(event: RequestEventName, listener: RequestListener): this
  off(event: 'pageerror', listener: PageErrorListener): this
  off(event: 'console', listener: ConsoleListener): this
  off(event: FrameEventName, listener: FrameListener): this
  off(event: PageEventName, callback: PageListenerCallback): this {
    if (event === 'crash') {
      NativeCrashEvents.forPage(this).off(callback as PageListener)
      return this
    }
    if (event === 'websocket') {
      const index = this.socketListeners.lastIndexOf(callback as (socket: NativeWebSocket) => void)
      if (index >= 0) this.socketListeners.splice(index, 1)
      if (!this.socketListeners.length) {
        this.detachSocketEvents?.()
        this.detachSocketEvents = undefined
      }
      return this
    }
    if (event === 'worker') {
      this.workerEvents().off(callback as (worker: NativeWorker) => void)
      return this
    }
    if (event === 'download') {
      this.downloadEvents().off(callback as DownloadListener)
      return this
    }
    if (event === 'filechooser') {
      this.fileChooserEvents().off(callback as FileChooserListener)
      return this
    }

    if (event === 'dialog' || event === 'dialogclosed') {
      NativeDialogEvents.forPage(this).off(callback as DialogListener, event === 'dialogclosed')
      return this
    }
    if (event === 'popup' || event === 'close') {
      offNativeContextPageEvent(this, event, callback as PageListener)
      return this
    }
    if (
      event === 'response' ||
      event === 'request' ||
      event === 'requestfinished' ||
      event === 'requestfailed'
    ) {
      this.networkController().off(event, callback as RequestListener | ResponseListener)
      return this
    }
    if (event === 'pageerror') {
      const index = this.pageErrorListeners.lastIndexOf(callback as PageErrorListener)
      if (index >= 0) this.pageErrorListeners.splice(index, 1)
      if (!this.pageErrorListeners.length) {
        this.detachPageErrorEvents?.()
        this.detachPageErrorEvents = undefined
      }
      return this
    }
    if (event === 'console') {
      const index = this.consoleListeners.lastIndexOf(callback as ConsoleListener)
      if (index >= 0) this.consoleListeners.splice(index, 1)
      if (!this.consoleListeners.length) {
        this.detachConsoleEvents?.()
        this.detachConsoleEvents = undefined
      }
      return this
    }
    const listener = callback as FrameListener
    const listeners = this.frameListeners.get(event)
    const index = listeners?.lastIndexOf(listener) ?? -1
    if (index >= 0) listeners?.splice(index, 1)
    if (!listeners?.length) this.frameListeners.delete(event)
    if (!this.frameListeners.size) {
      this.detachFrameEvents?.()
      this.detachFrameEvents = undefined
    }
    return this
  }
  once(event: 'popup' | 'close' | 'crash', listener: PageListener): this
  once(event: 'filechooser', listener: FileChooserListener): this
  once(event: 'worker', listener: (worker: NativeWorker) => void): this
  once(event: 'websocket', listener: (socket: NativeWebSocket) => void): this
  once(event: 'download', listener: DownloadListener): this
  once(event: 'dialog' | 'dialogclosed', listener: DialogListener): this
  once(event: 'response', listener: ResponseListener): this
  once(event: RequestEventName, listener: RequestListener): this
  once(event: 'pageerror', listener: PageErrorListener): this
  once(event: 'console', listener: ConsoleListener): this
  once(event: FrameEventName, listener: FrameListener): this
  once(event: PageEventName, callback: PageListenerCallback): this {
    if (event === 'download') {
      const once = (download: NativeDownload): void => {
        this.off(event, once)
        ;(callback as DownloadListener)(download)
      }
      return this.on(event, once)
    }
    if (event === 'filechooser') {
      const once = (chooser: NativeFileChooser): void => {
        this.off(event, once)
        ;(callback as FileChooserListener)(chooser)
      }
      return this.on(event, once)
    }

    if (event === 'dialog' || event === 'dialogclosed') {
      const once = (dialog: NativeDialog): void => {
        this.off(event, once)
        ;(callback as DialogListener)(dialog)
      }
      return this.on(event, once)
    }
    if (event === 'popup' || event === 'close' || event === 'crash') {
      const once = (page: NativePage) => {
        this.off(event, once)
        ;(callback as PageListener)(page)
      }
      return this.on(event, once)
    }
    if (event === 'response') {
      const once = (response: NativeResponse) => {
        this.off(event, once)
        ;(callback as ResponseListener)(response)
      }
      return this.on(event, once)
    }
    if (event === 'request' || event === 'requestfinished' || event === 'requestfailed') {
      const once = (request: NativeRequest) => {
        this.off(event, once)
        ;(callback as RequestListener)(request)
      }
      return this.on(event, once)
    }
    if (event === 'pageerror') {
      const once = (error: Error) => {
        this.off(event, once)
        ;(callback as PageErrorListener)(error)
      }
      return this.on(event, once)
    }
    if (event === 'console') {
      const once = (message: NativeConsoleMessage) => {
        this.off(event, once)
        ;(callback as ConsoleListener)(message)
      }
      return this.on(event, once)
    }
    if (event === 'websocket') {
      const once = (socket: NativeWebSocket) => {
        this.off(event, once)
        ;(callback as (socket: NativeWebSocket) => void)(socket)
      }
      return this.on(event, once)
    }
    if (event === 'worker') {
      const once = (worker: NativeWorker) => {
        this.off('worker', once)
        ;(callback as (worker: NativeWorker) => void)(worker)
      }
      return this.on('worker', once)
    }
    const listener = callback as FrameListener
    const once = (frame: NativeFrame) => {
      this.off(event, once)
      listener(frame)
    }
    return this.on(event, once)
  }
  async waitForEvent(
    event: 'download',
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativeDownload> })
      | NativeEventPredicate<NativeDownload>,
  ): Promise<NativeDownload>
  async waitForEvent(
    event: 'websocket',
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativeWebSocket> })
      | NativeEventPredicate<NativeWebSocket>,
  ): Promise<NativeWebSocket>
  async waitForEvent(
    event: 'worker',
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativeWorker> })
      | NativeEventPredicate<NativeWorker>,
  ): Promise<NativeWorker>
  async waitForEvent(
    event: 'filechooser',
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativeFileChooser> })
      | NativeEventPredicate<NativeFileChooser>,
  ): Promise<NativeFileChooser>
  async waitForEvent(
    event: 'crash',
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativePage> })
      | NativeEventPredicate<NativePage>,
  ): Promise<NativePage>
  async waitForEvent(event: 'popup' | 'close', options?: { timeout?: number }): Promise<NativePage>
  async waitForEvent(
    event: 'dialog' | 'dialogclosed',
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativeDialog> })
      | NativeEventPredicate<NativeDialog>,
  ): Promise<NativeDialog>
  async waitForEvent(
    event: 'response',
    options?: {
      timeout?: number
      predicate?: (response: NativeResponse) => boolean | Promise<boolean>
    },
  ): Promise<NativeResponse>
  async waitForEvent(
    event: RequestEventName,
    options?: {
      timeout?: number
      predicate?: (request: NativeRequest) => boolean | Promise<boolean>
    },
  ): Promise<NativeRequest>
  async waitForEvent(
    event: 'pageerror',
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<Error> })
      | NativeEventPredicate<Error>,
  ): Promise<Error>
  async waitForEvent(
    event: 'console',
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativeConsoleMessage> })
      | NativeEventPredicate<NativeConsoleMessage>,
  ): Promise<NativeConsoleMessage>
  async waitForEvent(
    event: FrameEventName,
    options?:
      | (NativeEventPredicateOptions & { predicate?: NativeEventPredicate<NativeFrame> })
      | NativeEventPredicate<NativeFrame>,
  ): Promise<NativeFrame>
  async waitForEvent(
    event: PageEventName,
    options:
      | (NativeEventPredicateOptions & { predicate?: (value: never) => boolean | Promise<boolean> })
      | ((value: never) => boolean | Promise<boolean>) = {},
  ): Promise<
    | Error
    | NativeFrame
    | NativeConsoleMessage
    | NativePage
    | NativeRequest
    | NativeResponse
    | NativeDialog
    | NativeFileChooser
    | NativeDownload
    | NativeWorker
    | NativeWebSocket
  > {
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
        predicate: z
          .custom<(value: never) => boolean | Promise<boolean>>(
            (value) => typeof value === 'function',
          )
          .optional(),
      })
      .parse(typeof options === 'function' ? { predicate: options } : options)
    config.timeout ??= this.defaultTimeout()
    if (event === 'crash') return NativeCrashEvents.forPage(this).wait(config)
    if (event === 'websocket') {
      return this.socketEvents().waitForEvent({
        timeout: config.timeout,
        signal: config.signal,
        ...(config.predicate
          ? { predicate: config.predicate as NativeEventPredicate<NativeWebSocket> }
          : {}),
      })
    }
    if (event === 'worker') {
      const events = this.workerEvents()
      if (config.predicate)
        return waitNativeEventPredicate(
          this.source,
          event,
          (value) => events.fromRecord(value),
          config.predicate as NativeEventPredicate<NativeWorker>,
          config,
        )
      const result = await this.runAutomation(
        'waitEvent',
        {
          event,
          timeoutMs: config.timeout,
          ...(this.source.eventSequence ? { afterSequence: this.source.eventSequence() } : {}),
        },
        config.signal,
      )
      return events.fromRecord(result)
    }
    if (
      config.predicate &&
      ['console', 'pageerror', 'frameattached', 'framenavigated', 'framedetached'].includes(event)
    ) {
      return waitNativeEventPredicate(
        this.source,
        event,
        (value) => {
          if (event === 'pageerror')
            return createNativePageError(NativePageErrorInfoSchema.parse(value))
          if (event === 'console')
            return new NativeConsoleMessage(this, NativeConsoleInfoSchema.parse(value))
          const info = NativeFrameInfoSchema.parse(value)
          const frame = this.frameObject(info)
          if (event === 'framedetached') {
            frame.markDetached()
            this.rememberDetached(info.id, frame)
            this.frameObjects.delete(info.id)
          }
          return frame
        },
        config.predicate as NativeEventPredicate<NativeConsoleMessage | NativeFrame | Error>,
        config,
      )
    }
    if (
      config.predicate &&
      ![
        'response',
        'request',
        'requestfinished',
        'requestfailed',
        'dialog',
        'dialogclosed',
        'filechooser',
        'download',
      ].includes(event)
    )
      throw new Error('Unsupported native event predicate')
    if (event === 'download')
      return this.downloadEvents().wait({
        timeout: config.timeout,
        signal: config.signal,
        ...(config.predicate
          ? { predicate: config.predicate as NativeEventPredicate<NativeDownload> }
          : {}),
      })
    if (event === 'filechooser')
      return this.fileChooserEvents().wait({
        timeout: config.timeout,
        signal: config.signal,
        ...(config.predicate
          ? { predicate: config.predicate as NativeEventPredicate<NativeFileChooser> }
          : {}),
      })
    if (event === 'popup' || event === 'close')
      return waitNativeContextPageEvent(this, event, config)
    if (event === 'dialog' || event === 'dialogclosed')
      return NativeDialogEvents.forPage(this).wait(
        {
          timeout: config.timeout,
          signal: config.signal,
          ...(config.predicate
            ? { predicate: config.predicate as NativeEventPredicate<NativeDialog> }
            : {}),
        },
        event === 'dialogclosed',
      )
    if (event === 'response')
      return this.networkController().waitForEvent(event, {
        timeout: config.timeout,
        signal: config.signal,
        ...(config.predicate
          ? {
              predicate: config.predicate as (
                response: NativeResponse,
              ) => boolean | Promise<boolean>,
            }
          : {}),
      })
    if (event === 'request' || event === 'requestfinished' || event === 'requestfailed')
      return this.networkController().waitForEvent(event, {
        timeout: config.timeout,
        signal: config.signal,
        ...(config.predicate
          ? {
              predicate: config.predicate as (request: NativeRequest) => boolean | Promise<boolean>,
            }
          : {}),
      })
    const result = await this.runAutomation('waitEvent', {
      event,
      timeoutMs: config.timeout,
      ...(this.source.eventSequence ? { afterSequence: this.source.eventSequence() } : {}),
    })
    if (event === 'pageerror') return createNativePageError(NativePageErrorInfoSchema.parse(result))
    if (event === 'console')
      return new NativeConsoleMessage(this, NativeConsoleInfoSchema.parse(result))
    const info = NativeFrameInfoSchema.parse(result)
    const frame = this.frameObject(info)
    if (event === 'framedetached') {
      frame.markDetached()
      this.rememberDetached(info.id, frame)
      this.frameObjects.delete(info.id)
    }
    return frame
  }

  async consoleMessages(
    options: { filter?: 'all' | 'since-navigation' } = {},
  ): Promise<NativeConsoleMessage[]> {
    return (await readNativeConsoleMessages(this, options)).map(
      (info) => new NativeConsoleMessage(this, info),
    )
  }
  clearConsoleMessages(): Promise<void> {
    return clearNativeConsoleMessages(this)
  }
  pageErrors(options: { filter?: 'all' | 'since-navigation' } = {}): Promise<Error[]> {
    return readNativePageErrors(this, options)
  }

  async waitForURL(
    matcher: string | RegExp | NativeURLPredicate,
    options: Omit<NativeNavigationOptions, 'referer'> = {},
  ): Promise<void> {
    this.signal.throwIfAborted()
    const config = NavigationOptionsSchema.omit({ referer: true }).parse(options)
    let predicate: NativeURLPredicate
    if (typeof matcher === 'string') {
      const page = this instanceof NativeFrame ? this.page() : this
      const matches = nativeURLGlobMatcher(resolveNativeContextUrlGlob(page, matcher))
      predicate = (url) => matches(url.href)
    } else if (matcher instanceof RegExp) {
      const pattern = new RegExp(matcher.source, matcher.flags)
      predicate = (url) => {
        pattern.lastIndex = 0
        return pattern.test(url.href)
      }
    } else if (typeof matcher === 'function') predicate = matcher
    else throw new Error('waitForURL requires a URL glob, RegExp or predicate')
    await runNativeURLPredicate(this.source, predicate, {
      waitForURL: true,
      timeout: config.timeout ?? this.defaultTimeout(true),
      waitUntil: config.waitUntil ?? 'load',
      api: this instanceof NativeFrame ? 'frame.waitForURL' : 'page.waitForURL',
      ...(this.frameId ? { frame: this.frameId } : {}),
      signal: this.signal,
    })
  }

  waitForNavigation(options: NativeNavigationOptions = {}): Promise<NativeResponse | null> {
    return runNativeNavigation(this.source, () => this.observeNavigation(options))
  }
  private async observeNavigation(
    options: NativeNavigationOptions,
  ): Promise<NativeResponse | null> {
    const config = NavigationOptionsSchema.omit({ referer: true }).parse(options)
    const current = this.source
      .frameInfos?.()
      .find((frame) => (this.frameId ? frame.id === this.frameId : !frame.parentId))
    const network = this.networkController()
    const result = await this.runAutomation('waitFrameNavigation', {
      timeoutMs: config.timeout,
      waitUntil: config.waitUntil ?? 'load',
      afterNavigation: current?.navigationSequence ?? 0,
      api: this instanceof NativeFrame ? 'frame.waitForNavigation' : 'page.waitForNavigation',
    })
    return result === null ? null : network.navigationResponse(result)
  }

  setContent(html: string): Promise<void> {
    return this.evaluateNative(
      (content: string) => {
        document.open()
        document.write(content)
        document.close()
      },
      html,
      'setContent',
    )
  }

  async addStyleTag(options: {
    content?: string
    url?: string
    path?: string
  }): Promise<NativeElementHandle> {
    return runNativeRetryOperation(async () => {
      const config = withoutNativeRetryOperation(() =>
        z
          .strictObject({
            content: z.string().optional(),
            url: z.string().optional(),
            path: z.string().optional(),
          })
          .parse(options),
      )
      if (Object.values(config).filter((value) => value !== undefined).length !== 1)
        throw new Error('addStyleTag requires exactly one of content, url or path')
      const content =
        config.path !== undefined ? await readFile(config.path, 'utf8') : config.content
      const handle = await this.evaluateHandle(
        async ({ content, url }) => {
          if (url !== undefined) {
            const node = document.createElement('link')
            node.rel = 'stylesheet'
            const loaded = new Promise<void>((resolve, reject) => {
              node.onload = () => resolve()
              node.onerror = () => reject(new Error('Stylesheet failed to load'))
            })
            node.href = url
            document.head.append(node)
            await loaded
            return node
          }
          const node = document.createElement('style')
          node.textContent = content ?? ''
          document.head.append(node)
          return node
        },
        { content, url: config.url },
      )
      return handle as NativeElementHandle
    })
  }

  async addScriptTag(options: {
    content?: string
    url?: string
    path?: string
    type?: string
  }): Promise<NativeElementHandle> {
    return runNativeRetryOperation(async () => {
      const config = withoutNativeRetryOperation(() =>
        z
          .strictObject({
            content: z.string().optional(),
            url: z.string().optional(),
            path: z.string().optional(),
            type: z.string().optional(),
          })
          .parse(options),
      )
      if (
        [config.content, config.url, config.path].filter((value) => value !== undefined).length !==
        1
      )
        throw new Error('addScriptTag requires exactly one of content, url or path')
      const content =
        config.path !== undefined ? await readFile(config.path, 'utf8') : config.content
      const handle = await this.evaluateHandle(
        async ({ content, url, type }) => {
          const node = document.createElement('script')
          if (type !== undefined) node.type = type
          const asynchronous = url !== undefined || type === 'module'
          const loaded = asynchronous
            ? new Promise<void>((resolve, reject) => {
                node.onload = () => resolve()
                node.onerror = () => reject(new Error('Script failed to load'))
              })
            : undefined
          if (url !== undefined) node.src = url
          else node.textContent = content ?? ''
          document.head.append(node)
          await loaded
          return node
        },
        { content, url: config.url, type: config.type },
      )
      return handle as NativeElementHandle
    })
  }

  async addInitScript<A = undefined>(
    script: string | { content?: string; path?: string } | ((arg: A) => void),
    arg?: A,
  ): Promise<void> {
    return runNativeRetryOperation(async () => {
      let body: string
      if (typeof script === 'function') {
        const { encoded, handles } = evaluationArgument(this, arg as UntrustedInput)
        if (handles.length) throw new Error('Initialization scripts cannot retain native handles')
        body = `(${evaluationBody(script)})((${createNativeValueCodec.toString()})().decode(${JSON.stringify(encoded)}));`
      } else if (typeof script === 'string') body = script
      else {
        const config = withoutNativeRetryOperation(() =>
          z
            .strictObject({ content: z.string().optional(), path: z.string().optional() })
            .parse(script),
        )
        if ((config.content !== undefined) === (config.path !== undefined))
          throw new Error('addInitScript requires exactly one of content or path')
        body =
          config.path !== undefined ? await readFile(config.path, 'utf8') : (config.content ?? '')
      }
      await this.source.sendDevToolsCommand('Page.addScriptToEvaluateOnNewDocument', {
        source: body,
      })
    })
  }
  click(selector: string, options: NativeClickOptions & { strict?: boolean } = {}): Promise<void> {
    const { strict, ...config } = ClickOptionsSchema.extend({
      strict: z.boolean().optional(),
    }).parse(options)
    return new NativeLocator(
      this,
      selector,
      [{ engine: 'css', selector }],
      [],
      undefined,
      strict ?? false,
    ).click(config as NativeClickOptions)
  }
  dblclick(selector: string, options?: Omit<NativeClickOptions, 'clickCount'>): Promise<void> {
    return this.locator(selector).dblclick(options)
  }
  tap(selector: string, options: NativeTapOptions & { strict?: boolean } = {}): Promise<void> {
    const { strict, ...config } = TapOptionsSchema.extend({ strict: z.boolean().optional() }).parse(
      options,
    )
    return new NativeLocator(
      this,
      selector,
      [{ engine: 'css', selector }],
      [],
      undefined,
      strict ?? false,
    ).tap(config)
  }

  hover(selector: string, options: NativeHoverOptions & { strict?: boolean } = {}): Promise<void> {
    const { strict, ...config } = HoverOptionsSchema.extend({
      strict: z.boolean().optional(),
    }).parse(options)
    return new NativeLocator(
      this,
      selector,
      [{ engine: 'css', selector }],
      [],
      undefined,
      strict ?? false,
    ).hover(config)
  }

  async dispatchEvent(
    selector: string,
    type: string,
    eventInit: UntrustedInput = {},
    options: NativePageDispatchEventOptions = {},
  ): Promise<void> {
    const { strict, ...config } = PageDispatchEventOptionsSchema.parse(options)
    await new NativeLocator(
      this,
      selector,
      [{ engine: 'css', selector }],
      [],
      undefined,
      strict ?? false,
    ).dispatchEvent(type, eventInit, config)
  }

  async fill(
    selector: string,
    value: string,
    options: NativePageEditableOptions = {},
  ): Promise<void> {
    const { strict, ...config } = PageEditableOptionsSchema.parse(options)
    return new NativeLocator(
      this,
      selector,
      [{ engine: 'css', selector }],
      [],
      undefined,
      strict ?? false,
    )
      .fill(value, config)
      .catch((error: UntrustedInput) => {
        if (config.signal?.aborted && error === config.signal.reason) throw error
        if (error instanceof NativeTimeoutError && error.message.startsWith('fill: '))
          throw new NativeTimeoutError(`page.${error.message}`, { cause: error })
        if (error instanceof Error && error.message.startsWith('fill: '))
          throw new Error(`page.${error.message}`, { cause: error })
        throw error
      })
  }

  async isChecked(selector: string, options: NativePageDOMReadOptions = {}): Promise<boolean> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.isChecked(read.options)
  }
  dragAndDrop(source: string, target: string, options: NativeDragOptions = {}): Promise<void> {
    return this.locator(source).dragTo(this.locator(target), options)
  }
  private nativeFormTarget(selector: string, options: NativePageFormOptions) {
    const { strict, ...config } = parseNativePageFormOptions(options)
    return {
      locator: new NativeLocator(
        this,
        selector,
        [{ engine: 'css', selector }],
        [],
        undefined,
        strict ?? false,
      ),
      options: config,
    }
  }
  async check(selector: string, options: NativePageFormOptions = {}): Promise<void> {
    const target = this.nativeFormTarget(selector, options)
    return target.locator.check(target.options)
  }
  async uncheck(selector: string, options: NativePageFormOptions = {}): Promise<void> {
    const target = this.nativeFormTarget(selector, options)
    return target.locator.uncheck(target.options)
  }
  async setChecked(
    selector: string,
    checked: boolean,
    options: NativePageFormOptions = {},
  ): Promise<void> {
    const target = this.nativeFormTarget(selector, options)
    return target.locator.setChecked(checked, target.options)
  }
  async selectOption(
    selector: string,
    values: NativeSelectValues,
    options: NativePageFormOptions = {},
  ): Promise<string[]> {
    const target = this.nativeFormTarget(selector, options)
    return target.locator.selectOption(values, target.options)
  }
  async setInputFiles(
    selector: string,
    files: NativeInputFiles,
    options: Omit<NativePageFormOptions, 'force'> = {},
  ): Promise<void> {
    const target = this.nativeFormTarget(selector, options)
    return target.locator.setInputFiles(files, target.options)
  }

  focus(selector: string, options: { timeout?: number } = {}): Promise<void> {
    return this.locator(selector).focus(options)
  }
  press(selector: string, key: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    return this.locator(selector).press(key, options)
  }
  type(selector: string, text: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    return this.locator(selector).type(text, options)
  }
  async inputValue(selector: string, options: NativePageDOMReadOptions = {}): Promise<string> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.inputValue(read.options)
  }
  async textContent(
    selector: string,
    options: NativePageDOMReadOptions = {},
  ): Promise<string | null> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.textContent(read.options)
  }
  async innerText(selector: string, options: NativePageDOMReadOptions = {}): Promise<string> {
    const read = this.nativeDOMReader(selector, options)
    return read.locator.innerText(read.options)
  }

  $eval<R, A>(
    selector: string,
    fn: (element: HTMLElement | SVGElement, arg: A) => R | Promise<R>,
    arg?: A,
  ): Promise<R> {
    if (arguments.length > 3)
      return Promise.reject(new Error('Too many arguments; pass arguments as a single object'))
    return this.locator(selector).evaluateAll(
      async (elements, { selector, body, value }) => {
        const element = elements[0]
        if (!element) throw new Error(`No element matches ${selector}`)
        const evaluate = (0, eval)(`(${body})`) as (
          element: Element,
          arg: A | undefined,
        ) => R | Promise<R>
        return await evaluate(element, value)
      },
      { selector, body: evaluationBody(fn), value: arg },
    )
  }

  $$eval<R, A>(
    selector: string,
    fn: (elements: (HTMLElement | SVGElement)[], arg: A) => R | Promise<R>,
    arg?: A,
  ): Promise<R> {
    if (arguments.length > 3)
      return Promise.reject(new Error('Too many arguments; pass arguments as a single object'))
    return this.locator(selector).evaluateAll(
      async (elements, { body, value }) => {
        const evaluate = (0, eval)(`(${body})`) as (
          elements: Element[],
          arg: A | undefined,
        ) => R | Promise<R>
        return await evaluate(elements, value)
      },
      { selector, body: evaluationBody(fn), value: arg },
    )
  }

  async waitForSelector(
    selector: string,
    options: {
      state?: 'attached' | 'detached' | 'visible' | 'hidden'
      timeout?: number
      strict?: boolean
    } = {},
  ): Promise<NativeElementHandle | null> {
    const config = z
      .strictObject({
        state: z.enum(['attached', 'detached', 'visible', 'hidden']).optional(),
        timeout: z.number().int().min(0).max(120_000).optional(),
        strict: z.boolean().optional(),
      })
      .parse(options)
    const validated = z
      .string()
      .min(1)
      .refine((value) => !!value.trim())
      .parse(selector)
    const state = config.state ?? 'visible'
    if (state === 'hidden' || state === 'detached') {
      await new NativeLocator(
        this,
        validated,
        [{ engine: 'css', selector: validated }],
        [],
        undefined,
        config.strict ?? false,
      ).waitFor({ state, ...(config.timeout === undefined ? {} : { timeout: config.timeout }) })
      return null
    }
    const handle = createHandle(
      this,
      await this.runAutomation('handle', {
        action: 'create',
        query: [{ engine: 'css', selector: validated }],
        selectorState: state,
        strict: config.strict ?? false,
        timeoutMs: config.timeout ?? this.defaultTimeout(),
      }).catch((error: UntrustedInput) => {
        if (error instanceof NativeTimeoutError)
          throw new NativeTimeoutError(
            `page.waitForSelector: Timeout ${config.timeout ?? this.defaultTimeout()}ms exceeded, waiting for ${validated} to be ${state}`,
            { cause: error },
          )
        throw error
      }),
    )
    const element = handle.asElement()
    if (element) return element
    await handle.dispose()
    throw new Error('Native selector wait did not return an actual DOM element')
  }

  async $(
    selector: string,
    options: { strict?: boolean } = {},
  ): Promise<NativeElementHandle | null> {
    if (typeof selector !== 'string')
      throw new Error(`selector: expected string, got ${typeof selector}`)
    const { strict } = z.strictObject({ strict: z.boolean().optional() }).parse(options)
    return new NativeLocator(
      this,
      selector,
      [{ engine: 'css', selector }],
      [],
      undefined,
      strict ?? false,
    ).queryElementNow()
  }
  $$(selector: string): Promise<NativeElementHandle[]> {
    return this.locator(selector).elementHandles()
  }

  sendDevToolsCommand(
    method: string,
    params?: Record<string, UntrustedInput>,
  ): Promise<UntrustedInput> {
    this.signal?.throwIfAborted()
    // A frame command must use the native target session belonging to its
    // immutable frame identity; OOPIF domains are absent from the root target.
    if (this.frameId) return this.runAutomation('devtools', { method, params: params ?? {} })
    return raceWithAbort(this.source.sendDevToolsCommand(method, params), this.signal)
  }

  locator(selector: string, options?: NativeLocatorFilterOptions): NativeLocator {
    if (!selector.trim()) throw new Error('A native locator requires a selector')
    const locator = new NativeLocator(this, selector)
    return options ? locator.filter(options) : locator
  }
  getByRef(ref: string): NativeLocator {
    return new NativeLocator(this, '<native snapshot reference>', [nativeReferenceQuery(ref)])
  }
  getByRole(role: string, options: NativeRoleOptions = {}): NativeLocator {
    return this.locator(':scope').getByRole(role, options)
  }
  getByText(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByText(text, options)
  }
  getByLabel(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByLabel(text, options)
  }
  getByPlaceholder(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByPlaceholder(text, options)
  }
  getByAltText(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByAltText(text, options)
  }
  getByTitle(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByTitle(text, options)
  }
  getByTestId(text: string | RegExp): NativeLocator {
    return this.locator(':scope').getByTestId(text)
  }
  frameLocator(selector: string): NativeFrameLocator {
    return new NativeFrameLocator(this, [[{ engine: 'css', selector }]])
  }

  /** Native Inspector hit testing in this actual page/frame viewport. */
  async pickLocator(
    point: NativeLocatorPickPoint,
    options: NativeLocatorSuggestionOptions = {},
  ): Promise<NativeLocatorSuggestions> {
    const coordinates = NativeLocatorPickPointSchema.parse(point)
    const config = NativeLocatorSuggestionOptionsSchema.parse(options)
    config.signal?.throwIfAborted()
    return projectNativeLocatorSuggestions(
      this,
      decodeNativeLocatorSuggestions(
        await this.runAutomation(
          'pickLocator',
          {
            ...coordinates,
            timeoutMs: config.timeout ?? this.defaultTimeout(),
          },
          config.signal,
        ),
      ),
    )
  }
  runAutomation(
    command: Exclude<Parameters<NativeBrowser['runAutomation']>[0], 'cancel'>,
    params: Record<string, UntrustedInput> = {},
    signal?: AbortSignal,
  ): Promise<UntrustedInput> {
    this.signal.throwIfAborted()
    const timeoutMs =
      typeof params.timeoutMs === 'number'
        ? params.timeoutMs
        : this.defaultTimeout(
            command === 'navigateFrame' ||
              command === 'reload' ||
              command === 'historyBack' ||
              command === 'historyForward' ||
              command === 'waitLoadState' ||
              command === 'waitFrameNavigation',
          )
    const traceCommands = new Set([
      'evaluate',
      'navigateFrame',
      'reload',
      'historyBack',
      'historyForward',
      'waitTimeout',
      'waitEvent',
      'locator',
      'form',
      'artifact',
      'input',
      'handle',
      'queryEvaluate',
      'snapshotReferences',
      'highlight',
    ])
    const traceMetadata = traceCommands.has(command) ? { traceStack: nativeTraceStack() } : {}
    return this.source.runAutomation(
      command,
      this.frameId
        ? { ...params, ...traceMetadata, timeoutMs, frame: this.frameId }
        : { ...params, ...traceMetadata, timeoutMs },
      signal ? AbortSignal.any([this.signal, signal]) : this.signal,
    )
  }

  async evaluate<R>(fn: (() => R | Promise<R>) | string): Promise<R>
  async evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg: A): Promise<R>
  async evaluate<R, A>(fn: ((arg: A) => R | Promise<R>) | string, arg?: A): Promise<R> {
    this.signal?.throwIfAborted()
    if (arguments.length > 2)
      throw new Error('Too many arguments; pass arguments as a single object')
    return this.evaluateNative(fn, arg)
  }

  private async evaluateNative<R, A>(
    fn: ((arg: A) => R | Promise<R>) | string,
    arg?: A,
    traceMethod?: string,
  ): Promise<R> {
    const codec = createNativeValueCodec()
    const { encoded, handles } = evaluationArgument(this, arg as UntrustedInput)
    const body = evaluationBody(fn)
    const invoke =
      typeof fn === 'string'
        ? `(0,eval)(${JSON.stringify(body)})`
        : `(${body}).call(globalThis,codec.decode(${JSON.stringify(encoded)},values))`
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=[];for(let i=0;i<handles.length;i++)values[i]=handles[i].value;return codec.encode(await (${invoke}));}`
    const labeled = declaration + '\n//# sourceURL=suitecut://native/page.evaluate\n'
    const params = handles.length
      ? { function: labeled, handles }
      : { expression: `(${declaration})()\n//# sourceURL=suitecut://native/page.evaluate\n` }
    const result = EvaluationResult.parse(
      await raceWithAbort(
        this.runAutomation('evaluate', { ...params, ...(traceMethod ? { traceMethod } : {}) }),
        this.signal,
      ),
    )
    if (result.exceptionDetails)
      throw new Error(`Native page evaluation failed: ${evaluationError(result.exceptionDetails)}`)
    if (result.result?.unserializableValue)
      throw new Error('Native evaluation requires a JSON-serializable result')
    // Like browser evaluate APIs, the caller declares the serialized result type.
    return codec.decode(result.result?.value as UntrustedInput) as R
  }

  async evaluateHandle(
    fn: (() => UntrustedInput | Promise<UntrustedInput>) | string,
  ): Promise<NativeJSHandle>
  async evaluateHandle<A>(
    fn: (arg: A) => UntrustedInput | Promise<UntrustedInput>,
    arg: A,
  ): Promise<NativeJSHandle>
  async evaluateHandle<A>(
    fn: ((arg: A) => UntrustedInput | Promise<UntrustedInput>) | string,
    arg?: A,
  ): Promise<NativeJSHandle> {
    if (arguments.length > 2)
      throw new Error('Too many arguments; pass arguments as a single object')
    const { encoded, handles } = evaluationArgument(this, arg as UntrustedInput)
    const expression =
      typeof fn === 'string'
        ? `(0,eval)(${JSON.stringify(fn)})`
        : `(${evaluationBody(fn)}).call(globalThis,codec.decode(${JSON.stringify(encoded)},values))`
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=[];for(let i=0;i<handles.length;i++)values[i]=handles[i].value;return {value:await (${expression})};}`
    const params = handles.length
      ? { function: declaration, handles }
      : {
          expression: `(async()=>{const codec=(${createNativeValueCodec.toString()})();const handles=[];const values=[];return await (${expression});})()`,
        }
    return createHandle(this, await this.runAutomation('handle', { action: 'create', ...params }))
  }

  async waitForFunction<R, A>(
    fn: ((arg: A) => R | Promise<R>) | string,
    arg?: A,
    options: { timeout?: number; polling?: 'raf' | number } = {},
  ): Promise<NativeJSHandle> {
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        polling: z.union([z.literal('raf'), z.number().int().min(1).max(120_000)]).optional(),
      })
      .parse(options)
    const { encoded, handles } = evaluationArgument(this, arg as UntrustedInput)
    const invoke =
      typeof fn === 'string'
        ? `(0,eval)(${JSON.stringify(fn)})`
        : `(${evaluationBody(fn)}).call(globalThis,codec.decode(${JSON.stringify(encoded)},values))`
    const raf =
      (config.polling ?? 'raf') === 'raf' ? 'await new Promise(requestAnimationFrame);' : ''
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=handles.map(handle=>handle.value);${raf}return {value:await (${invoke})};}`
    const params = handles.length
      ? { function: declaration, handles }
      : {
          expression: `(async()=>{const codec=(${createNativeValueCodec.toString()})();const values=[];${raf}return await (${invoke});})()`,
        }
    return createHandle(
      this,
      await this.runAutomation('handle', {
        action: 'create',
        predicate: true,
        pollInterval: typeof config.polling === 'number' ? config.polling : 0,
        timeoutMs: config.timeout,
        ...params,
      }),
    )
  }

  waitForTimeout(ms: number): Promise<void> {
    if (!Number.isFinite(ms) || ms < 0) return Promise.reject(new Error('Invalid wait duration'))
    return this.runAutomation('waitTimeout', { ms, timeoutMs: 0 }).then(() => undefined)
  }

  async waitForLoadState(
    state: NativeLoadState = 'load',
    options: { timeout?: number } = {},
  ): Promise<void> {
    const config = NavigationOptionsSchema.omit({ waitUntil: true, referer: true }).parse(options)
    await this.runAutomation('waitLoadState', {
      waitUntil: state,
      timeoutMs: config.timeout,
      api: this instanceof NativeFrame ? 'frame.waitForLoadState' : 'page.waitForLoadState',
    })
  }

  isClosed(): boolean {
    return isNativeContextPageClosed(this)
  }
}

/** A frame identity backed by the C++ execution-context registry. */
export class NativeFrame extends NativePage {
  private detached = false
  private info: NativeFrameInfo | undefined
  constructor(
    private readonly owner: NativePage,
    info?: NativeFrameInfo,
    signal?: AbortSignal,
  ) {
    super(owner.source, signal, info?.id)
    this.info = info
  }
  update(info: NativeFrameInfo): void {
    // A native predicate snapshot can precede its public frame packet. Never
    // replace that newer observation with an older registry/journal entry.
    if (
      this.info?.id === info.id &&
      this.info.navigationSequence !== undefined &&
      info.navigationSequence !== undefined &&
      info.navigationSequence < this.info.navigationSequence
    )
      return
    this.info = info
  }
  markDetached(): void {
    this.detached = true
  }
  page(): NativePage {
    return this.owner
  }
  override context(): NativeBrowserContext {
    return this.owner.context()
  }
  override async runAutomation(
    command: Parameters<NativePage['runAutomation']>[0],
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput> {
    try {
      return await super.runAutomation(command, params, signal)
    } catch (error) {
      if (error instanceof Error && /[Ff]rame (?:was )?detach/u.test(error.message))
        throw new Error(
          `frame.${command === 'navigateFrame' ? 'goto' : command}: Frame was detached`,
          { cause: error },
        )
      throw error
    }
  }
  override networkController(): NativeNetwork {
    return this.owner.networkController()
  }
  override get request(): NativeAPIRequestContext {
    return this.owner.request
  }
  override mainFrame(): NativeFrame {
    return this.owner.mainFrame()
  }
  override frames(): NativeFrame[] {
    return this.owner.frames()
  }
  name(): string {
    return this.currentInfo()?.name ?? ''
  }
  /** Native frame registry identity, stable across renderer process swaps. */
  identity(): string | undefined {
    return this.currentInfo()?.id ?? this.info?.id
  }
  override url(): string {
    return this.currentInfo()?.url ?? this.info?.url ?? this.owner.url()
  }
  private currentInfo(): NativeFrameInfo | undefined {
    const info = this.source
      .frameInfos?.()
      .find((info) => (this.frameId ? info.id === this.frameId : !info.parentId))
    if (!info) return undefined
    this.update(info)
    return this.info
  }
  /** The actual DOM owner in the parent frame, retained by native backend identity. */
  async frameElement(): Promise<NativeElementHandle> {
    const handle = createHandle(
      this,
      await this.runAutomation('handle', { action: 'frameElement' }),
    )
    const element = handle.asElement()
    if (!element) {
      await handle.dispose()
      throw new Error('Native frame owner is not an element')
    }
    return element
  }
  parentFrame(): NativeFrame | null {
    if (this.isDetached()) return null
    const parentId = this.currentInfo()?.parentId ?? this.info?.parentId
    if (!parentId) return null
    return this.owner.frames().find((frame) => frame.frameId === parentId) ?? null
  }
  childFrames(): NativeFrame[] {
    const id = this.currentInfo()?.id
    return id ? this.owner.frames().filter((frame) => frame.currentInfo()?.parentId === id) : []
  }
  isDetached(): boolean {
    return (
      this.detached ||
      this.source.state !== 'running' ||
      (!!this.frameId && !!this.source.frameInfos && !this.currentInfo())
    )
  }
}

// Internal cleanup uses the physical source lifetime, independently of a caller's
// canceled page projection. An acknowledged native release alone marks disposal.
const collectHandleProperties = Symbol('collectHandleProperties')
const cleanupHandleId = Symbol('cleanupHandleId')
const markCleanupDisposed = Symbol('markCleanupDisposed')

async function failWithHandleCleanup(
  handles: Iterable<NativeJSHandle>,
  error: UntrustedInput,
  message: string,
): Promise<never> {
  const owned = [...new Set(handles)].filter((handle) => handle[cleanupHandleId]() !== null)
  const first = owned[0]
  if (first) {
    try {
      await first.source.runAutomation(
        'handle',
        { action: 'disposeMany', handles: owned.map((handle) => handle.id), timeoutMs: 1000 },
        AbortSignal.any([first.source.signal, AbortSignal.timeout(1000)]),
      )
      for (const handle of owned) handle[markCleanupDisposed]()
    } catch (failure) {
      throw new AggregateError([error, failure], message, { cause: failure })
    }
  }
  throw error instanceof Error
    ? error
    : new Error('Native handle operation failed', { cause: error })
}

async function collectElementHandles(array: NativeJSHandle): Promise<NativeElementHandle[]> {
  const owned: NativeJSHandle[] = [array]
  try {
    const properties = await array[collectHandleProperties](owned)
    const elements: NativeElementHandle[] = []
    for (const handle of properties.values()) {
      const element = handle.asElement()
      if (element) elements.push(element)
      else await handle.dispose()
    }
    await array.dispose()
    return elements
  } catch (error) {
    return failWithHandleCleanup(
      owned,
      error as UntrustedInput,
      'Native selector handle lookup and cleanup failed',
    )
  }
}

/** A C++-owned object reference, invalidated by disposal or document navigation. */
export class NativeJSHandle {
  private disposed = false
  constructor(
    protected readonly owner: NativePage,
    readonly id: number,
    private readonly preview = 'JSHandle@object',
    readonly guid?: string,
  ) {}
  get source(): NativeBrowser {
    return this.owner.source
  }
  assertUsable(source: NativeBrowser): void {
    if (this.disposed)
      throw new Error(
        this.guid
          ? `no object with guid ${this.guid} (native handle is disposed)`
          : 'Native handle is disposed',
      )
    if (source !== this.source)
      throw new Error('Native handle belongs to a different browser source')
  }
  async evaluate<R, A>(
    fn: ((value: UntrustedInput, arg: A) => R | Promise<R>) | string,
    arg?: A,
  ): Promise<R> {
    if (arguments.length > 2)
      throw new Error('Too many arguments; pass arguments as a single object')
    this.assertUsable(this.source)
    const codec = createNativeValueCodec()
    const { encoded, handles } = evaluationArgument(this.owner, arg as UntrustedInput)
    const invoke =
      typeof fn === 'string'
        ? `(0,eval)(${JSON.stringify(fn)})`
        : `(${evaluationBody(fn)}).call(globalThis,this.value,codec.decode(${JSON.stringify(encoded)},values))`
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=[];for(let i=0;i<handles.length;i++)values[i]=handles[i].value;return codec.encode(await (${invoke}));}`
    const result = EvaluationResult.parse(
      await this.owner.runAutomation('handle', {
        action: 'evaluate',
        handle: this.id,
        function: declaration,
        handles,
      }),
    )
    if (result.exceptionDetails)
      throw new Error(
        `Native handle evaluation failed: ${evaluationError(result.exceptionDetails)}`,
      )
    return codec.decode(result.result?.value as UntrustedInput) as R
  }
  async evaluateHandle<A>(
    fn: ((value: UntrustedInput, arg: A) => UntrustedInput | Promise<UntrustedInput>) | string,
    arg?: A,
  ): Promise<NativeJSHandle> {
    if (arguments.length > 2)
      throw new Error('Too many arguments; pass arguments as a single object')
    this.assertUsable(this.source)
    if (typeof fn === 'string') return this.owner.evaluateHandle(fn)
    return this.owner.evaluateHandle(
      async ({ handle, body, value }) => {
        const callback = (0, eval)(`(${body})`) as (
          node: UntrustedInput,
          arg: A | undefined,
        ) => UntrustedInput | Promise<UntrustedInput>
        return await callback.call(globalThis, handle, value)
      },
      { handle: this, body: evaluationBody(fn), value: arg },
    )
  }

  jsonValue(): Promise<UntrustedInput> {
    return this.evaluate((value) => value)
  }
  asElement(): NativeElementHandle | null {
    return null
  }
  toString(): string {
    return this.preview
  }
  async getProperty(name: string): Promise<NativeJSHandle> {
    this.assertUsable(this.source)
    return createHandle(
      this.owner,
      await this.owner.runAutomation('handle', {
        action: 'property',
        handle: this.id,
        name,
      }),
    )
  }
  getProperties(): Promise<Map<string, NativeJSHandle>> {
    return this[collectHandleProperties]()
  }
  async [collectHandleProperties](owned?: NativeJSHandle[]): Promise<Map<string, NativeJSHandle>> {
    const names = await this.evaluate((value) => {
      if (value === null || (typeof value !== 'object' && typeof value !== 'function')) return []
      const keys = Object.keys(value)
      if (keys.length > 1024) throw new Error('Native handle property limit exceeded')
      return keys
    })
    const properties = new Map<string, NativeJSHandle>()
    try {
      for (const name of names) {
        const handle = await this.getProperty(name)
        properties.set(name, handle)
        owned?.push(handle)
      }
      return properties
    } catch (error) {
      if (owned) throw error
      return failWithHandleCleanup(
        properties.values(),
        error as UntrustedInput,
        'Native property lookup and cleanup failed',
      )
    }
  }
  [cleanupHandleId](): number | null {
    return this.disposed ? null : this.id
  }
  [markCleanupDisposed](): void {
    this.disposed = true
  }
  async dispose(): Promise<void> {
    if (this.disposed) return
    await this.owner.runAutomation('handle', { action: 'dispose', handle: this.id })
    this.disposed = true
  }
}

/** A fixed DOM node reference with the same native ownership as other handles. */
export class NativeElementHandle extends NativeJSHandle {
  /** Exact retained target capture for page-owned native locator handlers. */
  nativeHandlerDescriptor(page: NativePage): Record<string, UntrustedInput> {
    const actual = this.owner instanceof NativeFrame ? this.owner.page() : this.owner
    if (actual.source !== page.source)
      throw new Error('Native locator handler belongs to another page')
    return new NativeLocator(
      this.owner,
      '<native element handle>',
      [],
      [],
      this.id,
    ).nativeHandlerDescriptor(page)
  }

  /** Suggests recipes for this exact retained Element; never reselects by text. */
  suggestLocator(options: NativeLocatorSuggestionOptions = {}): Promise<NativeLocatorSuggestions> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).suggest(
      options,
    )
  }

  /** The genuine current frame of this node's ownerDocument, including adoption. */
  async ownerFrame(): Promise<NativeFrame | null> {
    this.assertUsable(this.source)
    const result = z
      .object({ pageId: z.number().int().positive(), frame: z.string().min(1).max(256) })
      .nullable()
      .parse(await this.owner.runAutomation('handle', { action: 'ownerFrame', handle: this.id }))
    if (result === null) return null
    const page = this.owner instanceof NativeFrame ? this.owner.page() : this.owner
    const context = nativeContextPageBindings.get(page)?.context
    const target =
      context?.pageById(result.pageId) ?? (page.source.pageId === result.pageId ? page : null)
    if (!target) throw new Error('Native owner frame page identity is unavailable')
    const frame = target.frames().find((candidate) => candidate.identity() === result.frame)
    if (!frame) throw new Error('Native owner frame identity is no longer available')
    return frame
  }

  async waitForSelector(
    selector: string,
    options: {
      state?: 'attached' | 'detached' | 'visible' | 'hidden'
      timeout?: number
      strict?: boolean
    } = {},
  ): Promise<NativeElementHandle | null> {
    this.assertUsable(this.source)
    const selected = z
      .string()
      .min(1)
      .refine((value) => !!value.trim())
      .parse(selector)
    const config = z
      .strictObject({
        state: z.enum(['attached', 'detached', 'visible', 'hidden']).optional(),
        timeout: z.number().int().min(0).max(120_000).optional(),
        strict: z.boolean().optional(),
      })
      .parse(options)
    const state = config.state ?? 'visible'
    const timeoutMs = config.timeout ?? this.owner.defaultTimeout()
    const query = [{ engine: 'css', selector: selected }]
    const timeout = (cause?: UntrustedInput) =>
      new NativeTimeoutError(
        `elementHandle.waitForSelector: Timeout ${timeoutMs}ms exceeded, waiting for ${selected} to be ${state}`,
        { cause },
      )
    if (state === 'hidden' || state === 'detached') {
      const result = EvaluationResult.parse(
        await this.owner.runAutomation('queryEvaluate', {
          nativeRootHandle: this.id,
          query,
          all: true,
          dom: true,
          expectation: true,
          visibility: state === 'hidden',
          timeoutMs,
          function: `function(nodes,state){if(${config.strict ?? false}&&nodes.length>1)throw new Error('strict mode violation: locator resolved to '+nodes.length+' elements');return {matches:${state === 'hidden' ? '!state?.visible' : '!nodes.length'},actual:null};}`,
        }),
      )
      if (result.exceptionDetails) throw new Error(evaluationError(result.exceptionDetails))
      if (!z.object({ matches: z.boolean() }).parse(result.result?.value).matches) throw timeout()
      return null
    }
    const handle = createHandle(
      this.owner,
      await this.owner
        .runAutomation('handle', {
          action: 'create',
          nativeRootHandle: this.id,
          query,
          selectorState: state,
          strict: config.strict ?? false,
          timeoutMs,
        })
        .catch((error: UntrustedInput) => {
          if (error instanceof NativeTimeoutError) throw timeout(error)
          throw error
        }),
    )
    const element = handle.asElement()
    if (element) return element
    await handle.dispose()
    throw new Error('Native scoped selector wait did not return an actual DOM element')
  }

  private async scopedHandle(selector: string, all: boolean): Promise<NativeJSHandle> {
    this.assertUsable(this.source)
    const selected = z
      .string()
      .min(1)
      .refine((value) => !!value.trim())
      .parse(selector)
    return createHandle(
      this.owner,
      await this.owner.runAutomation('handle', {
        action: 'create',
        nativeRootHandle: this.id,
        query: [{ engine: 'css', selector: selected }],
        all,
        strict: false,
      }),
    )
  }

  async $(selector: string): Promise<NativeElementHandle | null> {
    const handle = await this.scopedHandle(selector, false)
    const element = handle.asElement()
    if (!element) await handle.dispose()
    return element
  }

  async $$(selector: string): Promise<NativeElementHandle[]> {
    const array = await this.scopedHandle(selector, true)
    return collectElementHandles(array)
  }

  private async evaluateScoped<R, A>(
    selector: string,
    fn:
      | ((element: HTMLElement | SVGElement, arg: A) => R | Promise<R>)
      | ((elements: (HTMLElement | SVGElement)[], arg: A) => R | Promise<R>),
    arg: A | undefined,
    all: boolean,
  ): Promise<R> {
    this.assertUsable(this.source)
    const selected = z
      .string()
      .min(1)
      .refine((value) => !!value.trim())
      .parse(selector)
    const { encoded, handles } = evaluationArgument(this.owner, arg as UntrustedInput)
    const body = evaluationBody(fn)
    const declaration = `async function(nodes,...handles){const codec=(${createNativeValueCodec.toString()})();const values=handles.map(handle=>handle.value);${all ? '' : `if(!nodes.length)throw new Error('Failed to find element matching selector '+${JSON.stringify(JSON.stringify(selected))});`}return codec.encode(await (${body}).call(globalThis,${all ? 'nodes' : 'nodes[0]'},codec.decode(${JSON.stringify(encoded)},values)));}`
    const result = EvaluationResult.parse(
      await this.owner.runAutomation('queryEvaluate', {
        nativeRootHandle: this.id,
        query: [{ engine: 'css', selector: selected }],
        function: declaration,
        handles,
        all: true,
      }),
    )
    if (result.exceptionDetails)
      throw new Error(
        `Native scoped selector evaluation failed: ${evaluationError(result.exceptionDetails)}`,
      )
    return createNativeValueCodec().decode(result.result?.value as UntrustedInput) as R
  }

  $eval<R, A>(
    selector: string,
    fn: (element: HTMLElement | SVGElement, arg: A) => R | Promise<R>,
    arg?: A,
  ): Promise<R> {
    if (arguments.length > 3)
      return Promise.reject(new Error('Too many arguments; pass arguments as a single object'))
    return this.evaluateScoped(selector, fn, arg, false)
  }

  $$eval<R, A>(
    selector: string,
    fn: (elements: (HTMLElement | SVGElement)[], arg: A) => R | Promise<R>,
    arg?: A,
  ): Promise<R> {
    if (arguments.length > 3)
      return Promise.reject(new Error('Too many arguments; pass arguments as a single object'))
    return this.evaluateScoped(selector, fn, arg, true)
  }

  private async observeElementState(visibleOnly = false) {
    this.assertUsable(this.source)
    const result = EvaluationResult.parse(
      await this.owner.runAutomation('queryEvaluate', {
        nativeHandle: this.id,
        all: true,
        dom: true,
        ...(visibleOnly ? { visibility: true } : { elementState: true }),
        function: visibleOnly
          ? 'function(nodes,state){return state;}'
          : "function(nodes,state){if(!nodes[0]?.isConnected)throw new Error('Element is not attached to the DOM');return state;}",
      }),
    )
    if (result.exceptionDetails) throw new Error(evaluationError(result.exceptionDetails))
    return z
      .object({
        visible: z.boolean(),
        enabled: z.boolean().optional(),
        editable: z.boolean().optional(),
      })
      .parse(result.result?.value)
  }

  async isVisible(): Promise<boolean> {
    return (await this.observeElementState(true)).visible
  }
  async isHidden(): Promise<boolean> {
    return !(await this.isVisible())
  }
  async isEnabled(): Promise<boolean> {
    return z.boolean().parse((await this.observeElementState()).enabled)
  }
  async isDisabled(): Promise<boolean> {
    return !(await this.isEnabled())
  }
  async isEditable(): Promise<boolean> {
    const state = await this.observeElementState()
    if (state.editable === undefined)
      throw new Error(
        'Element is not an input, textarea, select, contenteditable or supported readonly role',
      )
    return state.editable
  }
  async waitForElementState(
    state: 'visible' | 'hidden' | 'stable' | 'enabled' | 'disabled' | 'editable',
    options: { timeout?: number } = {},
  ): Promise<void> {
    this.assertUsable(this.source)
    const requested = z
      .enum(['visible', 'hidden', 'stable', 'enabled', 'disabled', 'editable'])
      .parse(state)
    const config = z
      .strictObject({ timeout: z.number().int().min(0).max(120_000).optional() })
      .parse(options)
    const property =
      requested === 'disabled'
        ? '!state?.enabled'
        : requested === 'hidden'
          ? '!state?.visible'
          : `!!state?.${requested}`
    const result = EvaluationResult.parse(
      await this.owner.runAutomation('queryEvaluate', {
        nativeHandle: this.id,
        all: true,
        dom: true,
        expectation: true,
        elementState: true,
        stableState: requested === 'stable',
        timeoutMs: config.timeout ?? this.owner.defaultTimeout(),
        function: `function(nodes,state){if(!nodes[0]?.isConnected&&${requested !== 'hidden'})throw new Error('Element is not attached to the DOM');if(${requested === 'editable'}&&state?.editable===undefined)throw new Error('Element is not an input, textarea, select, contenteditable or supported readonly role');return {matches:${property},actual:null};}`,
      }),
    )
    if (result.exceptionDetails) throw new Error(evaluationError(result.exceptionDetails))
    if (!z.object({ matches: z.boolean() }).parse(result.result?.value).matches)
      throw new NativeTimeoutError(
        `elementHandle.waitForElementState: Timeout ${config.timeout ?? this.owner.defaultTimeout()}ms exceeded, waiting for ${requested}`,
      )
  }

  fill(value: string, options: NativeEditableOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).fill(
      value,
      options,
    )
  }
  clear(options: NativeEditableOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).clear(options)
  }
  async contentFrame(): Promise<NativeFrame | null> {
    this.assertUsable(this.source)
    const result = await this.owner.runAutomation('handle', {
      action: 'contentFrame',
      handle: this.id,
    })
    if (result === null) return null
    const id = z.string().parse(result)
    return this.owner.frames().find((frame) => frame.identity() === id) ?? null
  }
  click(options: NativeClickOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).click(options)
  }
  tap(options: NativeTapOptions = {}): Promise<void> {
    this.assertUsable(this.owner.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).tap(options)
  }
  dispatchEvent(
    type: string,
    eventInit: UntrustedInput = {},
    options: NativeDispatchEventOptions = {},
  ): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).dispatchEvent(
      type,
      eventInit,
      options,
    )
  }
  blur(options: NativeBlurOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).blur(options)
  }
  selectText(options: NativeSelectTextOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).selectText(
      options,
    )
  }
  scrollIntoViewIfNeeded(options: NativeScrollOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(
      this.owner,
      '<native element handle>',
      [],
      [],
      this.id,
    ).scrollIntoViewIfNeeded(options)
  }
  hover(options: NativeHoverOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).hover(options)
  }
  dblclick(options: Omit<NativeClickOptions, 'clickCount'> = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).dblclick(
      options,
    )
  }
  check(options: NativeFormOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).check(options)
  }
  uncheck(options: NativeFormOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).uncheck(
      options,
    )
  }
  setChecked(checked: boolean, options: NativeFormOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).setChecked(
      checked,
      options,
    )
  }
  selectOption(values: NativeSelectValues, options: NativeFormOptions = {}): Promise<string[]> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).selectOption(
      values,
      options,
    )
  }
  setInputFiles(
    files: NativeInputFiles,
    options: Omit<NativeFormOptions, 'force'> = {},
  ): Promise<void> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).setInputFiles(
      files,
      options,
    )
  }
  async inputValue(options: NativeInputValueOptions = {}): Promise<string> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).inputValue(
      options,
    )
  }
  async isChecked(options: NativeDOMReadOptions = {}): Promise<boolean> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).isChecked(
      options,
    )
  }

  async boundingBox(options: NativeDOMReadOptions = {}): Promise<SuiteCutRect | null> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).boundingBox(
      options,
    )
  }
  override asElement(): NativeElementHandle {
    return this
  }
  screenshot(options: NativeLocatorScreenshotOptions = {}): Promise<Buffer> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).screenshot(
      options,
    )
  }
  async focus(options: { timeout?: number } = {}): Promise<void> {
    this.assertUsable(this.source)
    const config = z
      .strictObject({ timeout: z.number().int().min(0).max(120_000).optional() })
      .parse(options)
    await this.owner.runAutomation('handle', {
      action: 'focus',
      handle: this.id,
      timeoutMs: config.timeout,
    })
  }
  async press(key: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    await new NativeLocator(this.owner, '<native element handle>', [], [], this.id).press(
      key,
      options,
    )
  }
  /** Standards send-keys intent with native focus/caret preparation and original deadline. */
  async sendKeys(text: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    await new NativeLocator(this.owner, '<native element handle>', [], [], this.id).sendKeys(
      text,
      options,
    )
  }
  async type(text: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    this.assertUsable(this.source)
    await new NativeLocator(this.owner, '<native element handle>', [], [], this.id).type(
      text,
      options,
    )
  }
  async textContent(options: NativeDOMReadOptions = {}): Promise<string | null> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).textContent(
      options,
    )
  }
  async innerHTML(options: NativeDOMReadOptions = {}): Promise<string> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).innerHTML(
      options,
    )
  }
  async innerText(options: NativeDOMReadOptions = {}): Promise<string> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).innerText(
      options,
    )
  }
  async getAttribute(name: string, options: NativeDOMReadOptions = {}): Promise<string | null> {
    this.assertUsable(this.source)
    return new NativeLocator(this.owner, '<native element handle>', [], [], this.id).getAttribute(
      name,
      options,
    )
  }
}

export interface NativeClickOptions {
  timeout?: number
  button?: 'left' | 'middle' | 'right'
  clickCount?: number
  delay?: number
  steps?: number
  position?: { x: number; y: number }
  modifiers?: ('Alt' | 'Control' | 'ControlOrMeta' | 'Meta' | 'Shift')[]
  force?: boolean
  trial?: boolean
  noWaitAfter?: boolean
  beforeStable?: NativeBeforeStableHook
  signal?: AbortSignal
}
const ClickOptionsSchema = z.strictObject({
  timeout: z.number().int().min(0).max(120_000).optional(),
  button: z.enum(['left', 'middle', 'right']).optional(),
  clickCount: z.number().int().min(1).max(1000).optional(),
  delay: z.number().int().min(0).max(120_000).optional(),
  steps: z.number().int().min(1).max(10000).optional(),
  position: z.strictObject({ x: z.number().finite(), y: z.number().finite() }).optional(),
  modifiers: z
    .array(z.enum(['Alt', 'Control', 'ControlOrMeta', 'Meta', 'Shift']))
    .max(5)
    .optional(),
  force: z.boolean().optional(),
  trial: z.boolean().optional(),
  noWaitAfter: z.boolean().optional(),
  beforeStable: z.custom<NativeBeforeStableHook>((value) => typeof value === 'function').optional(),
  signal: z.instanceof(AbortSignal).optional(),
})
const TapOptionsSchema = ClickOptionsSchema.omit({
  button: true,
  clickCount: true,
  delay: true,
  steps: true,
})

export type NativeTapOptions = z.input<typeof TapOptionsSchema>

const HoverOptionsSchema = ClickOptionsSchema.omit({ button: true, clickCount: true, delay: true })
export type NativeHoverOptions = z.input<typeof HoverOptionsSchema>

const ScrollOptionsSchema = z.strictObject({
  timeout: z.number().int().min(0).max(120_000).optional(),
  signal: z.instanceof(AbortSignal).optional(),
})
export type NativeScrollOptions = z.input<typeof ScrollOptionsSchema>
export type NativeEditableOptions = z.input<typeof ScrollOptionsSchema>
const KeyboardTargetOptionsSchema = ScrollOptionsSchema.extend({
  delay: z.number().int().min(0).max(120_000).optional(),
})
/** One original native target/queue budget, including preparation and key ACKs. */
export type NativeKeyboardTargetOptions = z.input<typeof KeyboardTargetOptionsSchema>
const PageEditableOptionsSchema = ScrollOptionsSchema.extend({ strict: z.boolean().optional() })
export type NativePageEditableOptions = z.input<typeof PageEditableOptionsSchema>

const DispatchEventOptionsSchema = ScrollOptionsSchema
export type NativeDispatchEventOptions = z.input<typeof DispatchEventOptionsSchema>
const PageDispatchEventOptionsSchema = DispatchEventOptionsSchema.extend({
  strict: z.boolean().optional(),
})
export type NativePageDispatchEventOptions = z.input<typeof PageDispatchEventOptionsSchema>

const BlurOptionsSchema = ScrollOptionsSchema
export type NativeBlurOptions = z.input<typeof BlurOptionsSchema>

const DOMReadOptionsSchema = ScrollOptionsSchema
export type NativeDOMReadOptions = z.input<typeof DOMReadOptionsSchema>
/** Native handle arguments are actual renderer values when the predicate runs. */
export type NativePredicateArgument<A> = A extends NativeElementHandle
  ? SVGElement | HTMLElement
  : A extends NativeJSHandle
    ? UntrustedInput
    : A extends Date | RegExp | ArrayBuffer | ArrayBufferView
      ? A
      : A extends object
        ? { [K in keyof A]: NativePredicateArgument<A[K]> }
        : A

const PageDOMReadOptionsSchema = DOMReadOptionsSchema.extend({ strict: z.boolean().optional() })
export type NativePageDOMReadOptions = z.input<typeof PageDOMReadOptionsSchema>

const InputValueOptionsSchema = ScrollOptionsSchema
export type NativeInputValueOptions = z.input<typeof InputValueOptionsSchema>

const SelectTextOptionsSchema = ScrollOptionsSchema.extend({ force: z.boolean().optional() })
export type NativeSelectTextOptions = z.input<typeof SelectTextOptionsSchema>

export interface NativeLocatorFilterOptions {
  hasText?: string | RegExp
  hasNotText?: string | RegExp
  has?: NativeLocator
  hasNot?: NativeLocator
  visible?: boolean
}

/** Strict CSS locator evaluated in its owning native frame. */
export class NativeLocator implements PresentationLocator<NativePage> {
  constructor(
    private readonly owner: NativePage,
    readonly selector: string,
    private readonly query: NativeQueryStep[] = [{ engine: 'css', selector }],
    private readonly framePath: NativeQueryStep[][] = [],
    private readonly nativeHandle?: number,
    private readonly strict = true,
    private readonly descriptionText?: string,
    private readonly normalizationPins?: NativeLocatorNormalization,
  ) {}
  /** Exact target capture; native owns visibility and invocation admission. */
  nativeHandlerDescriptor(page: NativePage): Record<string, UntrustedInput> {
    const actual = this.owner instanceof NativeFrame ? this.owner.page() : this.owner
    if (actual.source !== page.source)
      throw new Error('Native locator handler belongs to another page')
    return {
      selector: this.selector,
      query: this.query,
      framePath: this.framePath,
      strict: this.strict,
      ...(this.nativeHandle === undefined ? {} : { nativeHandle: this.nativeHandle }),
      diagnosticExpressionJSON: this.queryDiagnosticExpression(),
      ...(this.owner instanceof NativeFrame ? { frame: this.owner.identity() } : {}),
    }
  }
  nativeScreenshotDescriptor(): { page: NativePage; target: Record<string, UntrustedInput> } {
    const page = this.owner instanceof NativeFrame ? this.owner.page() : this.owner
    return {
      page,
      target: {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        ...(this.nativeHandle === undefined ? {} : { nativeHandle: this.nativeHandle }),
        ...(this.owner instanceof NativeFrame ? { frame: this.owner.identity() } : {}),
      },
    }
  }
  screenshot(options: NativeLocatorScreenshotOptions = {}): Promise<Buffer> {
    const descriptor = this.nativeScreenshotDescriptor()
    return captureNativeElementScreenshot(
      (params, signal) => descriptor.page.runAutomation('artifact', params, signal),
      descriptor.target,
      options,
      descriptor.page,
      descriptor.page.source,
      nativeArtifactLifetimes.get(descriptor.page),
    )
  }
  dragTo(target: NativeLocator, options: NativeDragOptions = {}): Promise<void> {
    if (!(target instanceof NativeLocator)) throw new Error('Drag target must be a native locator')
    if (target.owner.source !== this.owner.source)
      throw new Error('Drag target belongs to a different native page')
    const destination: Record<string, UntrustedInput> = {
      selector: target.selector,
      query: target.query,
      framePath: target.framePath,
      strict: target.strict,
      ...(target.nativeHandle === undefined ? {} : { nativeHandle: target.nativeHandle }),
      ...(target.owner instanceof NativeFrame && target.owner.identity()
        ? { frame: target.owner.identity() }
        : {}),
    }
    return performNativeDrag(
      (params) =>
        this.owner.runAutomation('form', {
          selector: this.selector,
          query: this.query,
          framePath: this.framePath,
          strict: this.strict,
          ...(this.nativeHandle === undefined ? {} : { nativeHandle: this.nativeHandle }),
          ...params,
        }),
      destination,
      options,
    )
  }
  private formActions(): NativeFormActions {
    return new NativeFormActions(
      (params) =>
        this.owner.runAutomation('form', {
          selector: this.selector,
          query: this.query,
          framePath: this.framePath,
          strict: this.strict,
          ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
          ...params,
        }),
      (value) => {
        if (!(value instanceof NativeElementHandle)) return undefined
        value.assertUsable(this.owner.source)
        return value.id
      },
    )
  }
  check(options: NativeFormOptions = {}): Promise<void> {
    return this.formActions().setChecked(true, options)
  }
  uncheck(options: NativeFormOptions = {}): Promise<void> {
    return this.formActions().setChecked(false, options)
  }
  setChecked(checked: boolean, options: NativeFormOptions = {}): Promise<void> {
    return this.formActions().setChecked(checked, options)
  }
  selectOption(values: NativeSelectValues, options: NativeFormOptions = {}): Promise<string[]> {
    return this.formActions().selectOption(values, options)
  }
  setInputFiles(
    files: NativeInputFiles,
    options: Omit<NativeFormOptions, 'force'> = {},
  ): Promise<void> {
    return this.formActions().setInputFiles(files, options)
  }

  private queryDiagnosticExpression(): string {
    // User descriptions are labels; they are not executable selector aliases.
    const first = this.query[0]
    const query =
      this.query.length > 1 && first?.engine === 'css' && first.selector === ':scope'
        ? this.query.slice(1)
        : this.query
    const encoded = JSON.stringify(nativeQueryDescription(query, this.framePath))
    return encoded.length <= 16_384 && new TextEncoder().encode(encoded).byteLength <= 16_384
      ? encoded
      : JSON.stringify('')
  }

  /** Internal native capture: no author callback can manufacture references. */
  runReferenceSnapshot(
    options: { timeout?: number; boxes?: boolean; depth?: number; format?: 'json' } = {},
    signal?: AbortSignal,
  ): Promise<UntrustedInput> {
    return this.owner.runAutomation(
      'snapshotReferences',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        timeoutMs: options.timeout,
        accessibilityBoxes: options.boxes ?? false,
        ...(options.format === undefined ? {} : { referenceFormat: options.format }),
        ...(options.depth === undefined ? {} : { referenceDepth: options.depth }),
        ...(this.nativeHandle === undefined ? {} : { nativeRootHandle: this.nativeHandle }),
      },
      signal,
    )
  }
  /** Internal native fixed serializer; no caller-supplied observer function. */
  runStructuredSnapshot(options: NativeAriaSnapshotJSONOptions): Promise<UntrustedInput> {
    return this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'ariaSnapshotJSON',
        timeoutMs: options.timeout,
        accessibilityBoxes: options.boxes ?? false,
        ...(options.depth === undefined ? {} : { snapshotDepth: options.depth }),
      },
      options.signal,
    )
  }
  runExpectation(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput> {
    const soft = nativeSoftOperationOptions(params.timeoutMs, signal)
    return this.owner.runAutomation(
      'queryEvaluate',
      {
        ...params,
        ...(soft ? { timeoutMs: soft.timeout } : {}),
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        all: true,
        expectation: true,
        diagnosticExpressionJSON: this.queryDiagnosticExpression(),
      },
      soft?.signal ?? signal,
    )
  }
  frameLocator(selector: string): NativeFrameLocator {
    return new NativeFrameLocator(this.owner, [
      ...this.framePath,
      [...this.query, { engine: 'css', selector }],
    ])
  }
  contentFrame(): NativeFrameLocator {
    return new NativeFrameLocator(this.owner, [...this.framePath, this.query])
  }
  private appendQuery(step: NativeQueryStep): NativeLocator {
    return new NativeLocator(
      this.owner,
      `${this.selector} >> ${step.engine}`,
      [...this.query, step],
      this.framePath,
    )
  }
  getByRef(ref: string): NativeLocator {
    return this.appendQuery(nativeReferenceQuery(ref))
  }
  getByRole(role: string, options: NativeRoleOptions = {}): NativeLocator {
    return this.appendQuery(nativeRoleQuery(role, options))
  }
  getByText(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.appendQuery({ engine: 'text', ...textPattern(text, options) })
  }
  getByLabel(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.appendQuery({ engine: 'label', ...textPattern(text, options) })
  }
  getByPlaceholder(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.appendQuery({ engine: 'placeholder', ...textPattern(text, options) })
  }
  getByAltText(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.appendQuery({ engine: 'alt', ...textPattern(text, options) })
  }
  getByTitle(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.appendQuery({ engine: 'title', ...textPattern(text, options) })
  }
  getByTestId(text: string | RegExp): NativeLocator {
    return this.appendQuery(nativeTestIdQuery(text))
  }
  first(): NativeLocator {
    return this.nth(0)
  }
  last(): NativeLocator {
    return this.nth(-1)
  }
  nth(index: number): NativeLocator {
    z.number().int().parse(index)
    return new NativeLocator(
      this.owner,
      `${this.selector} >> nth=${index}`,
      [...this.query, { engine: 'nth', index }],
      this.framePath,
    )
  }
  private compatibleQuery(other: NativeLocator, kind: string): NativeQueryStep[] {
    const frame = (page: NativePage) =>
      page instanceof NativeFrame ? page.identity() : page.mainFrame().identity()
    const separateFrame =
      this.owner.source !== other.owner.source || frame(this.owner) !== frame(other.owner)
    const separatePath = JSON.stringify(this.framePath) !== JSON.stringify(other.framePath)
    if (separateFrame || separatePath) {
      const ordinary = `Inner "${kind}" locator must belong to the same frame.`
      if (kind === 'has' || kind === 'hasNot') throw new Error(ordinary)
      const capture = other.framePath.some((path) =>
        path.some((step) => step.engine === 'css' && /(?:^|>>)\s*\*/u.test(step.selector)),
      )
      const message = capture
        ? 'Can not capture the selector before diving into the frame'
        : separatePath
          ? `Frame locators are not allowed inside composite locators, while querying "${nativeQueryDescription(this.query, this.framePath)}.${kind}(${nativeQueryDescription(other.query, other.framePath)})"`
          : ordinary
      return [{ engine: 'invalid', message }]
    }
    if (other.nativeHandle !== undefined)
      throw new Error('A fixed native element handle cannot be used as a composite locator')
    return other.query
  }
  filter(options: NativeLocatorFilterOptions = {}): NativeLocator {
    const config = z
      .strictObject({
        hasText: z.union([z.string(), z.instanceof(RegExp)]).optional(),
        hasNotText: z.union([z.string(), z.instanceof(RegExp)]).optional(),
        has: z.instanceof(NativeLocator).optional(),
        hasNot: z.instanceof(NativeLocator).optional(),
        visible: z.boolean().optional(),
      })
      .parse(options)
    return this.appendQuery({
      engine: 'filter',
      ...(config.hasText !== undefined ? { hasText: textPattern(config.hasText) } : {}),
      ...(config.hasNotText !== undefined ? { hasNotText: textPattern(config.hasNotText) } : {}),
      ...(config.has !== undefined ? { has: this.compatibleQuery(config.has, 'has') } : {}),
      ...(config.hasNot !== undefined
        ? { hasNot: this.compatibleQuery(config.hasNot, 'hasNot') }
        : {}),
      ...(config.visible !== undefined ? { visible: config.visible } : {}),
    })
  }
  /** Resolves genuine visibility afresh whenever the resulting locator is used. */
  visible(): NativeLocator {
    return this.filter({ visible: true })
  }
  and(other: NativeLocator): NativeLocator {
    return this.appendQuery({ engine: 'and', query: this.compatibleQuery(other, 'and') })
  }
  or(other: NativeLocator): NativeLocator {
    return this.appendQuery({ engine: 'or', query: this.compatibleQuery(other, 'or') })
  }
  within(container: NativeLocator): NativeLocator {
    this.compatibleQuery(container, 'within')
    return container.locator(this)
  }
  locator(selector: string | NativeLocator, options?: NativeLocatorFilterOptions): NativeLocator {
    let locator: NativeLocator
    if (selector instanceof NativeLocator)
      locator = this.appendQuery({
        engine: 'chain',
        query: this.compatibleQuery(selector, 'locator'),
      })
    else {
      if (!selector.trim()) throw new Error('A native locator requires a selector')
      locator = this.appendQuery({ engine: 'css', selector })
    }
    return options ? locator.filter(options) : locator
  }
  private async queryHandle(
    all: boolean,
    wait: boolean,
    timeout = this.owner.defaultTimeout(),
  ): Promise<NativeJSHandle> {
    return createHandle(
      this.owner,
      await this.owner.runAutomation('handle', {
        action: 'create',
        query: this.query,
        framePath: this.framePath,
        all,
        strict: this.strict,
        predicate: wait,
        pollInterval: 20,
        timeoutMs: timeout,
      }),
    )
  }
  async elementHandle(options: { timeout?: number } = {}): Promise<NativeElementHandle | null> {
    const config = z
      .strictObject({ timeout: z.number().int().min(0).max(120000).optional() })
      .parse(options)
    const handle = await this.queryHandle(
      false,
      true,
      config.timeout ?? this.owner.defaultTimeout(),
    )
    const node = handle.asElement()
    if (!node) await handle.dispose()
    return node
  }
  async elementHandles(): Promise<NativeElementHandle[]> {
    const array = await this.queryHandle(true, false)
    return collectElementHandles(array)
  }
  async queryElementNow(): Promise<NativeElementHandle | null> {
    const handle = await this.queryHandle(false, false)
    const node = handle.asElement()
    if (!node) await handle.dispose()
    return node
  }
  async count(): Promise<number> {
    const result = EvaluationResult.parse(
      await this.owner.runAutomation('queryEvaluate', {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        count: true,
      }),
    )
    if (result.exceptionDetails)
      throw new Error(`Native locator count failed: ${evaluationError(result.exceptionDetails)}`)
    return z
      .number()
      .int()
      .nonnegative()
      .parse(createNativeValueCodec().decode(result.result?.value as UntrustedInput))
  }
  async waitFor(
    options: { state?: 'attached' | 'detached' | 'visible' | 'hidden'; timeout?: number } = {},
  ): Promise<void> {
    const config = z
      .strictObject({
        state: z.enum(['attached', 'detached', 'visible', 'hidden']).optional(),
        timeout: z.number().int().min(0).max(120000).optional(),
      })
      .parse(options)
    const state = config.state ?? 'visible'
    const fn = `function(nodes,state){if(${this.strict}&&nodes.length>1)throw new Error('Strict locator matched multiple elements');return {matches:${state === 'attached' ? '!!nodes[0]' : state === 'detached' ? '!nodes[0]' : state === 'visible' ? '!!state?.visible' : '!state?.visible'},actual:null};}`
    const result = EvaluationResult.parse(
      await this.runExpectation({
        function: fn,
        dom: true,
        strictDiagnostics: this.strict,
        visibility: state === 'visible' || state === 'hidden',
        timeoutMs: config.timeout ?? this.owner.defaultTimeout(),
      }),
    )
    if (result.exceptionDetails) throw new Error(evaluationError(result.exceptionDetails))
    if (!z.object({ matches: z.boolean() }).parse(result.result?.value).matches)
      throw new NativeTimeoutError(
        `locator.waitFor: Timeout ${config.timeout ?? this.owner.defaultTimeout()}ms exceeded, waiting for ${this.selector} to be ${state}`,
      )
  }
  async all(): Promise<NativeLocator[]> {
    return Array.from({ length: await this.count() }, (_, index) => this.nth(index))
  }
  allTextContents(): Promise<string[]> {
    return this.evaluateQuery(
      (elements: (SVGElement | HTMLElement)[]) =>
        elements.map((element) => element.textContent ?? ''),
      undefined,
      true,
      {},
    )
  }
  allInnerTexts(): Promise<string[]> {
    return this.evaluateQuery(
      (elements: (SVGElement | HTMLElement)[]) =>
        elements.map((element) =>
          element instanceof HTMLElement ? element.innerText : (element.textContent ?? ''),
        ),
      undefined,
      true,
      {},
    )
  }
  ariaSnapshot(options: NativeAriaSnapshotOptions = {}): Promise<string> {
    return captureNativeAriaSnapshot(this, options)
  }
  ariaSnapshotJSON(options: NativeAriaSnapshotJSONOptions = {}): Promise<NativeAriaJSONNode[]> {
    return captureNativeAriaSnapshotJSON(this, options)
  }
  /** Shows a genuine Chromium node-tool without modifying author DOM/CSS. */
  highlight(options: NativeHighlightOptions = {}): Promise<NativeHighlightDisposable> {
    return showNativeHighlight(
      (params) =>
        this.owner.runAutomation('highlight', {
          ...params,
          ...(params.action === 'show'
            ? {
                selector: this.selector,
                query: this.query,
                framePath: this.framePath,
                ...(this.nativeHandle === undefined ? {} : { nativeHandle: this.nativeHandle }),
              }
            : {}),
        }),
      async (token, emergency = false) => {
        const source = this.owner.source
        const timeoutMs = emergency ? 1000 : this.owner.defaultTimeout()
        await source.runAutomation(
          'highlight',
          { action: 'hide', token, timeoutMs },
          emergency ? AbortSignal.any([source.signal, AbortSignal.timeout(1000)]) : source.signal,
        )
      },
      options,
    )
  }
  hideHighlight(): Promise<void> {
    return hideNativeHighlight((params) =>
      this.owner.runAutomation('highlight', {
        ...params,
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
      }),
    )
  }
  /** Native-ranked recipes uniquely matching the exact selected current node. */
  async suggest(options: NativeLocatorSuggestionOptions = {}): Promise<NativeLocatorSuggestions> {
    const config = NativeLocatorSuggestionOptionsSchema.parse(options)
    config.signal?.throwIfAborted()
    const result = decodeNativeLocatorSuggestions(
      await this.owner.runAutomation(
        'suggestLocator',
        {
          selector: this.selector,
          query: this.query,
          framePath: this.framePath,
          ...(this.nativeHandle === undefined ? {} : { nativeHandle: this.nativeHandle }),
          timeoutMs: config.timeout ?? this.owner.defaultTimeout(),
        },
        config.signal,
      ),
    )
    return projectNativeLocatorSuggestions(this.owner, result)
  }
  /** Derives and validates a builtin selector for the exact connected Element. */
  async normalize(
    options: { timeout?: number; signal?: AbortSignal } = {},
  ): Promise<NativeLocator> {
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
      })
      .parse(options)
    if (this.nativeHandle !== undefined)
      throw new Error(
        'Native locator normalization requires a locator query, rather than a fixed handle',
      )
    const result = decodeNativeLocatorNormalization(
      await this.owner.runAutomation(
        'normalizeLocator',
        {
          selector: this.selector,
          query: this.query,
          framePath: this.framePath,
          timeoutMs: config.timeout ?? this.owner.defaultTimeout(),
        },
        config.signal,
      ),
    )
    const frame =
      this.owner instanceof NativeFrame && this.owner.identity() === result.provenance.frame
        ? this.owner
        : this.page()
            .frames()
            .find((candidate) => candidate.identity() === result.provenance.frame)
    if (!frame) throw new Error('Native normalized locator frame identity is no longer available')
    return new NativeLocator(
      frame,
      nativeQueryDescription(result.query),
      result.query,
      [],
      undefined,
      true,
      undefined,
      result.provenance,
    )
  }
  /** Creation provenance only; future resolution runs the actual builtin AST. */
  normalization(): NativeLocatorNormalization | null {
    return this.normalizationPins ?? null
  }

  page(): NativePage {
    return this.owner instanceof NativeFrame ? this.owner.page() : this.owner
  }
  describe(description: string): NativeLocator {
    return new NativeLocator(
      this.owner,
      this.selector,
      this.query,
      this.framePath,
      this.nativeHandle,
      this.strict,
      z.string().parse(description),
      this.normalizationPins,
    )
  }
  description(): string | null {
    return this.descriptionText ?? null
  }
  toString(): string {
    if (this.descriptionText !== undefined) return this.descriptionText
    const first = this.query[0]
    const query =
      this.query.length > 1 && first?.engine === 'css' && first.selector === ':scope'
        ? this.query.slice(1)
        : this.query
    return nativeQueryDescription(query, this.framePath)
  }

  evaluate<R, A>(
    fn: (element: SVGElement | HTMLElement, arg: A) => R | Promise<R>,
    arg?: A,
  ): Promise<R> {
    return this.evaluateQuery(fn, arg, false)
  }
  evaluateAll<R, A>(
    fn: (elements: (SVGElement | HTMLElement)[], arg: A) => R | Promise<R>,
    arg?: A,
  ): Promise<R> {
    return this.evaluateQuery(fn, arg, true)
  }
  async evaluateHandle<A>(
    fn: (element: SVGElement | HTMLElement, arg: A) => UntrustedInput | Promise<UntrustedInput>,
    arg?: A,
    options: { timeout?: number } = {},
  ): Promise<NativeJSHandle> {
    const config = z
      .strictObject({ timeout: z.number().int().min(0).max(120_000).optional() })
      .parse(options)
    const { encoded, handles } = evaluationArgument(this.owner, arg as UntrustedInput)
    const declaration = `async function(element,...handles){const codec=(${createNativeValueCodec.toString()})();const values=handles.map(handle=>handle.value);return await (${evaluationBody(fn)}).call(globalThis,element,codec.decode(${JSON.stringify(encoded)},values));}`
    return createHandle(
      this.owner,
      await this.owner.runAutomation('handle', {
        action: 'create',
        query: this.query,
        framePath: this.framePath,
        queryFunction: declaration,
        handles,
        timeoutMs: config.timeout,
      }),
    )
  }
  async waitForFunction<A, R>(
    fn:
      | ((element: SVGElement | HTMLElement, arg: NativePredicateArgument<A>) => R | Promise<R>)
      | string,
    arg?: A,
    options: NativeDOMReadOptions = {},
  ): Promise<void> {
    const config = DOMReadOptionsSchema.parse(options)
    const abort = () => {
      const error = new Error('locator.waitForFunction: The operation was aborted', {
        cause: config.signal?.reason as UntrustedInput,
      })
      error.name = 'AbortError'
      return error
    }
    if (config.signal?.aborted) throw abort()
    if (typeof fn !== 'function' && typeof fn !== 'string')
      throw new Error('waitForFunction: predicate must be a function or string')
    const body = evaluationBody(fn)
    // Function declarations use the existing scalar source transport. Reject
    // isolated source units explicitly; typed argument values remain lossless.
    if (/(?<![\uD800-\uDBFF])[\uDC00-\uDFFF]|[\uD800-\uDBFF](?![\uDC00-\uDFFF])/u.test(body))
      throw new Error('waitForFunction: isolated UTF16 predicate source is unsupported')
    const { encoded, handles } = evaluationArgument(this.owner, arg as UntrustedInput)
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=[];for(let i=0;i<handles.length;i++)values[i]=handles[i].value;const predicate=(${body});return !!await predicate(this,codec.decode(${JSON.stringify(encoded)},values));}`
    const controller = new AbortController()
    const onAbort = () => controller.abort(abort())
    config.signal?.addEventListener('abort', onAbort, { once: true })
    try {
      const result = z.object({ predicateErrorJSON: z.string().optional() }).parse(
        await this.owner.runAutomation(
          'locator',
          {
            selector: this.selector,
            query: this.query,
            framePath: this.framePath,
            action: 'waitForFunction',
            function: declaration,
            handles,
            strict: true,
            timeoutMs: config.timeout,
            diagnosticExpressionJSON: this.queryDiagnosticExpression(),
          },
          controller.signal,
        ),
      )
      if (result.predicateErrorJSON !== undefined)
        throw new Error(z.string().parse(JSON.parse(result.predicateErrorJSON)))
    } finally {
      config.signal?.removeEventListener('abort', onAbort)
    }
  }

  private async evaluateQuery<R, A>(
    fn:
      | ((element: SVGElement | HTMLElement, arg: A) => R | Promise<R>)
      | ((elements: (SVGElement | HTMLElement)[], arg: A) => R | Promise<R>),
    arg: A | undefined,
    all: boolean,
    nativeOptions?: NativeDOMReadOptions,
  ): Promise<R> {
    const { encoded, handles } = evaluationArgument(this.owner, arg as UntrustedInput)
    const body = evaluationBody(fn)
    const declaration = `async function(element,...handles){const codec=(${createNativeValueCodec.toString()})();const values=handles.map(handle=>handle.value);return codec.encode(await (${body}).call(globalThis,element,codec.decode(${JSON.stringify(encoded)},values)));}`
    const result = EvaluationResult.parse(
      await (nativeOptions === undefined
        ? this.owner.runAutomation('queryEvaluate', {
            selector: this.selector,
            query: this.query,
            framePath: this.framePath,
            function: declaration,
            handles,
            all,
          })
        : this.runDOMRead({ function: declaration, handles, all }, nativeOptions)),
    )
    if (result.exceptionDetails)
      throw new Error(
        `Native locator evaluation failed: ${evaluationError(result.exceptionDetails)}`,
      )
    return createNativeValueCodec().decode(result.result?.value as UntrustedInput) as R
  }

  private runDOMRead(
    params: Record<string, UntrustedInput>,
    options: NativeDOMReadOptions,
  ): Promise<UntrustedInput> {
    const config = DOMReadOptionsSchema.parse(options)
    return this.owner
      .runAutomation(
        'queryEvaluate',
        {
          selector: this.selector,
          query: this.query,
          framePath: this.framePath,
          ...params,
          dom: true,
          domGetter: true,
          strict: this.strict,
          timeoutMs: config.timeout,
          diagnosticExpressionJSON: this.queryDiagnosticExpression(),
          ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        },
        config.signal,
      )
      .catch((cause: UntrustedInput) => {
        if (cause instanceof NativeTimeoutError) {
          const description = `locator('${this.selector.replaceAll('\\', '\\\\').replaceAll("'", "\\'")}')`
          throw new NativeTimeoutError(
            `Timeout ${config.timeout ?? this.owner.defaultTimeout()}ms exceeded, waiting for ${description}`,
            { cause },
          )
        }
        throw cause
      })
  }

  private evaluateDOM<R, A>(
    fn: (element: SVGElement | HTMLElement, arg: A) => R | Promise<R>,
    arg: A | undefined,
    options: NativeDOMReadOptions,
  ): Promise<R> {
    return this.evaluateQuery(fn, arg, false, options)
  }

  private async observeElementState(options: NativeDOMReadOptions) {
    const result = EvaluationResult.parse(
      await this.runDOMRead(
        {
          elementState: true,
          function: 'function(element,state){return state;}',
        },
        options,
      ),
    )
    if (result.exceptionDetails) throw new Error(evaluationError(result.exceptionDetails))
    return z
      .object({ enabled: z.boolean(), editable: z.boolean().optional() })
      .parse(result.result?.value)
  }

  async boundingBox(options: NativeDOMReadOptions = {}): Promise<SuiteCutRect | null> {
    const config = DOMReadOptionsSchema.parse(options)
    return z
      .object({
        x: z.number().finite(),
        y: z.number().finite(),
        width: z.number().nonnegative().finite(),
        height: z.number().nonnegative().finite(),
      })
      .nullable()
      .parse(
        await this.owner.runAutomation(
          'locator',
          {
            selector: this.selector,
            query: this.query,
            framePath: this.framePath,
            action: 'boundingBox',
            strict: this.strict,
            timeoutMs: config.timeout,
            diagnosticExpressionJSON: this.queryDiagnosticExpression(),
            ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
          },
          config.signal,
        ),
      )
  }

  getAttribute(name: string, options: NativeDOMReadOptions = {}): Promise<string | null> {
    return this.evaluateDOM(
      (element, name) => element.getAttribute(name),
      z.string().parse(name),
      options,
    )
  }
  async isVisible(options: NativeDOMReadOptions = {}): Promise<boolean> {
    const result = EvaluationResult.parse(
      await this.runDOMRead(
        {
          immediate: true,
          visibility: true,
          visibilitySnapshot: true,
          function: 'function(element,state){return !!state?.visible;}',
        },
        options,
      ),
    )
    if (result.exceptionDetails) throw new Error(evaluationError(result.exceptionDetails))
    return z.boolean().parse(result.result?.value)
  }
  async isHidden(options: NativeDOMReadOptions = {}): Promise<boolean> {
    return !(await this.isVisible(options))
  }
  async isEnabled(options: NativeDOMReadOptions = {}): Promise<boolean> {
    return (await this.observeElementState(options)).enabled
  }
  async isDisabled(options: NativeDOMReadOptions = {}): Promise<boolean> {
    return !(await this.isEnabled(options))
  }
  isChecked(options: NativeDOMReadOptions = {}): Promise<boolean> {
    return this.evaluateDOM(
      (node) => {
        let element: Element = node
        if (element instanceof HTMLLabelElement && element.control) element = element.control
        if (element instanceof HTMLInputElement && ['checkbox', 'radio'].includes(element.type))
          return element.checked
        const role = element.getAttribute('role')
        if (
          role &&
          [
            'checkbox',
            'radio',
            'switch',
            'menuitemcheckbox',
            'menuitemradio',
            'option',
            'treeitem',
          ].includes(role)
        )
          return element.getAttribute('aria-checked') === 'true'
        throw new Error('Not a checkbox or radio button')
      },
      undefined,
      options,
    )
  }
  async isEditable(options: NativeDOMReadOptions = {}): Promise<boolean> {
    const state = await this.observeElementState(options)
    if (state.editable === undefined)
      throw new Error(
        'Element is not an <input>, <textarea>, <select> or [contenteditable] and does not have a role allowing [aria-readonly]',
      )
    return state.editable
  }

  textContent(options: NativeDOMReadOptions = {}): Promise<string | null> {
    return this.evaluateDOM((element) => element.textContent, undefined, options)
  }

  innerHTML(options: NativeDOMReadOptions = {}): Promise<string> {
    return this.evaluateDOM((element) => element.innerHTML, undefined, options)
  }

  innerText(options: NativeDOMReadOptions = {}): Promise<string> {
    return this.evaluateDOM(
      (element) => {
        if (!(element instanceof HTMLElement)) throw new Error('Node is not an HTMLElement')
        return element.innerText
      },
      undefined,
      options,
    )
  }

  async inputValue(options: NativeInputValueOptions = {}): Promise<string> {
    const config = InputValueOptionsSchema.parse(options)
    return z.string().parse(
      await this.owner.runAutomation(
        'locator',
        {
          selector: this.selector,
          query: this.query,
          framePath: this.framePath,
          action: 'inputValue',
          strict: this.strict,
          ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
          timeoutMs: config.timeout,
        },
        config.signal,
      ),
    )
  }

  async dispatchEvent(
    type: string,
    eventInit: UntrustedInput = {},
    options: NativeDispatchEventOptions = {},
  ): Promise<void> {
    const config = DispatchEventOptionsSchema.parse(options)
    z.string().min(1).max(256).parse(type)
    if (!eventInit || typeof eventInit !== 'object' || Array.isArray(eventInit))
      throw new Error('dispatchEvent: eventInit must be an object')
    const { encoded, handles } = evaluationArgument(this.owner, { type, eventInit })
    const eventInitJSON = JSON.stringify(encoded)
    if (new TextEncoder().encode(eventInitJSON).byteLength > 16_384)
      throw new Error('dispatchEvent: Native eventInit exceeds 16 KiB')
    if (handles.length > 32) throw new Error('dispatchEvent: Native eventInit exceeds 32 handles')
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'dispatchEvent',
        diagnosticExpressionJSON: this.queryDiagnosticExpression(),
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        eventInitJSON,
        handles,
        timeoutMs: config.timeout,
      },
      config.signal,
    )
  }

  async blur(options: NativeBlurOptions = {}): Promise<void> {
    const config = BlurOptionsSchema.parse(options)
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'blur',
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        timeoutMs: config.timeout,
      },
      config.signal,
    )
  }

  async selectText(options: NativeSelectTextOptions = {}): Promise<void> {
    const config = SelectTextOptionsSchema.parse(options)
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'selectText',
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        force: config.force,
        timeoutMs: config.timeout,
      },
      config.signal,
    )
  }

  async scrollIntoViewIfNeeded(options: NativeScrollOptions = {}): Promise<void> {
    const config = ScrollOptionsSchema.parse(options)
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'scrollIntoViewIfNeeded',
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        timeoutMs: config.timeout,
      },
      config.signal,
    )
  }

  async hover(options: NativeHoverOptions = {}): Promise<void> {
    return this.pointerAction('hover', HoverOptionsSchema.parse(options))
  }

  async click(options: NativeClickOptions = {}): Promise<void> {
    return this.pointerAction('click', ClickOptionsSchema.parse(options))
  }
  async tap(options: NativeTapOptions = {}): Promise<void> {
    return this.pointerAction('tap', TapOptionsSchema.parse(options))
  }
  private async pointerAction(
    action: 'click' | 'tap' | 'hover',
    config: z.output<typeof ClickOptionsSchema>,
  ): Promise<void> {
    const controller = new AbortController()
    const abort = (started: boolean) => {
      const reason = config.signal?.reason as UntrustedInput
      const message =
        reason instanceof Error
          ? reason.message
          : typeof reason === 'string'
            ? reason
            : 'The operation was aborted'
      const error = new Error(
        started
          ? `locator.${action}: ${message}\nCall log:\n - operation was aborted: ${message}`
          : 'The operation was aborted',
        { cause: reason },
      )
      error.name = 'AbortError'
      return error
    }
    if (config.signal?.aborted) throw abort(false)
    const onAbort = () => controller.abort(abort(true))
    config.signal?.addEventListener('abort', onAbort, { once: true })
    let hook: ReturnType<typeof subscribeNativeActionHook> | undefined
    try {
      if (config.beforeStable)
        hook = subscribeNativeActionHook(this.owner.source, config.beforeStable, controller)
      const params: Record<string, UntrustedInput> = { ...config }
      delete params.beforeStable
      if (hook) {
        params.pauseBeforeStable = true
        params.clientToken = hook.clientToken
      }
      delete params.signal
      delete params.timeout
      await this.owner.runAutomation(
        'locator',
        {
          ...params,
          selector: this.selector,
          query: this.query,
          framePath: this.framePath,
          action,
          diagnosticExpressionJSON: this.queryDiagnosticExpression(),
          strict: this.strict,
          ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
          timeoutMs: config.timeout,
        },
        controller.signal,
      )
    } finally {
      hook?.dispose()
      config.signal?.removeEventListener('abort', onAbort)
    }
  }
  dblclick(options: Omit<NativeClickOptions, 'clickCount'> = {}): Promise<void> {
    const config = ClickOptionsSchema.omit({ clickCount: true }).parse(options)
    return this.click({ ...config, clickCount: 2 } as NativeClickOptions)
  }

  async fill(value: string, options: NativeEditableOptions = {}): Promise<void> {
    if (typeof value !== 'string') throw new Error(`value: expected string, got ${typeof value}`)
    this.validateOptions(options)
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'fill',
        diagnosticExpressionJSON: this.queryDiagnosticExpression(),
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        value,
        timeoutMs: options.timeout,
      },
      options.signal,
    )
  }

  async clear(options: NativeEditableOptions = {}): Promise<void> {
    this.validateOptions(options)
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'clear',
        diagnosticExpressionJSON: this.queryDiagnosticExpression(),
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        timeoutMs: options.timeout,
      },
      options.signal,
    )
  }

  private validateOptions(options: NativeEditableOptions): void {
    for (const key of Object.keys(options))
      if (key !== 'timeout' && key !== 'signal')
        throw new Error(`Unsupported native action option: ${key}`)
    if (
      options.timeout !== undefined &&
      (!Number.isInteger(options.timeout) || options.timeout < 0 || options.timeout > 120_000)
    )
      throw new Error('Native action timeout must be an integer from 0 through 120000')
    if (options.signal !== undefined && !(options.signal instanceof AbortSignal))
      throw new Error('Native action signal must be an AbortSignal')
  }

  async focus(options: { timeout?: number } = {}): Promise<void> {
    const config = z
      .strictObject({ timeout: z.number().int().min(0).max(120_000).optional() })
      .parse(options)
    await this.owner.runAutomation('locator', {
      selector: this.selector,
      query: this.query,
      framePath: this.framePath,
      action: 'focus',
      diagnosticExpressionJSON: this.queryDiagnosticExpression(),
      strict: this.strict,
      ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
      timeoutMs: config.timeout,
    })
  }
  async press(key: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    const config = KeyboardTargetOptionsSchema.parse(options)
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'press',
        diagnosticExpressionJSON: this.queryDiagnosticExpression(),
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        key: z.string().min(1).max(128).parse(key),
        delay: config.delay ?? 0,
        timeoutMs: config.timeout,
      },
      config.signal,
    )
  }
  async type(value: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    const config = KeyboardTargetOptionsSchema.parse(options)
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'type',
        diagnosticExpressionJSON: this.queryDiagnosticExpression(),
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        value: z.string().parse(value),
        delay: config.delay ?? 0,
        timeoutMs: config.timeout,
      },
      config.signal,
    )
  }
  /** Standards send-keys intent; existing type and press semantics remain unchanged. */
  async sendKeys(value: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    const config = KeyboardTargetOptionsSchema.parse(options)
    await this.owner.runAutomation(
      'locator',
      {
        selector: this.selector,
        query: this.query,
        framePath: this.framePath,
        action: 'sendKeys',
        diagnosticExpressionJSON: this.queryDiagnosticExpression(),
        strict: this.strict,
        ...(this.nativeHandle !== undefined ? { nativeHandle: this.nativeHandle } : {}),
        value: z.string().parse(value),
        delay: config.delay ?? 0,
        timeoutMs: config.timeout,
      },
      config.signal,
    )
  }
  pressSequentially(value: string, options: NativeKeyboardTargetOptions = {}): Promise<void> {
    return this.type(value, options)
  }
}

/** A lazily resolved path through native iframe owners. */
export class NativeFrameLocator {
  constructor(
    private readonly ownerPage: NativePage,
    private readonly path: NativeQueryStep[][],
  ) {}
  locator(selector: string, options?: NativeLocatorFilterOptions): NativeLocator {
    const locator = new NativeLocator(
      this.ownerPage,
      selector,
      [{ engine: 'css', selector }],
      this.path,
    )
    return options ? locator.filter(options) : locator
  }
  frameLocator(selector: string): NativeFrameLocator {
    return new NativeFrameLocator(this.ownerPage, [...this.path, [{ engine: 'css', selector }]])
  }
  getByRef(ref: string): NativeLocator {
    return new NativeLocator(
      this.ownerPage,
      '<native snapshot reference>',
      [nativeReferenceQuery(ref)],
      this.path,
    )
  }
  getByRole(role: string, options?: NativeRoleOptions): NativeLocator {
    return this.locator(':scope').getByRole(role, options)
  }
  getByText(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByText(text, options)
  }
  getByLabel(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByLabel(text, options)
  }
  getByPlaceholder(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByPlaceholder(text, options)
  }
  getByAltText(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByAltText(text, options)
  }
  getByTitle(text: string | RegExp, options?: { exact?: boolean }): NativeLocator {
    return this.locator(':scope').getByTitle(text, options)
  }
  getByTestId(text: string | RegExp): NativeLocator {
    return this.locator(':scope').getByTestId(text)
  }
  first(): NativeFrameLocator {
    return this.nth(0)
  }
  last(): NativeFrameLocator {
    return this.nth(-1)
  }
  nth(index: number): NativeFrameLocator {
    z.number().int().parse(index)
    const last = this.path.at(-1)
    if (!last) throw new Error('Native frame locator requires an iframe owner')
    return new NativeFrameLocator(this.ownerPage, [
      ...this.path.slice(0, -1),
      [...last, { engine: 'nth', index }],
    ])
  }
  owner(): NativeLocator {
    const last = this.path.at(-1)
    if (!last) throw new Error('Native frame locator requires an iframe owner')
    return new NativeLocator(this.ownerPage, '<iframe owner>', last, this.path.slice(0, -1))
  }
}

/** Adds authored browser controls to an existing native source without owning it. */
export function createNativePage(source: NativeBrowser, signal?: AbortSignal): NativePage {
  if (source.state !== 'running') throw new Error('Native source is not running')
  return createNativeContextBoundPage(source, signal) ?? new NativePage(source, signal)
}

function projectNativeLocatorSuggestions(
  owner: NativePage,
  result: ReturnType<typeof decodeNativeLocatorSuggestions>,
): NativeLocatorSuggestions {
  const page = owner instanceof NativeFrame ? owner.page() : owner
  const frame =
    owner instanceof NativeFrame && owner.identity() === result.provenance.frame
      ? owner
      : page.frames().find((candidate) => candidate.identity() === result.provenance.frame)
  if (!frame) throw new Error('Native locator suggestion frame identity is no longer available')
  return Object.freeze({
    ...result,
    provenance: Object.freeze(result.provenance),
    diagnostics: Object.freeze(result.diagnostics.map((item) => Object.freeze(item))),
    suggestions: Object.freeze(
      result.suggestions.map((item) =>
        Object.freeze({
          ...item,
          locator: new NativeLocator(frame, item.code, structuredClone(item.query)),
        }),
      ),
    ),
  })
}
