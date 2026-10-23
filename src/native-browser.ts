/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase lifecycle module edges. */
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process'
import { mkdir, mkdtemp, realpath, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { isAbsolute, join, resolve } from 'node:path'

import * as z from 'zod'

import { raceWithAbort } from './abort.js'
import { CaptureFrameRateSchema } from './frame-rate.js'
import {
  nativeAssertionDeadlineError,
  nativeAssertionDeadlineKind,
  type NativeAssertionDeadlineKind,
} from './native-assertion-deadline.js'
import { NativeBindingEventSchema, type NativeBindingEvent } from './native-bindings.js'
import { type NativeBrowserCapabilities } from './native-browser-capabilities.js'
import { nativeBrowserOperationRequest } from './native-browser-operations.js'
import {
  nativeRetryInfrastructureFailure,
  nativeRetryIsCleanup,
  nativeRetryOperationSignal,
  observeNativeRetrySource,
  retainNativeRetryOperation,
  runNativeRetryOperation,
  withoutNativeRetryOperation,
  type NativeRetryInvocation,
} from './native-callback-operations.js'
import {
  acknowledgeNativeConsoleEvent,
  NativePageErrorInfoSchema,
  type NativePageErrorInfo,
} from './native-console.js'
import {
  NativeContextConsoleInfoSchema,
  type NativeContextConsoleInfo,
} from './native-context-console-protocol.js'
import {
  NativeContextNetworkInfoSchema,
  type NativeContextNetworkInfo,
} from './native-context-network-protocol.js'
import {
  NativeContextRouteInfoSchema,
  type NativeContextRouteInfo,
  NativeContextRouteMutationSchema,
  type NativeContextRouteMutation,
} from './native-context-route-protocol.js'
import { NativeDownloadInfoSchema, type NativeDownloadInfo } from './native-download.js'
import {
  NativeHTTPLookupEventSchema,
  type NativeHTTPLookupEvent,
} from './native-http-lookup-protocol.js'
import { NativeLocatorHandlerContexts } from './native-locator-handler-contexts.js'
import { prepareNativeNavigationClose } from './native-navigation-lifecycle.js'
import { NativeNetworkBodyReceiver, NativeNetworkBodyArchive } from './native-network-body.js'
import { NativePacketDecoder, NATIVE_PROTOCOL_VERSION } from './native-protocol.js'
import {
  NativeConsoleInfoSchema,
  NativeNetworkInfoSchema,
  NativeContextEventSchema,
  NativeDialogInfoSchema,
  NativeFileChooserInfoSchema,
  type NativeFileChooserInfo,
  type NativeDialogInfo,
  type NativeActionStage,
  NativeLocatorHandlerStageSchema,
  type NativeLocatorHandlerStage,
  type NativeURLPredicate,
  type NativeNetworkInfo,
  type NativeContextEvent,
  type NativeConsoleInfo,
  type NativeFrameInfo,
  type NativeAutomationEvent,
} from './native-protocol.js'
import { NativeRecordingInfoSchema, type NativeRecordingInfo } from './native-recording-protocol.js'
import {
  prepareNativeFixtureSource,
  type NativeSourceLaunchAdmission,
} from './native-runner-source-setup.js'
import { nativeScreenshotResult } from './native-screenshot-receipt.js'
import { NativeSourceWorkCallbacks } from './native-source-work.js'
import { NativeStreamingInfoSchema, type NativeStreamingInfo } from './native-streaming-protocol.js'
import { encodeNativeStringFields } from './native-string-wire.js'
import {
  nativeCommandError,
  nativeHTTPCommandError,
  NativeTimeoutError,
} from './native-timeouts.js'
import { type NativeVideoDescriptor } from './native-video-protocol.js'
import {
  NativeRecordVideoSchema,
  nativeVideoAbsolutePath,
  type NativeRecordVideoOptions,
} from './native-video.js'
import { assertNativeViewerAdmission, runNativeViewerRequest } from './native-viewer-gate.js'
import { type NativeWebSocketMessage } from './native-websocket-protocol.js'
import {
  NativeWebSocketRouteInfoSchema,
  type NativeWebSocketRouteInfo,
} from './native-websocket-route-protocol.js'
import {
  NativeWorkerEventSchema,
  type NativeWorkerEvent,
  type NativeWorkerInfo,
} from './native-worker-protocol.js'
import {
  NativeWorkerRetirements,
  type NativeWorkerRetirementSnapshot,
} from './native-worker-retirements.js'
import { type UntrustedInput } from './untrusted.js'

import type { NativeEventPredicateSnapshot } from './native-event-predicate.js'

// Packet payload validators are immutable and shared across all browser instances.
// Constructing them per worker message makes high-volume console traffic allocate
// an entire validation graph before it can validate the actual payload.
const ContextWorkerPayloadSchema = z.strictObject({
  contextId: z.number().int().positive(),
  workerEvent: NativeWorkerEventSchema,
})
const ContextWorkerPredicatePayloadSchema = z.strictObject({
  contextId: z.number().int().positive(),
  id: z.number().int().positive(),
  generation: z.number().int().positive(),
  token: z.number().int().positive(),
  sequence: z.number().int().positive(),
  clientToken: z.string().min(1).max(128),
  event: z.literal('console'),
  value: NativeConsoleInfoSchema,
})

const LaunchSchema = z
  .strictObject({
    executablePath: z
      .string()
      .refine(
        (value) => isAbsolute(value) && !value.includes('\0'),
        'must be an absolute executable path without null bytes',
      ),
    width: z.number().int().min(2).max(3840).default(1920),
    height: z.number().int().min(2).max(2160).default(1080),
    deviceScaleFactor: z.union([z.literal(1), z.literal(2)]).default(1),
    framesPerSecond: CaptureFrameRateSchema.default(60),
    pixelFormat: z.enum(['bgra', 'i420']).default('bgra'),
    frameTransport: z.enum(['always', 'on-demand']).default('always'),
    headless: z.boolean().default(false),
    backForwardCache: z.boolean().optional(),
    recordVideo: NativeRecordVideoSchema.optional(),
    startupTimeoutMs: z.number().int().min(100).max(120_000).default(30_000),
    userDataDir: z
      .string()
      .min(1)
      .max(4096)
      .refine(
        (value) => isAbsolute(value) && !value.includes('\0'),
        'must be an absolute profile directory without null bytes',
      )
      .optional(),
    downloadsPath: z
      .string()
      .min(1)
      .max(4096)
      .refine((value) => !value.includes('\0'))
      .optional(),
    tracesDir: z
      .string()
      .min(1)
      .max(4096)
      .refine((value) => !value.includes('\0'))
      .optional(),
    artifactsDir: z
      .string()
      .min(1)
      .max(4096)
      .refine((value) => !value.includes('\0'))
      .optional(),
    signal: z.instanceof(AbortSignal).optional(),
    onDiagnostic: z
      .custom<(message: string) => void>((value) => typeof value === 'function')
      .optional(),
  })
  .refine(
    (v) =>
      Math.round(v.width * v.deviceScaleFactor) <= 3840 &&
      Math.round(v.height * v.deviceScaleFactor) <= 2160,
    'physical frame size must not exceed 3840x2160',
  )

export type NativePixelFormat = 'bgra' | 'i420'
export type NativeBrowserState = 'running' | 'closing' | 'closed' | 'failed'

export interface NativeBrowserOptions {
  /** Absolute path to the separately built suitecut-browser executable. */
  executablePath: string
  width?: number
  height?: number
  deviceScaleFactor?: 1 | 2
  /** Requested CEF capture rate, any integer from 1 through 120; actual delivery can be lower. */
  framesPerSecond?: number
  /** BGRA8 for exact pixels; planar BT.709 limited-range I420 for lower-bandwidth streaming. */
  pixelFormat?: NativePixelFormat
  /** Raw delivery while onFrame has subscribers. The low-level launch defaults to always; launchBrowser defaults to on-demand. */
  frameTransport?: 'always' | 'on-demand'
  /** Linux display-free software rendering; leaves Chromium's sandbox enabled. */
  headless?: boolean
  /** Omit/true preserves Chromium's policy; false disables its BackForwardCache at native launch. */
  backForwardCache?: boolean
  /** Record every page in the launch/default context, including persistent profiles and popups. */
  recordVideo?: NativeRecordVideoOptions
  startupTimeoutMs?: number
  /** Real persistent CEF profile directory; retained after failure and shutdown. */
  userDataDir?: string
  /** Parent of private native download directories; retained after browser close. */
  downloadsPath?: string
  /** Default native download parent when downloadsPath is omitted. */
  artifactsDir?: string
  /** Native raw trace journals and resources, exported when each tracing chunk stops. */
  tracesDir?: string
  signal?: AbortSignal
  /** Optional native stderr observer for controlled diagnostic runs. */
  onDiagnostic?: (message: string) => void
}

export interface NativeVideoFrame {
  /** Owned packed BGRA8 or contiguous I420 Y/U/V planes (BT.709 limited range), top-left origin. */
  data: Buffer
  pixelFormat: NativePixelFormat
  width: number
  height: number
  /** CSS viewport proven by this paint; null while a resized viewport has no matching paint. */
  viewport?: { width: number; height: number } | null
  /** Actual fitted page pixels within the fixed capture canvas, excluding letterboxing. */
  contentRect?: { x: number; y: number; width: number; height: number }
  /** Capture time in this Node process's performance.now() clock. */
  timestampMs: number
  /** Cumulative frames replaced by the native writer under backpressure. */
  droppedFrames: number
}

export interface NativeAudioPacket {
  /** Owned interleaved signed 16-bit little-endian stereo PCM at 48 kHz. */
  data: Buffer
  frames: number
  /** Estimated first-sample time in this Node process's performance.now() clock. */
  timestampMs: number
}

export interface NativeBrowser {
  readonly pageId?: number
  /** Actual recording metadata from this page's native ready packet. */
  videoDescriptor?(): NativeVideoDescriptor | undefined
  /** Undefined means the executable predates native capability reporting. */
  backendCapabilities?(): NativeBrowserCapabilities | undefined
  readonly state: NativeBrowserState
  readonly url: string
  /** Aborts on failure or closure; supports detachable lifecycle observation. */
  readonly signal: AbortSignal
  /** Actual page/helper close lifetime, independent of a failed renderer epoch. */
  readonly pageSignal?: AbortSignal
  /** Process lifetime, independent of an individual page closing. */
  readonly processSignal?: AbortSignal
  /** Real helper process and stdio closure, independent of logical page/context abort. */
  readonly processClosed?: Promise<void>
  readonly pixelFormat: NativePixelFormat
  readonly width: number
  readonly height: number
  readonly viewport?: { width: number; height: number } | null | undefined
  readonly framesPerSecond: number
  readonly cefVersion: string
  /** Handshake mapping: Node performance.now() = native monotonic milliseconds + this offset. */
  readonly nativeClockOffsetMs?: number
  /** Actual helper termination, available after the process closes. */
  processExit?(): { code: number | null; signal: NodeJS.Signals | null } | undefined
  /** Rejects on runtime, renderer, audio, protocol, or subscriber failure. */
  readonly failure: Promise<never>
  /** Settles only after the native process and its stdio have closed. */
  readonly closed: Promise<void>
  /** Navigates an HTTP(S) page and waits for its main-frame load. */
  navigate(url: string): Promise<void>
  /** Executes a DevTools method through private process IPC, without opening a debugging port. */
  sendDevToolsCommand(
    method: string,
    params?: Record<string, UntrustedInput>,
  ): Promise<UntrustedInput>
  /** Runs an authored automation operation scheduled and bounded by the C++ engine. */
  runAutomation(
    command:
      | 'devtools'
      | 'evaluate'
      | 'locator'
      | 'locatorHandlerRegister'
      | 'locatorHandlerRemove'
      | 'locatorHandlerRaw'
      | 'locatorHandlerOperationsRetired'
      | 'locatorHandlerOperationBegin'
      | 'locatorHandlerOperationEnd'
      | 'cancel'
      | 'waitNavigation'
      | 'handle'
      | 'worker'
      | 'diagnostics'
      | 'viewerIdle'
      | 'waitTimeout'
      | 'waitEvent'
      | 'navigateFrame'
      | 'reload'
      | 'recoverPage'
      | 'historyBack'
      | 'historyForward'
      | 'waitFrameNavigation'
      | 'waitLoadState'
      | 'responseBody'
      | 'input'
      | 'queryEvaluate'
      | 'snapshotReferences'
      | 'normalizeLocator'
      | 'suggestLocator'
      | 'pickLocator'
      | 'highlight'
      | 'bringToFront'
      | 'viewport'
      | 'coverage'
      | 'debugger'
      | 'gc'
      | 'websocket'
      | 'har'
      | 'network'
      | 'networkBody'
      | 'routeCallback'
      | 'context'
      | 'artifact'
      | 'form'
      | 'recording'
      | 'streaming'
      | 'clock'
      | 'continueAction'
      | 'http'
      | 'selectors'
      | 'claimConsoleArguments'
      | 'acknowledgeConsole'
      | 'console'
      | 'dialog'
      | 'bindings'
      | 'urlPredicate'
      | 'continueURLPredicate'
      | 'waitEventPredicate'
      | 'continueEventPredicate',
    params?: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
  /** Subscribers must return promptly; buffers may be retained but must not be modified. */
  onFrame(listener: (frame: NativeVideoFrame) => void): () => void
  onAudio(listener: (packet: NativeAudioPacket) => void): () => void
  /** Current native frame metadata; optional for authored unit-test sources. */
  frameInfos?(): NativeFrameInfo[]
  workerInfos?(): NativeWorkerInfo[]
  workerRetirements?(): NativeWorkerRetirementSnapshot
  onWorkerMessage?(listener: (event: NativeWorkerEvent) => void): () => void
  onAutomationEvent?(listener: (event: NativeAutomationEvent) => void): () => void
  onPageErrorMessage?(listener: (message: NativePageErrorInfo) => void): () => void
  onConsoleMessage?(listener: (message: NativeConsoleInfo) => void): () => void
  onLocatorHandlerStage?(listener: (stage: NativeLocatorHandlerStage) => void): () => void
  onActionStage?(listener: (stage: NativeActionStage) => void): () => void
  onURLPredicate?(listener: (snapshot: NativeURLPredicate) => void): () => void
  onCrashMessage?(listener: () => void): () => void
  onEventPredicate?(listener: (snapshot: NativeEventPredicateSnapshot) => void): () => void
  runStreaming?(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
  onStreamingMessage?(listener: (info: NativeStreamingInfo) => void): () => void
  runRecording?(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
  onRecordingMessage?(listener: (info: NativeRecordingInfo) => void): () => void
  onDownloadMessage?(listener: (info: NativeDownloadInfo) => void): () => void
  onFileChooserMessage?(listener: (chooser: NativeFileChooserInfo) => void): () => void
  onDialogMessage?(listener: (dialog: NativeDialogInfo) => void): () => void
  onNetworkMessage?(listener: (event: NativeNetworkInfo) => void): () => void
  onWebSocketRouteMessage?(listener: (event: NativeWebSocketRouteInfo) => void): () => void
  onHTTPLookupMessage?(listener: (event: NativeHTTPLookupEvent) => void): () => void
  onBindingMessage?(listener: (event: NativeBindingEvent) => void): () => void
  onWebSocketMessage?(listener: (event: NativeWebSocketMessage) => void): () => void
  webSocketSequence?(): number
  networkSequence?(): number
  takeNetworkBody?(bodyId: string): Buffer
  takeContextNetworkBody?(context: number, owner: string, bodyId: string): Buffer
  contextSequence?(): number
  contextWorkerInfos?(contextId: number): NativeWorkerInfo[]
  contextWorkerRetirements?(contextId: number): NativeWorkerRetirementSnapshot
  contextWorkerSequence?(contextId: number): number
  onContextWorkerMessage?(
    listener: (record: { contextId: number; workerEvent: NativeWorkerEvent }) => void,
  ): () => void
  onContextWorkerPredicate?(
    listener: (record: NativeEventPredicateSnapshot & { contextId: number }) => void,
  ): () => void
  onContextConsole?(listener: (info: NativeContextConsoleInfo) => void): () => void
  contextConsoleSequence?(): number
  contextNetworkSequence?(): number
  onContextRoute?(listener: (info: NativeContextRouteInfo) => void): () => void
  onContextRouteMutation?(listener: (info: NativeContextRouteMutation) => void): () => void
  onContextNetwork?(listener: (info: NativeContextNetworkInfo) => void): () => void
  runContext?(
    action: string,
    params?: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
  pageSource?(pageId: number): NativeBrowser
  onContextEvent?(listener: (event: NativeContextEvent) => void): () => void
  eventSequence?(): number
  /** Idempotent; closes CEF gracefully, then kills the helper if shutdown stalls. */
  close(): Promise<void>
}

/** Launches an isolated CEF source with raw frame/audio capture and private command IPC. */
export async function launchNativeBrowser(options: NativeBrowserOptions): Promise<NativeBrowser> {
  const config = LaunchSchema.parse(options)
  if (config.pixelFormat === 'i420' && (config.width % 2 || config.height % 2))
    throw new Error('I420 sources require even CSS dimensions')
  config.signal?.throwIfAborted()
  const temporaryProfile = config.userDataDir === undefined
  const requestedProfile = config.userDataDir ?? (await mkdtemp(join(tmpdir(), 'suitecut-cef-')))
  if (!temporaryProfile) await mkdir(requestedProfile, { recursive: true })
  const profile = await realpath(requestedProfile)
  let sourceAdmission: NativeSourceLaunchAdmission | undefined
  let sourceWork: NativeSourceWorkCallbacks | undefined
  let unbindSourceWork: (() => void) | undefined
  try {
    sourceAdmission = await prepareNativeFixtureSource(config.executablePath)
    if (sourceAdmission && !sourceAdmission.launchAllowed()) {
      await sourceAdmission.cancel()
      sourceAdmission = undefined
    }
  } catch (error) {
    if (temporaryProfile) await rm(profile, { recursive: true, force: true })
    throw error
  }
  const launch = () =>
    spawn(
      config.executablePath,
      [
        ...(sourceAdmission === undefined
          ? []
          : [
              '--suitecut-runner-source-fd=7',
              `--suitecut-runner-source-ticket=${sourceAdmission.ticket}`,
              `--suitecut-runner-source-cookie=${sourceAdmission.cookie}`,
            ]),
        ...(config.recordVideo === undefined
          ? []
          : [
              `--suitecut-record-video=${JSON.stringify({ ...config.recordVideo, dir: nativeVideoAbsolutePath(config.recordVideo.dir) })}`,
              `--suitecut-startup-timeout-ms=${config.startupTimeoutMs}`,
            ]),
        `--suitecut-width=${config.width}`,
        `--suitecut-height=${config.height}`,
        `--suitecut-scale=${config.deviceScaleFactor}`,
        `--suitecut-fps=${config.framesPerSecond}`,
        `--suitecut-pixel-format=${config.pixelFormat}`,
        `--suitecut-frame-transport=${config.frameTransport}`,
        `--suitecut-headless=${config.headless ? 1 : 0}`,
        ...(config.backForwardCache === false ? ['--disable-features=BackForwardCache'] : []),
        `--suitecut-profile=${profile}`,
        ...(config.tracesDir === undefined
          ? []
          : [`--suitecut-traces-dir=${resolve(config.tracesDir)}`]),
        ...(temporaryProfile ? [] : ['--suitecut-persistent-profile=1']),
        ...((config.downloadsPath ?? config.artifactsDir) === undefined
          ? []
          : [
              `--suitecut-downloads-path=${resolve(config.downloadsPath ?? config.artifactsDir ?? profile)}`,
            ]),
      ],
      // Node's stdio tuple overload describes only three entries. These first
      // three remain literal pipes when the private inherited fd is appended.
      {
        shell: false,
        stdio:
          sourceAdmission === undefined
            ? ['pipe', 'pipe', 'pipe']
            : [
                'pipe',
                'pipe',
                'pipe',
                'ignore',
                'ignore',
                'ignore',
                'ignore',
                sourceAdmission.descriptor,
              ],
      },
    ) as ChildProcessWithoutNullStreams
  let child: ReturnType<typeof launch>
  try {
    child = launch()
  } catch (error) {
    await sourceAdmission?.cancel()
    if (temporaryProfile) await rm(profile, { recursive: true, force: true })
    throw error
  }
  const networkBodyArchive = new NativeNetworkBodyArchive()
  const contextNetworkBodies = new Map<
    string,
    { context: number; receiver: NativeNetworkBodyReceiver }
  >()
  const closedNetworkContexts = new Set<number>()
  const httpLookupMessages = new Set<(event: NativeHTTPLookupEvent) => void>()
  let contextNetworkSequence = 0
  const contextRouteEvents = new Set<(info: NativeContextRouteInfo) => void>()
  const webSocketRouteEvents = new Set<(info: NativeWebSocketRouteInfo) => void>()
  const contextRouteMutations = new Set<(info: NativeContextRouteMutation) => void>()
  const contextNetworkEvents = new Set<(info: NativeContextNetworkInfo) => void>()
  let contextConsoleSequence = 0
  const contextConsoleEvents = new Set<(info: NativeContextConsoleInfo) => void>()
  const workerRetirements = new NativeWorkerRetirements()
  const contextWorkerInfos = new Map<number, Map<string, NativeWorkerInfo>>()
  const contextWorkerSequences = new Map<number, number>()
  const contextWorkerMessages = new Set<
    (record: { contextId: number; workerEvent: NativeWorkerEvent }) => void
  >()
  const contextWorkerPredicates = new Set<
    (record: NativeEventPredicateSnapshot & { contextId: number }) => void
  >()
  let contextSequence = 0
  const contextEvents = new Set<(event: NativeContextEvent) => void>()
  const pages = new Map<
    number,
    {
      frames: Set<(frame: NativeVideoFrame) => void>
      frameSubscription: {
        enabled: boolean
        active: boolean
        epoch: number
        queued: boolean
        pending: { enabled: boolean; epoch: number } | undefined
      }
      audio: Set<(packet: NativeAudioPacket) => void>
      automationEvents: Set<(event: NativeAutomationEvent) => void>
      crashes: Set<() => void>
      pageErrors: Set<(message: NativePageErrorInfo) => void>
      consoleMessages: Set<(message: NativeConsoleInfo) => void>
      actionStages: Set<(stage: NativeActionStage) => void>
      urlPredicates: Set<(snapshot: NativeURLPredicate) => void>
      eventPredicates: Set<(snapshot: NativeEventPredicateSnapshot) => void>
      fileChoosers: Set<(chooser: NativeFileChooserInfo) => void>
      recordings: Set<(info: NativeRecordingInfo) => void>
      streaming: Set<(info: NativeStreamingInfo) => void>
      downloads: Set<(info: NativeDownloadInfo) => void>
      dialogs: Set<(dialog: NativeDialogInfo) => void>
      socketMessages: Set<(event: NativeWebSocketMessage) => void>
      socketSequence: number
      networkMessages: Set<(event: NativeNetworkInfo) => void>
      bindingMessages: Set<(event: NativeBindingEvent) => void>
      frameInfos: Map<string, NativeFrameInfo>
      workerInfos: Map<string, NativeWorkerInfo>
      workerMessages: Set<(event: NativeWorkerEvent) => void>
      lifecycle: AbortController
      closeLifecycle: AbortController
      renderSignal: AbortSignal
      rendererGeneration: number
      closed: Promise<void>
      resolveClosed: () => void
      failure: Promise<never>
      rejectFailure(error: Error): void
      state: NativeBrowserState
      ready: boolean
      video: NativeVideoDescriptor | undefined
      captureWidth: number
      captureHeight: number
      viewport: { width: number; height: number } | null | undefined
      readyPromise: Promise<void>
      resolveReady: () => void
      eventSequence: number
      networkSequence: number
      networkBodies: NativeNetworkBodyReceiver
      url: string
      lastFrame: NativeVideoFrame | undefined
      source: NativeBrowser | undefined
    }
  >()
  const closedPageSources = new Map<number, WeakRef<NativeBrowser>>()
  let highestPageId = 1
  const pageState = (id: number) => {
    const existing = pages.get(id)
    if (existing) return existing
    if (pages.size >= 128) throw new Error('Native page registry capacity exceeded')
    let resolvePageClosed: () => void = () => undefined
    let resolvePageReady: () => void = () => undefined
    let rejectPageFailure: (error: Error) => void = () => undefined
    const pageFailure = new Promise<never>((_, reject) => {
      rejectPageFailure = reject
    })
    void pageFailure.catch(() => undefined)
    const render = new AbortController()
    const state = {
      frames: new Set<(frame: NativeVideoFrame) => void>(),
      frameSubscription: {
        enabled: false,
        active: false,
        epoch: 0,
        queued: false,
        pending: undefined as { enabled: boolean; epoch: number } | undefined,
      },
      audio: new Set<(packet: NativeAudioPacket) => void>(),
      automationEvents: new Set<(event: NativeAutomationEvent) => void>(),
      crashes: new Set<() => void>(),
      pageErrors: new Set<(message: NativePageErrorInfo) => void>(),
      consoleMessages: new Set<(message: NativeConsoleInfo) => void>(),
      actionStages: new Set<(stage: NativeActionStage) => void>(),
      urlPredicates: new Set<(snapshot: NativeURLPredicate) => void>(),
      eventPredicates: new Set<(snapshot: NativeEventPredicateSnapshot) => void>(),
      fileChoosers: new Set<(chooser: NativeFileChooserInfo) => void>(),
      recordings: new Set<(info: NativeRecordingInfo) => void>(),
      streaming: new Set<(info: NativeStreamingInfo) => void>(),
      downloads: new Set<(info: NativeDownloadInfo) => void>(),
      dialogs: new Set<(dialog: NativeDialogInfo) => void>(),
      socketMessages: new Set<(event: NativeWebSocketMessage) => void>(),
      socketSequence: 0,
      networkMessages: new Set<(event: NativeNetworkInfo) => void>(),
      bindingMessages: new Set<(event: NativeBindingEvent) => void>(),
      frameInfos: new Map<string, NativeFrameInfo>(),
      workerInfos: new Map<string, NativeWorkerInfo>(),
      workerMessages: new Set<(event: NativeWorkerEvent) => void>(),
      lifecycle: render,
      closeLifecycle: new AbortController(),
      renderSignal: AbortSignal.any([lifecycle.signal, render.signal]),
      rendererGeneration: 0,
      closed: new Promise<void>((resolve) => {
        resolvePageClosed = resolve
      }),
      resolveClosed: () => resolvePageClosed(),
      failure: pageFailure,
      rejectFailure: (error: Error) => rejectPageFailure(error),
      state: 'running' as NativeBrowserState,
      ready: false,
      video: undefined as NativeVideoDescriptor | undefined,
      captureWidth: Math.ceil(config.width * config.deviceScaleFactor),
      captureHeight: Math.ceil(config.height * config.deviceScaleFactor),
      viewport: undefined as { width: number; height: number } | null | undefined,
      readyPromise: new Promise<void>((resolve) => {
        resolvePageReady = resolve
      }),
      resolveReady: () => resolvePageReady(),
      eventSequence: 0,
      networkSequence: 0,
      networkBodies: new NativeNetworkBodyReceiver(
        async (bodyId, stage) => {
          await request('networkBody', { bodyId, stage }, undefined, id)
        },
        (error) => fail(error),
        networkBodyArchive,
      ),
      url: 'about:blank',
      lastFrame: undefined as NativeVideoFrame | undefined,
      source: undefined as NativeBrowser | undefined,
    }
    highestPageId = Math.max(highestPageId, id)
    pages.set(id, state)
    return state
  }
  const finishPage = (id: number, error: Error): void => {
    const page = pages.get(id)
    if (!page || page.state === 'closed') return
    page.state = 'closed'
    page.closeLifecycle.abort(error)
    page.lifecycle.abort(error)
    page.frames.clear()
    page.frameSubscription.active = false
    ++page.frameSubscription.epoch
    page.audio.clear()
    page.crashes.clear()
    page.automationEvents.clear()
    page.pageErrors.clear()
    page.consoleMessages.clear()
    page.socketMessages.clear()
    page.networkMessages.clear()
    page.networkBodies.close()
    page.bindingMessages.clear()
    page.actionStages.clear()
    page.urlPredicates.clear()
    page.eventPredicates.clear()
    page.dialogs.clear()
    page.fileChoosers.clear()
    page.recordings.clear()
    page.streaming.clear()
    page.downloads.clear()
    page.frameInfos.clear()
    page.workerInfos.clear()
    page.workerMessages.clear()
    page.lastFrame = undefined
    page.resolveClosed()
    if (id !== 1) {
      if (page.source) closedPageSources.set(id, new WeakRef(page.source))
      while (closedPageSources.size > 1024) {
        const oldest = closedPageSources.keys().next().value
        if (oldest === undefined) break
        closedPageSources.delete(oldest)
      }
      pages.delete(id)
    }
    for (const [requestId, item] of pending)
      if (item.pageId === id) {
        if (item.screenshotInputToken && !exited) {
          item.screenshotAbortReason ??= error
          continue
        }
        pending.delete(requestId)
        item.cleanup()
        item.reject(error)
      }
    queueMicrotask(pumpControls)
  }
  const nativeLocatorHandlerControl = (command: string): boolean =>
    command === 'locatorHandlerRaw' ||
    command === 'locatorHandlerOperationsRetired' ||
    command === 'locatorHandlerOperationBegin' ||
    command === 'locatorHandlerOperationEnd' ||
    command === 'locatorHandlerRemove'
  const nativeOwnerCommand = (command: string, fields: Record<string, UntrustedInput>): boolean =>
    nativeLocatorHandlerControl(command) ||
    command === 'diagnostics' ||
    command === 'frameSubscription' ||
    command === 'nativeWorkBarrier' ||
    command === 'recoverPage' ||
    command === 'cancel' ||
    command === 'download' ||
    command === 'networkBody' ||
    (command === 'debugger' && fields.action === 'close') ||
    (command === 'handle' && (fields.action === 'dispose' || fields.action === 'disposeMany')) ||
    (command === 'highlight' && (fields.action === 'hide' || fields.action === 'claim')) ||
    command === 'continueEventPredicate' ||
    ((command === 'waitEventPredicate' || command === 'waitEvent') && fields.event === 'crash') ||
    ((command === 'streaming' || command === 'recording') &&
      typeof fields.action === 'string' &&
      ['stop', 'cancel', 'status'].includes(fields.action))
  const nativeDebuggerControl = (fields: Record<string, UntrustedInput>): boolean =>
    fields.action === 'open' ||
    fields.action === 'close' ||
    fields.action === 'resume' ||
    fields.action === 'stepOver' ||
    fields.action === 'stepInto' ||
    fields.action === 'stepOut'
  const failPage = (id: number, error: Error): void => {
    const page = pages.get(id)
    if (!page || page.state === 'closed' || page.state === 'closing') return
    // A renderer failure is logical: the real CEF page/context stays owned until
    // its actual native close callback and publisher/process settlement.
    page.state = 'failed'
    page.ready = false
    page.lastFrame = undefined
    page.rejectFailure(error)
    page.lifecycle.abort(error)
    page.frames.clear()
    page.frameSubscription.active = false
    ++page.frameSubscription.epoch
    scheduleFrameSubscription(id)
    page.audio.clear()
    for (const [requestId, item] of pending)
      if (
        item.pageId === id &&
        !(
          (item.command === 'waitEventPredicate' || item.command === 'waitEvent') &&
          item.event === 'crash'
        ) &&
        item.command !== 'continueEventPredicate'
      ) {
        if (item.screenshotInputToken) {
          item.screenshotAbortReason ??= error
          continue
        }
        pending.delete(requestId)
        item.cleanup()
        item.reject(error)
      }
    for (const job of [...controlQueue])
      if (
        job.pageId === id &&
        job.command !== 'context' &&
        !nativeOwnerCommand(job.command, job.fields) &&
        job.command !== 'http' &&
        job.command !== 'selectors' &&
        job.command !== 'routeCallback'
      ) {
        removeControl(job)
        job.reject(error)
      }
    queueMicrotask(pumpControls)
    // User exceptions stay visible after failure cleanup and must never become
    // parser errors or prevent another page from remaining usable.
    for (const listener of [...page.crashes]) {
      try {
        listener()
      } catch (reason) {
        const thrown =
          reason instanceof Error
            ? reason
            : new Error('Native crash listener threw a non-Error value', { cause: reason })
        queueMicrotask(() => {
          throw thrown
        })
      }
    }
  }
  const pending = new Map<
    number,
    {
      resolve: (value: UntrustedInput) => void
      reject: (error: Error) => void
      cleanup: () => void
      command: string
      assertionDeadlineKind: NativeAssertionDeadlineKind | undefined
      screenshotInputToken: string | undefined
      screenshotAbortReason: Error | undefined
      event: string | undefined
      pageId: number
    }
  >()
  let locatorHandlerContexts: NativeLocatorHandlerContexts | undefined
  let nextId = 0
  let closing: Promise<void> | undefined
  let stopping = false
  let exited = false
  let exitStatus: { code: number | null; signal: NodeJS.Signals | null } | undefined
  let shutdownError: Error | undefined
  let terminal: Error | undefined
  let clockOffset: number | undefined
  let cefVersion = ''
  let backendCapabilities: NativeBrowserCapabilities | undefined
  const lifecycle = new AbortController()
  lifecycle.signal.addEventListener(
    'abort',
    () => {
      for (const { receiver } of contextNetworkBodies.values()) receiver.close()
      contextNetworkBodies.clear()
      closedNetworkContexts.clear()
    },
    { once: true },
  )
  let rejectFailure: (error: Error) => void = () => undefined
  let resolveReady: () => void = () => undefined
  let resolveClosed: () => void = () => undefined
  const failure = new Promise<never>((_, reject) => {
    rejectFailure = reject
  })
  void failure.catch(() => undefined)
  const ready = new Promise<void>((resolve) => {
    resolveReady = resolve
  })
  const closed = new Promise<void>((resolve) => {
    resolveClosed = resolve
  })
  const retrySource = {},
    retryFailures = new Set<(error: Error) => void>()
  const captureRetrySource = (): NativeRetryInvocation | undefined =>
    observeNativeRetrySource(retrySource, sourceAdmission?.ticket, (failed) => {
      retryFailures.add(failed)
      if (terminal) failed(terminal)
      return () => {
        retryFailures.delete(failed)
      }
    })
  const fail = (error: Error): void => {
    if (terminal) return
    terminal = error
    for (const failed of retryFailures) failed(error)
    decoder.dispose()
    lifecycle.abort(error)
    rejectFailure(error)
    for (const page of pages.values()) page.rejectFailure(error)
    for (const [id, request] of pending) {
      if (request.screenshotInputToken && !exited) {
        request.screenshotAbortReason ??= error
        continue
      }
      request.cleanup()
      request.reject(request.screenshotAbortReason ?? error)
      pending.delete(id)
    }
    queueMicrotask(pumpControls)
    if (!exited) child.kill('SIGKILL')
  }
  const decoder = new NativePacketDecoder((header, payload) => {
    if (terminal) return
    if (header.type === 'workwatermark') {
      if (!sourceWork)
        throw new Error('Original stdout watermark has no actual registered Source binding')
      sourceWork.observe(header)
      return
    }
    const pageId = header.pageId ?? 1
    if (header.type === 'contextworker') {
      const info = ContextWorkerPayloadSchema.parse(JSON.parse(payload.toString('utf8')))
      let workers = contextWorkerInfos.get(info.contextId)
      if (!workers) {
        workers = new Map()
        contextWorkerInfos.set(info.contextId, workers)
      }
      const event = info.workerEvent
      if (!workerRetirements.observe(`context:${info.contextId}`, event)) return
      contextWorkerSequences.set(info.contextId, event.sequence)
      if (event.event === 'close' || event.event === 'channelclosed')
        workers.delete(event.worker.id)
      else if (event.event === 'worker' || workers.has(event.worker.id))
        workers.set(event.worker.id, event.worker)
      for (const listener of [...contextWorkerMessages]) listener(info)
      return
    }
    if (header.type === 'contextworkerpredicate') {
      const info = ContextWorkerPredicatePayloadSchema.parse(JSON.parse(payload.toString('utf8')))
      contextWorkerSequences.set(
        info.contextId,
        Math.max(contextWorkerSequences.get(info.contextId) ?? 0, info.sequence),
      )
      for (const listener of [...contextWorkerPredicates]) listener(info)
      return
    }
    if (header.type === 'contextnetworkbody') {
      if (closedNetworkContexts.has(header.contextId)) return
      let state = contextNetworkBodies.get(header.owner)
      if (state && state.context !== header.contextId)
        throw new Error('Native worker body belongs to a different context')
      if (!state) {
        if (contextNetworkBodies.size >= 192)
          throw new Error('Native worker body owner capacity exceeded')
        const context = header.contextId,
          owner = header.owner
        state = {
          context,
          receiver: new NativeNetworkBodyReceiver(
            async (bodyId, stage) => {
              await request(
                'context',
                {
                  action: 'workerNetworkBody',
                  params: { contextId: context, owner, bodyId, stage },
                },
                undefined,
                1,
              )
            },
            (error) => fail(error),
            networkBodyArchive,
          ),
        }
        contextNetworkBodies.set(owner, state)
      }
      state.receiver.receive(
        {
          bodyId: header.bodyId,
          offset: header.offset,
          totalBytes: header.totalBytes,
          ...(header.canceled === undefined ? {} : { canceled: header.canceled }),
        },
        payload,
      )
      return
    }
    if (header.type === 'httplookup') {
      const event = NativeHTTPLookupEventSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...httpLookupMessages]) listener(event)
      return
    }
    if (header.type === 'websocketroute') {
      const info = NativeWebSocketRouteInfoSchema.parse({ ...header, payload })
      for (const listener of [...webSocketRouteEvents]) listener(info)
      return
    }
    if (header.type === 'contextroutemutation') {
      const info = NativeContextRouteMutationSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...contextRouteMutations]) listener(info)
      return
    }
    if (header.type === 'contextroute') {
      const info = NativeContextRouteInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...contextRouteEvents]) listener(info)
      return
    }
    if (header.type === 'contextnetwork') {
      const info = NativeContextNetworkInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      contextNetworkSequence = info.sequence
      if ('request' in info.network && info.network.request.postDataBodyId)
        if ('pageId' in info)
          pages.get(info.pageId)?.networkBodies.consume(info.network.request.postDataBodyId)
        else if (info.network.request.postDataOwner) {
          const actual = info.network.request.postDataOwner
          if (actual.startsWith('page:'))
            pages
              .get(Number(actual.slice(5)))
              ?.networkBodies.consume(info.network.request.postDataBodyId)
          else
            contextNetworkBodies.get(actual)?.receiver.consume(info.network.request.postDataBodyId)
        }
      for (const listener of [...contextNetworkEvents]) listener(info)
      return
    }
    if (header.type === 'contextconsole') {
      const info = NativeContextConsoleInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      if (info.pageId === null)
        contextWorkerSequences.set(
          info.contextId,
          Math.max(contextWorkerSequences.get(info.contextId) ?? 0, info.console.sequence ?? 0),
        )
      contextConsoleSequence = info.sequence
      for (const listener of [...contextConsoleEvents]) listener(info)
      return
    }
    if (header.type === 'context') {
      const event = NativeContextEventSchema.parse(JSON.parse(payload.toString('utf8')))
      contextSequence = event.sequence ?? contextSequence + 1
      if (event.event === 'context-closed') {
        contextWorkerInfos.delete(event.contextId)
        contextWorkerSequences.delete(event.contextId)
      }
      if (event.event === 'page-created' && event.pageId !== undefined) pageState(event.pageId)
      if (event.event === 'context-closed') {
        if (closedNetworkContexts.size >= 65536 && !closedNetworkContexts.has(event.contextId))
          throw new Error('Native worker network closed-context capacity exceeded')
        closedNetworkContexts.add(event.contextId)
        for (const [owner, state] of contextNetworkBodies)
          if (state.context === event.contextId) {
            state.receiver.close()
            contextNetworkBodies.delete(owner)
          }
      }
      if (event.event === 'page-closed' && event.pageId !== undefined)
        finishPage(event.pageId, new Error(event.closedReason ?? 'Native page closed'))
      for (const listener of [...contextEvents]) listener(event)
      return
    }
    if (header.type === 'response') {
      locatorHandlerContexts?.response(header.id)
      const request = pending.get(header.id)
      if (!request) return // A timed-out command may complete later.
      if (header.ok && header.navigation && request.pageId > 0) {
        const target = pages.get(request.pageId)
        if (target) {
          target.frameInfos.set(header.navigation.id, header.navigation)
          if (!header.navigation.parentId) target.url = header.navigation.url
        }
      }
      const result =
        header.ok && payload.length
          ? (JSON.parse(payload.toString('utf8')) as UntrustedInput)
          : undefined
      if (header.ok && request.screenshotInputToken && !request.screenshotAbortReason)
        nativeScreenshotResult(result, { id: header.id, inputToken: request.screenshotInputToken })
      const message = header.error ?? 'Native command failed'
      const failure =
        request.screenshotAbortReason ??
        (header.ok
          ? undefined
          : request.command === 'http' && payload.length
            ? nativeHTTPCommandError(
                message,
                JSON.parse(payload.toString('utf8')) as UntrustedInput,
              )
            : request.assertionDeadlineKind &&
                payload.length &&
                (!request.screenshotInputToken ||
                  (JSON.parse(payload.toString('utf8')) as { type?: UntrustedInput }).type ===
                    'native-assertion-deadline')
              ? nativeAssertionDeadlineError(
                  message,
                  JSON.parse(payload.toString('utf8')) as UntrustedInput,
                  { id: header.id, matcher: request.assertionDeadlineKind },
                )
              : nativeCommandError(message))
      pending.delete(header.id)
      request.cleanup()
      if (request.command === 'frameSubscription') {
        const target = pages.get(request.pageId)
        const subscription = target?.frameSubscription
        if (target && subscription?.pending)
          subscription.active =
            !failure &&
            subscription.pending.enabled &&
            subscription.pending.epoch === subscription.epoch &&
            target.frames.size > 0 &&
            target.state === 'running'
      }
      if (failure) request.reject(failure)
      else request.resolve(result)
      return
    }
    if (header.type === 'actionstage' && header.stage.startsWith('locatorHandler')) {
      if (!locatorHandlerContexts)
        throw new Error('Native locator handler event arrived without registration')
      locatorHandlerContexts.receive(
        pageId,
        NativeLocatorHandlerStageSchema.parse({
          id: header.id,
          generation: header.generation,
          token: header.token,
          clientToken: header.clientToken,
          stage: header.stage,
          handlerId: header.handlerId,
          ...(header.removed === undefined ? {} : { removed: header.removed }),
        }),
      )
      return
    }
    if (pageId !== 1 && !pages.has(pageId) && pageId <= highestPageId) return
    const page = pageState(pageId)
    if (page.state === 'closed' && header.type !== 'error') return
    if (header.type === 'pagecrash') {
      if (header.sequence !== undefined)
        page.eventSequence = Math.max(page.eventSequence, header.sequence)
      failPage(pageId, new Error(`Native page: ${header.error}`))
      return
    }
    if (header.type === 'pagerecovered') {
      if (page.state === 'closing' || page.state === 'closed') return
      if (page.state !== 'failed' || header.rendererGeneration <= page.rendererGeneration)
        throw new Error('Unexpected native renderer recovery')
      page.rendererGeneration = header.rendererGeneration
      page.lifecycle = new AbortController()
      page.renderSignal = AbortSignal.any([lifecycle.signal, page.lifecycle.signal])
      page.failure = new Promise<never>((_, reject) => {
        page.rejectFailure = reject
      })
      void page.failure.catch(() => undefined)
      page.state = 'running'
      page.ready = true
      // A pre-crash command may have reached native after its JS waiter was
      // rejected. Reconcile unknown native delivery to the current listeners.
      page.frameSubscription.enabled = true
      page.url = header.mainFrame.url
      page.frameInfos.set(header.mainFrame.id, header.mainFrame)
      scheduleFrameSubscription(pageId)
      return
    }
    if (header.type === 'error') {
      fail(new Error(`Native browser: ${header.error}`))
      return
    }
    if (header.type === 'networkbody') {
      page.networkBodies.receive(
        {
          bodyId: header.bodyId,
          offset: header.offset,
          totalBytes: header.totalBytes,
          ...(header.canceled !== undefined ? { canceled: header.canceled } : {}),
        },
        payload,
      )
      return
    }
    if (header.type === 'ready') {
      if (pageId === 1 && config.recordVideo && !header.video)
        throw new Error(
          'Native executable did not acknowledge launch recording; rebuild suitecut-browser with launch recordVideo support',
        )
      if (header.video) page.video = header.video
      if (page.ready) throw new Error('Duplicate native page handshake')
      page.captureWidth = header.captureWidth ?? page.captureWidth
      page.captureHeight = header.captureHeight ?? page.captureHeight
      page.viewport = header.viewport
      page.ready = true
      page.resolveReady()
      clockOffset ??= performance.now() - header.timestampMs
      cefVersion = header.cefVersion
      if (pageId === 1 && header.capabilities)
        backendCapabilities = Object.freeze({ ...header.capabilities })
      scheduleFrameSubscription(pageId)
      if (header.mainFrame) {
        page.frameInfos.set(header.mainFrame.id, header.mainFrame)
        page.url = header.mainFrame.url
      }
      if (pageId === 1) resolveReady()
      return
    }
    if (clockOffset === undefined) throw new Error('Native packet before handshake')
    if (header.type === 'eventpredicate') {
      const value = z.unknown().parse(JSON.parse(payload.toString('utf8'))) as UntrustedInput
      for (const listener of [...page.eventPredicates])
        listener({
          id: header.id,
          generation: header.generation,
          token: header.token,
          clientToken: header.clientToken,
          event: header.event,
          sequence: header.sequence,
          value,
        })
      return
    }
    if (header.type === 'urlpredicate') {
      for (const listener of [...page.urlPredicates])
        listener({
          id: header.id,
          generation: header.generation,
          token: header.token,
          clientToken: header.clientToken,
          url: header.url,
        })
      return
    }
    if (header.type === 'streaming') {
      const info = NativeStreamingInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...page.streaming]) listener(info)
      return
    }
    if (header.type === 'recording') {
      const info = NativeRecordingInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...page.recordings]) listener(info)
      return
    }
    if (header.type === 'download') {
      page.eventSequence = Math.max(page.eventSequence, header.sequence)
      const info = NativeDownloadInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...page.downloads]) listener(info)
      return
    }
    if (header.type === 'filechooser') {
      page.eventSequence = header.sequence
      const chooser = NativeFileChooserInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...page.fileChoosers]) listener(chooser)
      return
    }
    if (header.type === 'dialog') {
      page.eventSequence = header.sequence
      const dialog = NativeDialogInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...page.dialogs]) listener(dialog)
      return
    }
    if (header.type === 'actionstage') {
      const stage = header.stage
      if (stage !== 'beforeStable' && stage !== 'beforeScreenshot')
        throw new Error('Native locator handler stage bypassed its actual receiver')
      for (const listener of [...page.actionStages])
        listener({
          id: header.id,
          generation: header.generation,
          token: header.token,
          stage,
          clientToken: header.clientToken,
        })
      return
    }
    if (header.type === 'binding') {
      const event = NativeBindingEventSchema.parse(JSON.parse(payload.toString('utf8')))
      if (event.event === 'call') page.frameInfos.set(event.frame.id, event.frame)
      for (const listener of [...page.bindingMessages]) listener(event)
      return
    }
    if (header.type === 'websocket') {
      page.socketSequence = header.socket.sequence
      if (
        (header.socket.event === 'framesent' || header.socket.event === 'framereceived') &&
        header.socket.opcode === undefined
      )
        throw new Error('Native socket frame has no opcode')
      if (
        header.socket.event !== 'framesent' &&
        header.socket.event !== 'framereceived' &&
        payload.length
      )
        throw new Error('Native socket metadata carries unexpected payload')
      const message = { info: header.socket, payload }
      for (const listener of [...page.socketMessages]) listener(message)
      return
    }
    if (header.type === 'network') {
      const event = NativeNetworkInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      if ('request' in event && event.request.postDataBodyId)
        page.networkBodies.consume(event.request.postDataBodyId)
      page.networkSequence = event.sequence
      if (header.sequence !== undefined) page.eventSequence = header.sequence
      for (const listener of [...page.networkMessages]) listener(event)
      return
    }
    if (header.type === 'pageerror') {
      page.eventSequence = header.sequence
      const error = NativePageErrorInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...page.pageErrors]) listener(error)
      return
    }
    if (header.type === 'worker') {
      const event = NativeWorkerEventSchema.parse(JSON.parse(payload.toString('utf8')))
      if (event.sequence !== header.sequence)
        throw new Error('Native worker event sequence mismatch')
      if (!workerRetirements.observe(`page:${pageId}`, event)) return
      page.eventSequence = header.sequence
      if (event.event === 'close' || event.event === 'channelclosed')
        page.workerInfos.delete(event.worker.id)
      else if (event.event === 'worker' || page.workerInfos.has(event.worker.id)) {
        if (!page.workerInfos.has(event.worker.id) && page.workerInfos.size >= 1024)
          throw new Error('Native worker registry capacity exceeded')
        page.workerInfos.set(event.worker.id, event.worker)
      }
      for (const listener of [...page.workerMessages]) listener(event)
      return
    }
    if (header.type === 'console') {
      page.eventSequence = header.sequence ?? page.eventSequence + 1
      const message = NativeConsoleInfoSchema.parse(JSON.parse(payload.toString('utf8')))
      for (const listener of [...page.consoleMessages]) listener(message)
      acknowledgeNativeConsoleEvent(sourceForPage(pageId), page.eventSequence)
      return
    }
    if (header.type === 'event') {
      page.eventSequence = header.sequence ?? page.eventSequence + 1
      if (header.event === 'framenavigated' && !header.frame.parentId) page.url = header.frame.url
      if (header.event === 'framedetached') page.frameInfos.delete(header.frame.id)
      else {
        if (!page.frameInfos.has(header.frame.id) && page.frameInfos.size >= 1024)
          throw new Error('Native frame registry capacity exceeded')
        page.frameInfos.set(header.frame.id, header.frame)
      }
      for (const listener of [...page.automationEvents])
        listener({ event: header.event, frame: header.frame })
      return
    }
    if (header.type === 'navigation') {
      page.url = header.url
      return
    }
    if (stopping) return
    if (header.type === 'frame') {
      if (usesFrameSubscriptions() && !page.frameSubscription.active) return
      if (
        header.pixelFormat !== config.pixelFormat ||
        header.width !== page.captureWidth ||
        header.height !== page.captureHeight
      ) {
        fail(new Error('Native frame dimensions changed unexpectedly'))
        return
      }
      page.lastFrame = {
        data: payload,
        pixelFormat: header.pixelFormat,
        width: header.width,
        height: header.height,
        timestampMs: header.timestampMs + clockOffset,
        droppedFrames: header.dropped,
        ...(header.viewport !== undefined ? { viewport: header.viewport } : {}),
        ...(header.contentRect !== undefined ? { contentRect: header.contentRect } : {}),
      }
      for (const listener of [...page.frames]) listener(page.lastFrame)
    } else {
      const packet = {
        data: payload,
        frames: header.frames,
        timestampMs: header.timestampMs + clockOffset,
      }
      for (const listener of [...page.audio]) listener(packet)
    }
  })
  child.stdout.on('data', (chunk: Buffer) => {
    if (terminal) return
    try {
      decoder.push(chunk)
    } catch (cause) {
      fail(new Error('Native browser protocol or subscriber failure', { cause }))
    }
  })
  // Native stderr is observable only through an explicitly supplied callback.
  if (options.onDiagnostic)
    child.stderr.on('data', (chunk: Buffer) => {
      try {
        options.onDiagnostic?.(chunk.toString('utf8'))
      } catch (error) {
        fail(new Error('Native diagnostic observer failed', { cause: error }))
      }
    })
  else child.stderr.resume()
  child.stdin.on('error', (error) => {
    if (!stopping) fail(new Error('Native browser command pipe failed', { cause: error }))
  })
  child.once('error', () => fail(new Error('Could not launch the native browser executable')))
  child.once('close', (code, signal) => {
    sourceWork?.close()
    unbindSourceWork?.()
    unbindSourceWork = undefined
    exitStatus = { code, signal: signal ?? null }
    if (stopping && !terminal && (code !== 0 || (signal ?? null) !== null))
      shutdownError = new Error(
        `Native browser failed during shutdown (code ${String(code)}, signal ${String(signal)})`,
      )
    exited = true
    for (const [id, request] of pending)
      if (request.screenshotInputToken) {
        request.cleanup()
        request.reject(
          request.screenshotAbortReason ??
            terminal ??
            new Error('Actual native screenshot process exited'),
        )
        pending.delete(id)
      }
    try {
      decoder.end()
    } catch {
      if (!stopping) fail(new Error('Native browser output was truncated'))
    }
    if (!stopping)
      fail(
        new Error(
          `Native browser exited unexpectedly (code ${String(code)}, signal ${String(signal)})`,
        ),
      )
    lifecycle.abort(terminal ?? new Error('Native source closed'))
    for (const id of pages.keys()) finishPage(id, terminal ?? new Error('Native source closed'))
    locatorHandlerContexts?.closed()
    workerRetirements.clear()
    contextWorkerInfos.clear()
    contextWorkerSequences.clear()
    contextWorkerMessages.clear()
    contextWorkerPredicates.clear()
    httpLookupMessages.clear()
    contextConsoleEvents.clear()
    contextEvents.clear()
    contextRouteMutations.clear()
    contextRouteEvents.clear()
    contextNetworkEvents.clear()
    webSocketRouteEvents.clear()
    decoder.dispose()
    config.signal?.removeEventListener('abort', abort)
    void (
      temporaryProfile ? rm(profile, { recursive: true, force: true }) : Promise.resolve()
    ).then(resolveClosed, () => {
      fail(new Error('Could not remove the native browser temporary profile'))
      resolveClosed()
    })
  })
  interface QueuedNativeControl {
    readonly retry: NativeRetryInvocation | undefined
    command: string
    fields: Record<string, UntrustedInput>
    signal: AbortSignal | undefined
    pageId: number
    deadline: number
    bytes: number
    resolve: (value: UntrustedInput) => void
    reject: (error: Error) => void
    cleanup(): void
  }
  const controlQueue: QueuedNativeControl[] = []
  let controlQueueBytes = 0
  let pumpingControls = false
  const removeControl = (job: QueuedNativeControl): boolean => {
    const index = controlQueue.indexOf(job)
    if (index < 0) return false
    controlQueue.splice(index, 1)
    controlQueueBytes -= job.bytes
    job.cleanup()
    return true
  }
  const pumpControls = (): void => {
    if (pumpingControls) return
    pumpingControls = true
    try {
      while (controlQueue.length) {
        const job = controlQueue[0]
        if (!job) break
        if (
          terminal ||
          stopping ||
          exited ||
          (job.command !== 'context' &&
            !nativeOwnerCommand(job.command, job.fields) &&
            job.command !== 'http' &&
            job.command !== 'routeCallback' &&
            job.command !== 'selectors' &&
            ['closed', 'failed'].includes(pages.get(job.pageId)?.state ?? 'closed'))
        ) {
          removeControl(job)
          job.reject(terminal ?? new Error('Native browser or page is closed'))
          continue
        }
        if (job.signal?.aborted || (job.deadline && Date.now() >= job.deadline)) {
          removeControl(job)
          job.reject(
            job.signal?.reason instanceof Error
              ? job.signal.reason
              : new Error('Native control owner deadline expired while queued'),
          )
          continue
        }
        if (pending.size >= 128 || child.stdin.writableLength + job.bytes > 4 * 1024 * 1024) break
        removeControl(job)
        const fields = job.deadline
          ? { ...job.fields, timeoutMs: Math.max(1, Math.ceil(job.deadline - Date.now())) }
          : job.fields
        void dispatchRequest(job.command, fields, job.signal, job.pageId, job.retry).then(
          job.resolve,
          job.reject,
        )
      }
    } finally {
      pumpingControls = false
    }
  }
  child.stdin.on('drain', pumpControls)
  const request = (
    command: string,
    fields: Record<string, UntrustedInput> = {},
    signal?: AbortSignal,
    pageId = 1,
  ): Promise<UntrustedInput> =>
    runNativeViewerRequest(lifecycle.signal, () =>
      runNativeRetryOperation(() => {
        const handler = nativeBrowserOperationRequest(lifecycle.signal)
        if (!handler) return requestAdmitted(command, fields, signal, pageId)
        const captured = withoutNativeRetryOperation(() =>
          encodeNativeStringFields(command, fields),
        )
        return handler.token.then((operationToken) =>
          requestAdmitted(
            command,
            {
              ...captured,
              locatorHandlerOrigin: { ...handler.origin, operationToken },
            },
            signal && handler.signal
              ? AbortSignal.any([signal, handler.signal])
              : (signal ?? handler.signal),
            pageId,
            true,
          ),
        )
      }),
    )
  const requestAdmitted = (
    command: string,
    fields: Record<string, UntrustedInput> = {},
    signal?: AbortSignal,
    pageId = 1,
    encoded = false,
  ): Promise<UntrustedInput> => {
    if (!encoded)
      fields = withoutNativeRetryOperation(() => encodeNativeStringFields(command, fields))
    const retry = command === 'frameSubscription' ? undefined : captureRetrySource()
    if (retry) {
      signal = nativeRetryOperationSignal(signal, retry)
      if (retry.timeoutMs > 0 && !nativeRetryIsCleanup(retry)) {
        const duration = typeof fields.timeoutMs === 'number' ? fields.timeoutMs : 30000
        fields = {
          ...fields,
          timeoutMs: duration === 0 ? retry.timeoutMs : Math.min(duration, retry.timeoutMs),
        }
      }
    }
    signal?.throwIfAborted()
    const control =
      nativeLocatorHandlerControl(command) ||
      (command === 'context' &&
        (fields.action === 'close' ||
          fields.action === 'closePage' ||
          fields.action === 'workerNetworkBody' ||
          fields.action === 'screenshotInput')) ||
      command === 'recoverPage' ||
      command === 'frameSubscription' ||
      command === 'nativeWorkBarrier' ||
      (command === 'debugger' && nativeDebuggerControl(fields)) ||
      command === 'continueAction' ||
      command === 'continueURLPredicate' ||
      command === 'continueEventPredicate' ||
      command === 'routeCallback' ||
      command === 'networkBody' ||
      (command === 'handle' && (fields.action === 'dispose' || fields.action === 'disposeMany')) ||
      (command === 'highlight' && (fields.action === 'hide' || fields.action === 'claim')) ||
      ((command === 'recording' || command === 'streaming') &&
        (fields.action === 'stop' || fields.action === 'cancel')) ||
      command === 'claimConsoleArguments' ||
      command === 'acknowledgeConsole' ||
      (command === 'bindings' &&
        typeof fields.action === 'string' &&
        ['resolve', 'argument', 'stage'].includes(fields.action))
    // Frame observation is internal transport maintenance, including during a
    // human viewer lease; it does not author a browser action.
    if (command !== 'frameSubscription')
      assertNativeViewerAdmission(lifecycle.signal, command, fields, control)
    if (!control) {
      sourceWork?.beforeAuthorAdmission()
      return dispatchRequest(command, fields, signal, pageId, retry)
    }
    if (terminal || stopping || exited)
      return Promise.reject(terminal ?? new Error('Native browser is closed'))
    const requestedAt = Date.now()
    let bytes: number
    let serialized: string
    try {
      serialized = withoutNativeRetryOperation(() =>
        JSON.stringify({
          ...fields,
          pageId,
          version: NATIVE_PROTOCOL_VERSION,
          id: 2147483647,
          command,
        }),
      )
      // An own toJSON hook can replace the entire spread envelope. Keep the
      // JSON-object boundary without parsing/cloning immediate controls.
      if (typeof serialized !== 'string' || !serialized.startsWith('{'))
        throw new Error('Native control envelope must be a JSON object')
      bytes = Buffer.byteLength(serialized) + 1
    } catch (error) {
      return Promise.reject(
        error instanceof Error ? error : new Error('Native control serialization failed'),
      )
    }
    if (bytes > 65536) return Promise.reject(new Error('Native command exceeds 64 KiB'))
    if (
      !controlQueue.length &&
      pending.size < 128 &&
      child.stdin.writableLength + bytes < 4 * 1024 * 1024
    )
      return dispatchRequest(command, fields, signal, pageId, retry)
    if (controlQueue.length >= 128 || controlQueueBytes + bytes > 8 * 1024 * 1024)
      return Promise.reject(new Error('Native control sender queue capacity exceeded'))
    // Only queued controls need a detached snapshot. The serialized envelope is
    // already a JSON object; validating/cloning it again adds no wire safety.
    const snapshot = JSON.parse(serialized) as Record<string, UntrustedInput>
    const duration = typeof fields.timeoutMs === 'number' ? fields.timeoutMs : 30000
    const deadline = duration === 0 ? 0 : requestedAt + duration
    return new Promise((resolve, reject) => {
      let timer: ReturnType<typeof setTimeout> | undefined
      const abort = (): void => {
        if (removeControl(job))
          reject(
            signal?.reason instanceof Error
              ? signal.reason
              : new Error('Native control operation aborted while queued'),
          )
        pumpControls()
      }
      const job: QueuedNativeControl = {
        retry,
        command,
        fields: snapshot,
        signal,
        pageId,
        deadline,
        bytes,
        resolve,
        reject,
        cleanup: () => {
          clearTimeout(timer)
          signal?.removeEventListener('abort', abort)
        },
      }
      controlQueue.push(job)
      controlQueueBytes += bytes
      signal?.addEventListener('abort', abort, { once: true })
      if (deadline)
        timer = setTimeout(
          () => {
            if (removeControl(job))
              reject(new Error('Native control owner deadline expired while queued'))
            pumpControls()
          },
          Math.max(0, deadline - Date.now()),
        )
      if (signal?.aborted) abort()
      else pumpControls()
    })
  }
  function usesFrameSubscriptions(): boolean {
    return config.frameTransport === 'on-demand' && backendCapabilities?.frameSubscription === true
  }
  function scheduleFrameSubscription(pageId: number): void {
    const page = pages.get(pageId)
    if (!page || !usesFrameSubscriptions() || page.frameSubscription.queued) return
    page.frameSubscription.queued = true
    queueMicrotask(() => {
      const subscription = page.frameSubscription
      subscription.queued = false
      if (
        subscription.pending ||
        !page.ready ||
        page.state !== 'running' ||
        stopping ||
        terminal ||
        exited
      )
        return
      const enabled = page.frames.size > 0
      if (subscription.enabled === enabled && (!enabled || subscription.active)) return
      const change = { enabled, epoch: subscription.epoch }
      const renderSignal = page.renderSignal
      subscription.pending = change
      // A background observation must not inherit an authored retry or handler
      // operation. Its close signal still cancels the real pending command.
      void Promise.resolve()
        .then(() =>
          withoutNativeRetryOperation(() =>
            requestAdmitted('frameSubscription', { enabled }, page.closeLifecycle.signal, pageId),
          ),
        )
        .then(
          () => {
            subscription.pending = undefined
            subscription.enabled = enabled
            scheduleFrameSubscription(pageId)
          },
          (error: UntrustedInput) => {
            subscription.pending = undefined
            if (renderSignal.aborted) {
              subscription.enabled = true
              scheduleFrameSubscription(pageId)
              return
            }
            if (page.state === 'running' && !stopping && !terminal && !exited)
              fail(new Error('Native frame subscription failed', { cause: error }))
          },
        )
    })
  }
  const dispatchRequest = (
    command: string,
    fields: Record<string, UntrustedInput> = {},
    signal?: AbortSignal,
    pageId = 1,
    retry?: NativeRetryInvocation,
  ): Promise<UntrustedInput> => {
    signal?.throwIfAborted()
    if (
      command !== 'context' &&
      !nativeOwnerCommand(command, fields) &&
      command !== 'http' &&
      command !== 'routeCallback' &&
      command !== 'selectors' &&
      ['closed', 'failed'].includes(pages.get(pageId)?.state ?? 'closed')
    ) {
      const reason = z.instanceof(Error).safeParse(pages.get(pageId)?.lifecycle.signal.reason)
      return Promise.reject(
        reason.success ? reason.data : new Error('Native page is closed or failed'),
      )
    }
    if (terminal || stopping || exited)
      return Promise.reject(terminal ?? new Error('Native browser is closed'))
    const bindingControl =
      command === 'bindings' &&
      (fields.action === 'resolve' || fields.action === 'argument' || fields.action === 'stage')
    const capacity =
      nativeLocatorHandlerControl(command) ||
      command === 'frameSubscription' ||
      command === 'nativeWorkBarrier' ||
      (command === 'debugger' && nativeDebuggerControl(fields)) ||
      (command === 'context' &&
        (fields.action === 'close' ||
          fields.action === 'closePage' ||
          fields.action === 'workerNetworkBody' ||
          fields.action === 'screenshotInput')) ||
      command === 'continueAction' ||
      command === 'continueURLPredicate' ||
      command === 'continueEventPredicate' ||
      command === 'routeCallback' ||
      command === 'networkBody' ||
      (command === 'handle' && (fields.action === 'dispose' || fields.action === 'disposeMany')) ||
      (command === 'highlight' && (fields.action === 'hide' || fields.action === 'claim')) ||
      ((command === 'recording' || command === 'streaming') &&
        (fields.action === 'stop' || fields.action === 'cancel')) ||
      command === 'claimConsoleArguments' ||
      command === 'acknowledgeConsole' ||
      bindingControl
        ? 128
        : 64
    if (pending.size >= capacity)
      return Promise.reject(new Error('Too many pending native commands'))
    const id = ++nextId
    const message =
      withoutNativeRetryOperation(() =>
        JSON.stringify({ ...fields, pageId, version: NATIVE_PROTOCOL_VERSION, id, command }),
      ) + '\n'
    const messageBytes = Buffer.byteLength(message)
    if (messageBytes > 65_536) return Promise.reject(new Error('Native command exceeds 64 KiB'))
    if (child.stdin.writableLength + messageBytes > 4 * 1024 * 1024)
      return Promise.reject(new Error('Native command transport buffer is full'))
    const callbackLifetime =
      command === 'frameSubscription' ? undefined : locatorHandlerContexts?.capture(id, pageId)
    if (callbackLifetime) retainNativeRetryOperation(callbackLifetime)
    return new Promise((resolve, reject) => {
      const cancel = (): void => {
        const actual = pending.get(id)
        if (!actual || actual.screenshotAbortReason) return
        const reason =
          signal?.reason instanceof Error ? signal.reason : new Error('Native operation aborted')
        if (actual.screenshotInputToken) {
          // The native assertion keeps its actual capture/restore/codec/IO
          // receipts. Retain this real pending command until its reply or the
          // actual process close; caller abort alone is not a retirement ACK.
          actual.screenshotAbortReason = reason
          signal?.removeEventListener('abort', cancel)
        } else {
          pending.delete(id)
          cleanup()
        }
        const cancellation =
          JSON.stringify({
            version: NATIVE_PROTOCOL_VERSION,
            id: ++nextId,
            command: 'cancel',
            pageId,
            target: id,
          }) + '\n'
        if (!terminal && !stopping && !exited) child.stdin.write(cancellation)
        if (!actual.screenshotInputToken) reject(reason)
      }
      const cleanup = (): void => {
        clearTimeout(timer)
        signal?.removeEventListener('abort', cancel)
        queueMicrotask(pumpControls)
      }
      const duration = typeof fields.timeoutMs === 'number' ? fields.timeoutMs : 30_000
      const timer =
        duration === 0
          ? undefined
          : setTimeout(() => {
              const actual = pending.get(id)
              if (!actual) return
              const error = new NativeTimeoutError('Native browser command timed out')
              if (actual.screenshotInputToken) {
                // A deadline closes caller admission, not the actual native
                // capture/restore/codec/IO lifetime. Keep its original failure
                // until a response or physical process close retires the work.
                actual.screenshotAbortReason ??= error
                signal?.removeEventListener('abort', cancel)
              } else {
                pending.delete(id)
                cleanup()
              }
              const cancellation =
                JSON.stringify({
                  version: NATIVE_PROTOCOL_VERSION,
                  id: ++nextId,
                  command: 'cancel',
                  pageId,
                  target: id,
                }) + '\n'
              if (!terminal && !stopping && !exited) child.stdin.write(cancellation)
              nativeRetryInfrastructureFailure(error, retry)
              if (!actual.screenshotInputToken) reject(error)
            }, duration + 1000)
      pending.set(id, {
        command,
        screenshotAbortReason: undefined,
        screenshotInputToken:
          command === 'artifact' &&
          fields.action === 'compareCommit' &&
          typeof fields.inputToken === 'string'
            ? fields.inputToken
            : undefined,
        assertionDeadlineKind:
          command === 'artifact' && fields.action === 'compareCommit'
            ? 'toHaveScreenshot'
            : command === 'queryEvaluate' &&
                fields.dom === true &&
                fields.expectation === true &&
                typeof fields.nativeAssertionKind === 'string'
              ? nativeAssertionDeadlineKind(fields.nativeAssertionKind, undefined)
              : undefined,
        event: typeof fields.event === 'string' ? fields.event : undefined,
        resolve,
        reject,
        cleanup,
        pageId:
          command === 'context' ||
          command === 'http' ||
          command === 'selectors' ||
          command === 'routeCallback'
            ? 0
            : pageId,
      })
      signal?.addEventListener('abort', cancel, { once: true })
      child.stdin.write(message)
      if (signal?.aborted) cancel()
    })
  }
  const close = (): Promise<void> => {
    if (closing) return closing
    for (const page of pages.values()) if (page.source) prepareNativeNavigationClose(page.source)
    stopping = true
    lifecycle.abort(new Error('Native browser is closing'))
    closing = (async () => {
      for (const [id, item] of pending) {
        if (item.screenshotInputToken) {
          item.screenshotAbortReason ??= new Error('Native browser is closing')
          continue
        }
        item.cleanup()
        item.reject(new Error('Native browser is closing'))
        pending.delete(id)
      }
      if (!exited) child.stdin.end()
      // Native recording/stream finalization owns a real 60-second deadline.
      // Give that work and CEF's physical process drain time to settle under
      // load, then enforce a finite process-wide shutdown bound.
      const timer = setTimeout(() => child.kill('SIGKILL'), 75_000)
      try {
        await closed
        if (shutdownError) throw shutdownError
      } finally {
        clearTimeout(timer)
      }
    })()
    return closing
  }
  const bindSourceWork = sourceAdmission?.bindWork?.bind(sourceAdmission)
  const observeSourceWork = sourceAdmission?.observeWork?.bind(sourceAdmission)
  if (sourceAdmission && bindSourceWork && observeSourceWork) {
    const actualAdmission = sourceAdmission
    sourceWork = new NativeSourceWorkCallbacks(actualAdmission.ticket, {
      ordered: (challenge) =>
        request('nativeWorkBarrier', {
          sourceTicket: challenge.ticket,
          barrier: challenge.nonce,
          borrowLease: challenge.borrowLease,
          borrowGeneration: challenge.borrowGeneration,
          nativeDeadline: challenge.deadline,
          timeoutMs: 0,
        }),
      observed: (_ticket, watermark) => observeSourceWork(watermark),
      failed: (_challenge, error) => {
        // Native close/drain cancels this internal barrier; it does not turn
        // graceful CEF shutdown into a new protocol-failure SIGKILL. The native
        // Source/host lease and rejected barrier remain physically fenced.
        if (!stopping && !exited) fail(error)
      },
      changed: () => actualAdmission.workChanged?.(),
    })
    unbindSourceWork = bindSourceWork(sourceWork)
  }
  const abort = (): void => {
    fail(new Error('Native browser aborted'))
    void close().catch(() => undefined)
  }
  config.signal?.addEventListener('abort', abort, { once: true })
  if (config.signal?.aborted) abort()
  try {
    await raceWithAbort(
      Promise.race([
        sourceAdmission === undefined ? ready : Promise.all([ready, sourceAdmission.startup()]),
        failure,
      ]),
      AbortSignal.timeout(config.startupTimeoutMs),
    )
  } catch (error) {
    await close()
    throw error
  }
  const sourceForPage = (pageId: number): NativeBrowser => {
    if (!Number.isInteger(pageId) || pageId < 1) throw new Error('Invalid native page ID')
    const cachedClosed = closedPageSources.get(pageId)?.deref()
    if (cachedClosed) return cachedClosed
    if (pageId !== 1 && !pages.has(pageId)) throw new Error('Native page is closed or unknown')
    const page = pageState(pageId)
    if (page.source) return page.source
    const assertRunning = (): void => {
      if (page.lifecycle.signal.aborted && page.lifecycle.signal.reason instanceof Error)
        throw new Error(`Native source is not running: ${page.lifecycle.signal.reason.message}`, {
          cause: page.lifecycle.signal.reason,
        })
      if (stopping || exited || terminal || page.state !== 'running')
        throw new Error('Native source is not running')
    }
    const awaitReady = async (signal?: AbortSignal): Promise<void> => {
      if (page.ready) return
      await raceWithAbort(
        page.readyPromise,
        AbortSignal.any([
          lifecycle.signal,
          page.lifecycle.signal,
          ...(signal ? [signal] : []),
          AbortSignal.timeout(config.startupTimeoutMs),
        ]),
      )
    }
    const source: NativeBrowser = {
      pageId,
      videoDescriptor: () => (page.video ? { ...page.video } : undefined),
      backendCapabilities: () => backendCapabilities,
      get state() {
        return terminal ? 'failed' : exited ? 'closed' : stopping ? 'closing' : page.state
      },
      pixelFormat: config.pixelFormat,
      get signal() {
        return page.renderSignal
      },
      pageSignal: AbortSignal.any([lifecycle.signal, page.closeLifecycle.signal]),
      processSignal: lifecycle.signal,
      get url() {
        return page.url
      },
      get width() {
        return page.captureWidth
      },
      get height() {
        return page.captureHeight
      },
      get viewport() {
        return page.viewport ? { ...page.viewport } : page.viewport
      },
      framesPerSecond: config.framesPerSecond,
      get cefVersion() {
        return cefVersion
      },
      get nativeClockOffsetMs() {
        if (clockOffset === undefined) throw new Error('Native clock mapping is not ready')
        return clockOffset
      },
      get failure() {
        return page.failure
      },
      closed: page.closed,
      processClosed: closed,
      processExit: () => (exitStatus ? { ...exitStatus } : undefined),
      navigate: (url) =>
        runNativeRetryOperation(async () => {
          if (!page.ready) await awaitReady()
          const parsed = new URL(url)
          if (!['http:', 'https:'].includes(parsed.protocol))
            throw new Error('Native navigation requires HTTP(S)')
          await request('navigate', { url: parsed.href }, undefined, pageId)
        }),
      sendDevToolsCommand: (method, params = {}) =>
        runNativeRetryOperation(() =>
          page.ready
            ? request('devtools', { method, params }, undefined, pageId)
            : awaitReady().then(() => request('devtools', { method, params }, undefined, pageId)),
        ),
      runAutomation: (command, params = {}, signal) =>
        runNativeRetryOperation(() =>
          command === 'http' ||
          command === 'context' ||
          command === 'selectors' ||
          command === 'routeCallback' ||
          command === 'networkBody' ||
          nativeOwnerCommand(command, params) ||
          page.ready
            ? request(command, params, signal, pageId)
            : awaitReady(signal).then(() => request(command, params, signal, pageId)),
        ),
      runContext: (action, params = {}, signal) =>
        request(
          'context',
          {
            action,
            params,
            ...(typeof params.timeoutMs === 'number' ? { timeoutMs: params.timeoutMs } : {}),
          },
          signal,
          1,
        ),
      takeContextNetworkBody: (context, owner, bodyId) => {
        if (owner.startsWith('page:')) {
          const actual = pages.get(Number(owner.slice(5)))
          if (!actual) throw new Error('Actual native request body Page owner is unavailable')
          return actual.networkBodies.take(bodyId)
        }
        const state = contextNetworkBodies.get(owner)
        if (state?.context !== context)
          throw new Error('Actual native worker request body owner is unavailable')
        return state.receiver.take(bodyId)
      },
      contextNetworkSequence: () => contextNetworkSequence,
      onWebSocketRouteMessage: (listener) => {
        if (stopping || exited || terminal) assertRunning()
        webSocketRouteEvents.add(listener)
        return () => webSocketRouteEvents.delete(listener)
      },
      onContextRoute: (listener) => {
        if (stopping || exited || terminal) assertRunning()
        contextRouteEvents.add(listener)
        return () => contextRouteEvents.delete(listener)
      },
      onContextRouteMutation: (listener) => {
        if (stopping || exited || terminal) assertRunning()
        contextRouteMutations.add(listener)
        return () => contextRouteMutations.delete(listener)
      },
      onContextNetwork: (listener) => {
        if (stopping || exited || terminal) throw new Error('Native process is not running')
        contextNetworkEvents.add(listener)
        return () => contextNetworkEvents.delete(listener)
      },
      contextWorkerInfos: (contextId) =>
        [...(contextWorkerInfos.get(contextId)?.values() ?? [])].map((worker) => ({ ...worker })),
      contextWorkerRetirements: (contextId) => workerRetirements.snapshot(`context:${contextId}`),
      contextWorkerSequence: (contextId) => contextWorkerSequences.get(contextId) ?? 0,
      onContextWorkerMessage: (listener) => {
        if (stopping || exited || terminal) throw new Error('Native process is not running')
        contextWorkerMessages.add(listener)
        return () => contextWorkerMessages.delete(listener)
      },
      onContextWorkerPredicate: (listener) => {
        if (stopping || exited || terminal) throw new Error('Native process is not running')
        contextWorkerPredicates.add(listener)
        return () => contextWorkerPredicates.delete(listener)
      },
      contextConsoleSequence: () => contextConsoleSequence,
      onContextConsole: (listener) => {
        if (stopping || exited || terminal) throw new Error('Native process is not running')
        contextConsoleEvents.add(listener)
        return () => contextConsoleEvents.delete(listener)
      },
      contextSequence: () => contextSequence,
      pageSource: sourceForPage,
      onContextEvent: (listener) => {
        if (stopping || exited || terminal) throw new Error('Native process is not running')
        contextEvents.add(listener)
        return () => contextEvents.delete(listener)
      },
      frameInfos: () => [...page.frameInfos.values()].map((frame) => ({ ...frame })),
      workerRetirements: () => workerRetirements.snapshot(`page:${pageId}`),
      workerInfos: () => [...page.workerInfos.values()].map((worker) => ({ ...worker })),
      onWorkerMessage: (listener) => {
        assertRunning()
        page.workerMessages.add(listener)
        return () => page.workerMessages.delete(listener)
      },
      eventSequence: () => page.eventSequence,
      webSocketSequence: () => page.socketSequence,
      networkSequence: () => page.networkSequence,
      takeNetworkBody: (bodyId) => page.networkBodies.take(bodyId),
      onAutomationEvent: (listener) => {
        assertRunning()
        page.automationEvents.add(listener)
        return () => page.automationEvents.delete(listener)
      },
      onPageErrorMessage: (listener) => {
        assertRunning()
        page.pageErrors.add(listener)
        return () => page.pageErrors.delete(listener)
      },
      onConsoleMessage: (listener) => {
        assertRunning()
        page.consoleMessages.add(listener)
        return () => page.consoleMessages.delete(listener)
      },
      onLocatorHandlerStage: (listener) => {
        if (page.state === 'closed' || terminal || exited || stopping) assertRunning()
        locatorHandlerContexts ??= new NativeLocatorHandlerContexts()
        return locatorHandlerContexts.subscribe(pageId, listener)
      },
      onActionStage: (listener) => {
        assertRunning()
        page.actionStages.add(listener)
        return () => page.actionStages.delete(listener)
      },
      onURLPredicate: (listener) => {
        assertRunning()
        page.urlPredicates.add(listener)
        return () => page.urlPredicates.delete(listener)
      },
      onCrashMessage: (listener) => {
        if (page.state === 'closed' || terminal || exited || stopping) assertRunning()
        page.crashes.add(listener)
        return () => page.crashes.delete(listener)
      },
      onEventPredicate: (listener) => {
        if (page.state === 'closed' || terminal || exited || stopping) assertRunning()
        page.eventPredicates.add(listener)
        return () => page.eventPredicates.delete(listener)
      },
      runStreaming: (params, signal) => request('streaming', params, signal, pageId),
      onStreamingMessage: (listener) => {
        if (page.state === 'closed' || terminal || exited || stopping) assertRunning()
        page.streaming.add(listener)
        return () => page.streaming.delete(listener)
      },
      runRecording: (params, signal) => request('recording', params, signal, pageId),
      onRecordingMessage: (listener) => {
        if (page.state === 'closed' || terminal || exited || stopping) assertRunning()
        page.recordings.add(listener)
        return () => page.recordings.delete(listener)
      },
      onDownloadMessage: (listener) => {
        assertRunning()
        page.downloads.add(listener)
        return () => page.downloads.delete(listener)
      },
      onFileChooserMessage: (listener) => {
        assertRunning()
        page.fileChoosers.add(listener)
        return () => page.fileChoosers.delete(listener)
      },
      onDialogMessage: (listener) => {
        assertRunning()
        page.dialogs.add(listener)
        return () => page.dialogs.delete(listener)
      },
      onHTTPLookupMessage: (listener) => {
        if (stopping || exited || terminal) throw new Error('Native process is not running')
        httpLookupMessages.add(listener)
        return () => httpLookupMessages.delete(listener)
      },
      onBindingMessage: (listener) => {
        assertRunning()
        page.bindingMessages.add(listener)
        return () => page.bindingMessages.delete(listener)
      },
      onWebSocketMessage: (listener) => {
        assertRunning()
        page.socketMessages.add(listener)
        return () => page.socketMessages.delete(listener)
      },
      onNetworkMessage: (listener) => {
        assertRunning()
        page.networkMessages.add(listener)
        return () => page.networkMessages.delete(listener)
      },
      onFrame: (listener) => {
        assertRunning()
        const alreadySubscribed = page.frames.has(listener)
        page.frames.add(listener)
        try {
          if (page.lastFrame) listener(page.lastFrame)
        } catch (error) {
          // A failed initial delivery never returns an unsubscribe function.
          // Roll back only this registration; an existing subscription still owns it.
          if (!alreadySubscribed) page.frames.delete(listener)
          throw error
        }
        scheduleFrameSubscription(pageId)
        return () => {
          if (!page.frames.delete(listener) || page.frames.size || !usesFrameSubscriptions()) return
          page.lastFrame = undefined
          page.frameSubscription.active = false
          ++page.frameSubscription.epoch
          scheduleFrameSubscription(pageId)
        }
      },
      onAudio: (listener) => {
        assertRunning()
        page.audio.add(listener)
        return () => page.audio.delete(listener)
      },
      close: async () => {
        if (page.state === 'closed') return
        if (page.state === 'closing') {
          await page.closed
          return
        }
        prepareNativeNavigationClose(source)
        page.state = 'closing'
        try {
          await request('context', { action: 'closePage', params: { pageId } }, undefined, 1)
          await page.closed
        } catch (error) {
          if (!page.lifecycle.signal.aborted) page.state = 'running'
          throw error
        }
      },
    }
    page.source = source
    return source
  }
  const defaultPage = sourceForPage(1)
  return {
    ...defaultPage,
    failure,
    signal: lifecycle.signal,
    closed,
    get state() {
      return terminal ? 'failed' : exited ? 'closed' : stopping ? 'closing' : 'running'
    },
    get url() {
      return defaultPage.url
    },
    close,
    processExit: () => (exitStatus ? { ...exitStatus } : undefined),
  }
}
