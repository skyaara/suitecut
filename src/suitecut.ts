import { spawn, type ChildProcess } from 'node:child_process'
import { randomUUID } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, rm, stat } from 'node:fs/promises'
import { dirname } from 'node:path'

import { type Locator, type Page } from 'playwright'

import { raceWithAbort } from './abort.js'
import { type SuiteCutArtifactSink } from './artifact-sink.js'
import {
  DEFAULT_SUITE_CUT_CHECKPOINT_DURATION_MS,
  DEFAULT_SUITE_CUT_CAPTURE_FRAMES_PER_SECOND,
  DEFAULT_SUITE_CUT_VIEWPORT,
  resolveCaptureLayout,
} from './capture.js'
import {
  type SuiteCutCaptureOptions,
  type SuiteCutCheckpointOptions,
  type SuiteCutFixture,
  type SuiteCutNarrationOptions,
  type SuiteCutPointerActionOptions,
  type SuiteCutScrollOptions,
  type SuiteCutTypeOptions,
} from './fixtures.js'
import { createLiveStream, type SuiteCutLiveStream } from './live-stream.js'
import { createLiveCamera } from './live-zoom.js'
import { type SuiteCutNarrationPipeline } from './narration.js'
import {
  movePresentationCursor,
  pulsePresentationCursor,
  showPresentationCaption,
  showPresentationHighlight,
  waitForPresentationAnimations,
} from './presentation.js'
import { resolveFfmpeg, runProcess, waitForProcessExit } from './process.js'
import { scrollLocator, scrollPageTop } from './scroll.js'
import { deriveSourceStartedAt } from './timing.js'
import {
  type Milliseconds,
  type SuiteCutActiveScreencast,
  type SuiteCutAttemptClock,
  type SuiteCutCheckpointEvent,
  type SuiteCutEventAttachment,
  type SuiteCutRecordingSession,
  type SuiteCutZoomOptions,
  type SuiteCutPageId,
  type SuiteCutPage,
  type SuiteCutViewport,
  type SuiteCutNarrationEvent,
  type SuiteCutPageEventBase,
  type SuiteCutPageSelectionReason,
  type SuiteCutHoldEvent,
  type SuiteCutHighlightEvent,
  type SuiteCutHighlightOptions,
  type SuiteCutPointerButton,
  type SuiteCutPointerButtonEvent,
  type SuiteCutPointerMoveEvent,
  type SuiteCutRect,
  type SuiteCutZoomEvent,
} from './types.js'
import {
  parseCapturedGeometry,
  parseCapturedViewport,
  parseCaptureOptions,
  parseCheckpointInput,
  parseHighlightOptions,
  parseHoldInput,
  parseNarrationInput,
  parsePointerActionOptions,
  parseScrollOptions,
  parseTypeInput,
  parseZoomOptions,
} from './validation.js'
import { resolveSuiteCutZoomOptions } from './zoom.js'

interface ActivePageListenerRegistration {
  dispose(): void
}

interface AsyncDisposeResource {
  dispose(): Promise<void>
}

type ActivePageBinding = (payload?: CapturedPointerPayload) => void | Promise<void>
type SuiteCutWindowProperties = Record<
  string,
  ActivePageBinding | ActivePageListenerRegistration | undefined
>

interface CapturedPointerPayload {
  type: 'pointer-move' | 'pointer-down' | 'pointer-up' | 'click'
  x: number
  y: number
  pointerType: string
  button: number
  viewport: SuiteCutViewport
  targetRect?: { x: number; y: number; width: number; height: number }
}

interface StreamingRecorder {
  readonly child: ChildProcess
  readonly completion: Promise<void>
  readonly firstFrame: Promise<void>
  readonly stdin: NodeJS.WritableStream
  writeQueue: Promise<void>
  firstFrameEpochMs?: number
  lastFrame?: Buffer
  lastFrameNumber: number
  closing: boolean
  excludedDurationMs: number
  pausedAtEpochMs?: number
}

interface PendingCheckpointClip {
  event: SuiteCutCheckpointEvent
  attachmentName: string
  outputPath: string
}

const INITIAL_FRAME_TIMEOUT_MS = 10_000
const CAPTURE_GRACEFUL_SHUTDOWN_TIMEOUT_MS = 5_000
const CAPTURE_FORCE_KILL_AFTER_MS = 2_000
const CAPTURE_STDERR_MAX_BYTES = 64 * 1024

function appendBoundedBuffer(
  chunks: Buffer[],
  chunk: Buffer,
  bufferedBytes: number,
  maxBytes: number,
): number {
  if (chunk.length >= maxBytes) {
    chunks.splice(0, chunks.length, chunk.subarray(-maxBytes))
    return maxBytes
  }

  chunks.push(chunk)
  bufferedBytes += chunk.length
  while (bufferedBytes > maxBytes) {
    const first = chunks[0]
    if (first === undefined) return 0
    const overflow = bufferedBytes - maxBytes
    if (first.length <= overflow) {
      chunks.shift()
      bufferedBytes -= first.length
    } else {
      chunks[0] = first.subarray(overflow)
      bufferedBytes -= overflow
    }
  }
  return bufferedBytes
}

function readSuiteCutViewport(): SuiteCutViewport {
  return {
    width: window.innerWidth,
    height: window.innerHeight,
    scrollX: window.scrollX,
    scrollY: window.scrollY,
  }
}

function readSuiteCutLocatorRect(element: Element, geometry: 'element' | 'content'): SuiteCutRect {
  const elementRect = element.getBoundingClientRect()
  if (geometry === 'element') {
    return {
      x: elementRect.x,
      y: elementRect.y,
      width: elementRect.width,
      height: elementRect.height,
    }
  }

  const rects: DOMRect[] = []
  const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT)
  let node = walker.nextNode()
  while (node !== null) {
    if (node.textContent?.trim().length !== 0) {
      const range = document.createRange()
      range.selectNodeContents(node)
      rects.push(...Array.from(range.getClientRects()))
      range.detach()
    }
    node = walker.nextNode()
  }

  const visualElements = element.matches('img,svg,canvas,video,input,textarea,select,button')
    ? [element]
    : Array.from(element.querySelectorAll('img,svg,canvas,video,input,textarea,select,button'))
  rects.push(...visualElements.map((candidate) => candidate.getBoundingClientRect()))

  const visibleRects = rects.filter((rect) => rect.width > 0 && rect.height > 0)
  if (visibleRects.length === 0) {
    return {
      x: elementRect.x,
      y: elementRect.y,
      width: elementRect.width,
      height: elementRect.height,
    }
  }

  const left = Math.max(elementRect.left, Math.min(...visibleRects.map((rect) => rect.left)))
  const top = Math.max(elementRect.top, Math.min(...visibleRects.map((rect) => rect.top)))
  const right = Math.min(elementRect.right, Math.max(...visibleRects.map((rect) => rect.right)))
  const bottom = Math.min(elementRect.bottom, Math.max(...visibleRects.map((rect) => rect.bottom)))
  if (right <= left || bottom <= top) {
    return {
      x: elementRect.x,
      y: elementRect.y,
      width: elementRect.width,
      height: elementRect.height,
    }
  }
  return { x: left, y: top, width: right - left, height: bottom - top }
}

function applySuiteCutLayoutScale(scale: number): void {
  document.documentElement.style.zoom = String(scale)
}

function installActivePageListeners(options: { bindingName: string; markerName: string }): void {
  const target = window
  const properties = target as typeof target & SuiteCutWindowProperties
  if (properties[options.markerName] !== undefined) return

  const notifySuiteCut = (event: Event): void => {
    const binding = properties[options.bindingName]
    if (typeof binding !== 'function') return

    if (!(event instanceof PointerEvent)) {
      void Promise.resolve(binding()).catch(() => undefined)
      return
    }

    const eventTarget = event.target instanceof Element ? event.target : undefined
    const rect = eventTarget?.getBoundingClientRect()
    const payload: CapturedPointerPayload = {
      type:
        event.type === 'pointerover'
          ? 'pointer-move'
          : (event.type as CapturedPointerPayload['type']),
      x: event.clientX,
      y: event.clientY,
      pointerType: event.pointerType,
      button: event.button,
      viewport: {
        width: window.innerWidth,
        height: window.innerHeight,
        scrollX: window.scrollX,
        scrollY: window.scrollY,
      },
    }
    if (rect !== undefined) {
      payload.targetRect = {
        x: rect.x,
        y: rect.y,
        width: rect.width,
        height: rect.height,
      }
    }
    void Promise.resolve(binding(payload)).catch(() => undefined)
  }

  const eventTypes = [
    'pointerdown',
    'pointerup',
    'click',
    'pointerover',
    'keydown',
    'input',
    'change',
    'wheel',
    'scroll',
  ]
  for (const eventType of eventTypes) {
    target.addEventListener(eventType, notifySuiteCut, { capture: true, passive: true })
  }

  const registration: ActivePageListenerRegistration = {
    dispose: () => {
      for (const eventType of eventTypes) {
        target.removeEventListener(eventType, notifySuiteCut, true)
      }
      delete properties[options.markerName]
    },
  }
  properties[options.markerName] = registration
}

function removeActivePageListeners(markerName: string): void {
  const properties = window as typeof window & SuiteCutWindowProperties
  const registration = properties[markerName]
  if (registration === undefined || typeof registration === 'function') return
  registration.dispose()
}

export async function createRecordingSession({
  captureOptions,
  output,
  page,
  signal,
}: {
  captureOptions: SuiteCutCaptureOptions
  output: SuiteCutArtifactSink
  page: Page
  signal?: AbortSignal
}): Promise<SuiteCutRecordingSession> {
  signal?.throwIfAborted()
  const originEpochMs = Date.now()
  const originMonotonicMs = performance.now()

  const clock: SuiteCutAttemptClock = {
    originEpochMs,
    originMonotonicMs,
    startedAt: new Date(originEpochMs).toISOString(),
  }

  const attemptId = randomUUID()
  const bindingName = `__suitecutSelectPage_${attemptId.replaceAll('-', '')}`
  const markerName = `${bindingName}_listeners`
  const listenerOptions = { bindingName, markerName }
  const pages = new Map<SuiteCutPageId, SuiteCutPage>()
  const pageIds = new WeakMap<Page, SuiteCutPageId>()
  const livePages = new Map<SuiteCutPageId, Page>()
  const pageCloseListeners = new Map<Page, (closedPage: Page) => void>()
  const pageDocumentListeners = new Map<Page, () => void>()
  const pageCaptureControllers = new Map<Page, AbortController>()
  const pageCaptureTasks = new Map<SuiteCutPageId, Promise<void>>()
  const pageLayoutScales = new Map<Page, number>()
  const pendingPageTasks = new Set<Promise<void>>()
  const pageTaskErrors: Error[] = []
  const events: SuiteCutRecordingSession['events'] = []
  const artifacts: SuiteCutRecordingSession['artifacts'] = []
  const screencasts: SuiteCutRecordingSession['screencasts'] = new Map()
  const recordedScreencasts = new Map<SuiteCutPageId, SuiteCutActiveScreencast>()
  const videos: SuiteCutRecordingSession['videos'] = []
  const pendingCheckpointClips: PendingCheckpointClip[] = []
  const streamingRecorders = new Map<SuiteCutPageId, StreamingRecorder>()
  const context = page.context()
  const ffmpegPath = await raceWithAbort(resolveFfmpeg(), signal)
  const parsedCaptureOptions = parseCaptureOptions(captureOptions)
  const captureFramesPerSecond =
    parsedCaptureOptions.framesPerSecond ?? DEFAULT_SUITE_CUT_CAPTURE_FRAMES_PER_SECOND
  const streamOnly = parsedCaptureOptions.stream !== undefined
  const latestFrames = new Map<SuiteCutPageId, Buffer>()
  const captureFrameDurationMs = 1_000 / captureFramesPerSecond
  let activePageId: SuiteCutPageId
  let mainPageId: SuiteCutPageId
  let active = true
  let endedAtMs: Milliseconds | undefined
  let endingPromise: Promise<void> | undefined
  let sealedAttachment: Promise<SuiteCutEventAttachment> | undefined
  let capturePauseDepth = 0
  let capturePausedAtMonotonicMs: number | undefined
  let excludedCaptureDurationMs = 0
  let liveStream: SuiteCutLiveStream | undefined
  const liveCamera = createLiveCamera(signal)

  const now = (): Milliseconds => {
    const activePauseDurationMs =
      capturePausedAtMonotonicMs === undefined ? 0 : performance.now() - capturePausedAtMonotonicMs
    return performance.now() - originMonotonicMs - excludedCaptureDurationMs - activePauseDurationMs
  }

  const selectActivePage = (
    pageId: SuiteCutPageId,
    reason: SuiteCutPageSelectionReason,
  ): SuiteCutPageId => {
    if (activePageId === pageId) return pageId
    activePageId = pageId
    const frame = latestFrames.get(pageId) ?? streamingRecorders.get(pageId)?.lastFrame
    if (frame !== undefined) liveStream?.update(frame)
    if (!streamOnly)
      events.push({ id: randomUUID(), type: 'page-selected', atMs: now(), pageId, reason })
    return pageId
  }

  const withCapturePaused = async <T>(operation: () => Promise<T>): Promise<T> => {
    // Newly opened pages need their first frame before narration can pause capture.
    // Otherwise slow synthesis can exhaust their first-frame startup timeout.
    if (capturePauseDepth === 0) await drainPageTasks()
    capturePauseDepth += 1
    if (capturePauseDepth === 1) {
      capturePausedAtMonotonicMs = performance.now()
      const pauseEpochMs = Date.now()
      for (const recorder of streamingRecorders.values()) {
        recorder.pausedAtEpochMs = pauseEpochMs
      }
    }

    try {
      return await operation()
    } finally {
      capturePauseDepth -= 1
      if (capturePauseDepth === 0 && capturePausedAtMonotonicMs !== undefined) {
        const resumeEpochMs = Date.now()
        excludedCaptureDurationMs += performance.now() - capturePausedAtMonotonicMs
        capturePausedAtMonotonicMs = undefined
        for (const recorder of streamingRecorders.values()) {
          if (recorder.pausedAtEpochMs !== undefined) {
            if (recorder.firstFrameEpochMs !== undefined) {
              recorder.excludedDurationMs += resumeEpochMs - recorder.pausedAtEpochMs
            }
            delete recorder.pausedAtEpochMs
          }
        }
      }
    }
  }

  const trackPageTask = (task: Promise<void>): Promise<void> => {
    const trackedTask = task.catch((error) => {
      pageTaskErrors.push(error instanceof Error ? error : new Error(String(error)))
    })

    pendingPageTasks.add(trackedTask)
    void trackedTask.then(() => {
      pendingPageTasks.delete(trackedTask)
    })
    return trackedTask
  }

  const queuePageCaptureTask = (
    pageId: SuiteCutPageId,
    operation: () => Promise<void>,
  ): Promise<void> => {
    const previous = pageCaptureTasks.get(pageId) ?? Promise.resolve()
    const tracked = trackPageTask(previous.then(operation))
    pageCaptureTasks.set(pageId, tracked)
    void tracked.then(() => {
      if (pageCaptureTasks.get(pageId) === tracked) pageCaptureTasks.delete(pageId)
    })
    return tracked
  }

  const drainPageTasks = async (): Promise<void> => {
    while (pendingPageTasks.size > 0) await Promise.all([...pendingPageTasks])
  }

  const startScreencast = async (
    playwrightPage: Page,
    pageId: SuiteCutPageId,
    startSignal: AbortSignal,
  ): Promise<boolean> => {
    const artifactId = randomUUID()
    const attachmentName = `suitecut-source-${attemptId}-${artifactId}.webm`
    const outputPath = output.pathFor(attachmentName)
    const activeScreencast: SuiteCutActiveScreencast = {
      artifactId,
      pageId,
      page: playwrightPage,
      outputPath,
      attachmentName,
    }
    if (!streamOnly) recordedScreencasts.set(pageId, activeScreencast)
    let captureLayout: ReturnType<typeof resolveCaptureLayout>
    try {
      if (startSignal.aborted || playwrightPage.isClosed()) return false
      const fallbackViewport = playwrightPage.viewportSize() ?? DEFAULT_SUITE_CUT_VIEWPORT
      captureLayout = resolveCaptureLayout(parsedCaptureOptions, fallbackViewport)
      pageLayoutScales.set(playwrightPage, captureLayout.layoutScale)
      if (
        fallbackViewport.width !== captureLayout.surfaceViewport.width ||
        fallbackViewport.height !== captureLayout.surfaceViewport.height
      ) {
        await raceWithAbort(
          playwrightPage.setViewportSize(captureLayout.surfaceViewport),
          startSignal,
        )
      }
      if (captureLayout.layoutScale !== 1) {
        await raceWithAbort(
          playwrightPage.addInitScript(applySuiteCutLayoutScale, captureLayout.layoutScale),
          startSignal,
        )
        await raceWithAbort(
          playwrightPage.evaluate(applySuiteCutLayoutScale, captureLayout.layoutScale),
          startSignal,
        )
      }
    } catch (error) {
      if (startSignal.aborted || playwrightPage.isClosed()) return false
      throw error
    }
    if (startSignal.aborted || playwrightPage.isClosed()) return false
    const captureSize = captureLayout.captureSize
    if (streamOnly) {
      screencasts.set(pageId, activeScreencast)
      let received = (): void => undefined
      const first = new Promise<void>((resolve) => {
        received = resolve
      })
      try {
        await raceWithAbort(
          playwrightPage.screencast.start({
            size: captureSize,
            quality: parsedCaptureOptions.quality ?? 90,
            onFrame: ({ data }) => {
              if (!active) return
              latestFrames.set(pageId, data)
              if (pageId === activePageId) liveStream?.update(data)
              received()
            },
          }),
          startSignal,
        )
        await raceWithAbort(
          first,
          AbortSignal.any([startSignal, AbortSignal.timeout(INITIAL_FRAME_TIMEOUT_MS)]),
        )
        return true
      } catch (error) {
        screencasts.delete(pageId)
        latestFrames.delete(pageId)
        await raceWithAbort(
          playwrightPage.screencast.stop(),
          AbortSignal.timeout(CAPTURE_FORCE_KILL_AFTER_MS),
        ).catch(() => undefined)
        if (startSignal.aborted || playwrightPage.isClosed()) return false
        throw error
      }
    }
    await mkdir(dirname(outputPath), { recursive: true })
    let resolveFirstFrame = (): void => undefined
    let rejectFirstFrame = (error: Error): void => {
      void error
    }
    const firstFrame = new Promise<void>((resolve, reject) => {
      resolveFirstFrame = resolve
      rejectFirstFrame = (error: Error) => reject(error)
    })
    void firstFrame.catch(() => undefined)
    const child = spawn(
      ffmpegPath,
      [
        '-loglevel',
        'error',
        '-f',
        'image2pipe',
        '-framerate',
        String(captureFramesPerSecond),
        '-vcodec',
        'mjpeg',
        '-i',
        'pipe:0',
        '-y',
        '-an',
        '-r',
        String(captureFramesPerSecond),
        '-c:v',
        'libvpx-vp9',
        '-crf',
        '18',
        '-b:v',
        '0',
        '-deadline',
        'realtime',
        '-cpu-used',
        '5',
        '-row-mt',
        '1',
        '-threads',
        '8',
        outputPath,
      ],
      {
        shell: false,
        stdio: ['pipe', 'ignore', 'pipe'],
      },
    )
    const stderr: Buffer[] = []
    let stderrBytes = 0
    child.stderr.on('data', (chunk: Buffer) => {
      stderrBytes = appendBoundedBuffer(stderr, chunk, stderrBytes, CAPTURE_STDERR_MAX_BYTES)
    })
    const completion = new Promise<void>((resolveCompletion, rejectCompletion) => {
      let settled = false
      child.once('error', (error) => {
        rejectFirstFrame(error)
        if (settled) return
        settled = true
        rejectCompletion(error)
      })
      child.once('close', (exitCode) => {
        if (settled) return
        settled = true
        if (exitCode === 0) resolveCompletion()
        else {
          const error = new Error(
            `SuiteCut capture FFmpeg exited with ${String(exitCode)}: ${Buffer.concat(stderr).toString('utf8')}`,
          )
          rejectFirstFrame(error)
          rejectCompletion(error)
        }
      })
    })
    void completion.catch(() => undefined)
    child.stdin.on('error', () => undefined)
    const recorder: StreamingRecorder = {
      child,
      completion,
      firstFrame,
      stdin: child.stdin,
      writeQueue: Promise.resolve(),
      lastFrameNumber: -1,
      closing: false,
      excludedDurationMs: 0,
      ...(capturePausedAtMonotonicMs === undefined ? {} : { pausedAtEpochMs: Date.now() }),
    }
    screencasts.set(pageId, activeScreencast)
    streamingRecorders.set(pageId, recorder)

    try {
      await raceWithAbort(
        playwrightPage.screencast.start({
          size: captureSize,
          quality: parsedCaptureOptions.quality ?? 100,
          onFrame: async ({ data, timestamp }) => {
            if (recorder.closing || recorder.pausedAtEpochMs !== undefined) return
            if (pageId === activePageId) liveStream?.update(data)
            recorder.writeQueue = recorder.writeQueue.then(async () => {
              if (recorder.closing || recorder.pausedAtEpochMs !== undefined) return
              const isFirstFrame = recorder.firstFrameEpochMs === undefined
              recorder.firstFrameEpochMs ??= timestamp
              activeScreencast.firstFrameEpochMs ??= timestamp
              activeScreencast.sourceStartedAtMs ??= Math.max(
                0,
                deriveSourceStartedAt(timestamp, originEpochMs) - excludedCaptureDurationMs,
              )
              const frameNumber = Math.max(
                0,
                Math.floor(
                  (timestamp - recorder.firstFrameEpochMs - recorder.excludedDurationMs) /
                    captureFrameDurationMs,
                ),
              )
              if (frameNumber <= recorder.lastFrameNumber) {
                recorder.lastFrame = data
                return
              }
              if (recorder.lastFrame !== undefined) {
                for (
                  let current = recorder.lastFrameNumber + 1;
                  current < frameNumber;
                  current += 1
                ) {
                  if (!recorder.stdin.write(recorder.lastFrame)) await once(recorder.stdin, 'drain')
                }
              }
              if (!recorder.stdin.write(data)) await once(recorder.stdin, 'drain')
              recorder.lastFrame = data
              recorder.lastFrameNumber = frameNumber
              if (isFirstFrame) resolveFirstFrame()
            })
            await recorder.writeQueue
          },
        }),
        startSignal,
      )
      let initialFrameTimer: ReturnType<typeof setTimeout> | undefined
      try {
        await raceWithAbort(
          Promise.race([
            recorder.firstFrame,
            new Promise<never>((_, reject) => {
              initialFrameTimer = setTimeout(() => {
                reject(
                  new Error(
                    `SuiteCut did not receive an initial screencast frame for page ${pageId} within ${String(INITIAL_FRAME_TIMEOUT_MS)}ms`,
                  ),
                )
              }, INITIAL_FRAME_TIMEOUT_MS)
            }),
          ]),
          startSignal,
        )
      } finally {
        if (initialFrameTimer !== undefined) clearTimeout(initialFrameTimer)
      }
      return true
    } catch (error) {
      screencasts.delete(pageId)
      streamingRecorders.delete(pageId)
      recorder.closing = true
      await raceWithAbort(
        playwrightPage.screencast.stop(),
        AbortSignal.timeout(CAPTURE_FORCE_KILL_AFTER_MS),
      ).catch(() => undefined)
      recorder.stdin.end()
      await waitForProcessExit(recorder.child, recorder.completion, {
        gracefulTimeoutMs: CAPTURE_GRACEFUL_SHUTDOWN_TIMEOUT_MS,
        forceKillAfterMs: CAPTURE_FORCE_KILL_AFTER_MS,
      }).catch(() => undefined)
      await rm(outputPath, { force: true }).catch(() => undefined)
      if (startSignal.aborted || playwrightPage.isClosed()) return false
      throw new Error(
        `SuiteCut could not start the screencast for page ${pageId}. Keep Playwright video disabled.`,
        { cause: error },
      )
    }
  }

  const stopScreencast = async (pageId: SuiteCutPageId): Promise<void> => {
    const activeScreencast = screencasts.get(pageId)
    if (activeScreencast === undefined) return
    screencasts.delete(pageId)
    if (streamOnly) {
      latestFrames.delete(pageId)
      try {
        await raceWithAbort(
          activeScreencast.page.screencast.stop(),
          AbortSignal.timeout(CAPTURE_GRACEFUL_SHUTDOWN_TIMEOUT_MS),
        )
      } catch (error) {
        if (!activeScreencast.page.isClosed()) throw error
      }
      return
    }
    const recorder = streamingRecorders.get(pageId)
    streamingRecorders.delete(pageId)
    if (recorder === undefined) throw new Error(`SuiteCut recorder is missing for page ${pageId}`)
    recorder.closing = true

    const shutdownErrors: Error[] = []
    const shutdownStartedAt = performance.now()
    const gracefulSignal = AbortSignal.timeout(CAPTURE_GRACEFUL_SHUTDOWN_TIMEOUT_MS)
    try {
      await raceWithAbort(
        (async () => {
          try {
            await activeScreencast.page.screencast.stop()
          } catch (error) {
            if (!activeScreencast.page.isClosed()) throw error
          }
          await recorder.writeQueue
          if (
            recorder.lastFrame !== undefined &&
            activeScreencast.sourceStartedAtMs !== undefined
          ) {
            const finalFrameNumber = Math.floor(
              Math.max(0, now() - activeScreencast.sourceStartedAtMs) / captureFrameDurationMs,
            )
            for (
              let current = recorder.lastFrameNumber + 1;
              current <= finalFrameNumber;
              current += 1
            ) {
              if (!recorder.stdin.write(recorder.lastFrame)) await once(recorder.stdin, 'drain')
            }
          }
        })(),
        gracefulSignal,
      )
    } catch (error) {
      shutdownErrors.push(
        error instanceof Error ? error : new Error('The browser rejected capture shutdown'),
      )
    } finally {
      delete recorder.lastFrame
      recorder.stdin.end()
    }
    try {
      const elapsedMs = performance.now() - shutdownStartedAt
      await waitForProcessExit(recorder.child, recorder.completion, {
        gracefulTimeoutMs: Math.max(1, Math.ceil(CAPTURE_GRACEFUL_SHUTDOWN_TIMEOUT_MS - elapsedMs)),
        forceKillAfterMs: CAPTURE_FORCE_KILL_AFTER_MS,
      })
    } catch (error) {
      shutdownErrors.push(error instanceof Error ? error : new Error(String(error)))
    }
    const firstShutdownError = shutdownErrors.at(0)
    if (shutdownErrors.length === 1 && firstShutdownError !== undefined) {
      throw firstShutdownError
    }
    if (shutdownErrors.length > 1) {
      throw new AggregateError(
        shutdownErrors,
        `Failed to stop SuiteCut recording for page ${pageId}`,
      )
    }
    await stat(activeScreencast.outputPath)

    if (activeScreencast.firstFrameEpochMs === undefined) {
      throw new Error(`SuiteCut did not receive a first frame for page ${pageId}`)
    }

    await output.attach(activeScreencast.attachmentName, activeScreencast.outputPath, 'video/webm')
    videos.push({
      artifactId: activeScreencast.artifactId,
      pageId,
      attachmentName: activeScreencast.attachmentName,
      firstFrameEpochMs: activeScreencast.firstFrameEpochMs,
      sourceStartedAtMs: activeScreencast.sourceStartedAtMs ?? 0,
    })
  }

  const registerPage = (playwrightPage: Page, kind: SuiteCutPage['kind']): SuiteCutPageId => {
    const existingPageId = pageIds.get(playwrightPage)
    if (existingPageId !== undefined) return existingPageId
    if (!active)
      throw new Error('Cannot register a Playwright page after the recording session is sealed')

    const pageId = randomUUID()
    const recordedPage: SuiteCutPage = {
      id: pageId,
      kind,
      initialUrl: playwrightPage.url(),
      createdAtMs: kind === 'main' ? 0 : now(),
    }

    pageIds.set(playwrightPage, pageId)
    livePages.set(pageId, playwrightPage)
    pages.set(pageId, recordedPage)
    const captureController = new AbortController()
    pageCaptureControllers.set(playwrightPage, captureController)
    const startSignal =
      signal === undefined
        ? captureController.signal
        : AbortSignal.any([captureController.signal, signal])
    void queuePageCaptureTask(pageId, async () => {
      const captured = await startScreencast(playwrightPage, pageId, startSignal)
      if (captured) return

      pages.delete(pageId)
      pageIds.delete(playwrightPage)
      livePages.delete(pageId)
      for (let index = events.length - 1; index >= 0; index -= 1) {
        if (events[index]?.pageId === pageId) events.splice(index, 1)
      }
    })

    let pageFinished = false
    const onClose = (): void => {
      if (pageFinished) return
      pageFinished = true
      pageCloseListeners.delete(playwrightPage)
      playwrightPage.off('close', onClose)
      playwrightPage.off('crash', onClose)
      const documentListener = pageDocumentListeners.get(playwrightPage)
      if (documentListener !== undefined) {
        playwrightPage.off('domcontentloaded', documentListener)
        playwrightPage.off('load', documentListener)
        pageDocumentListeners.delete(playwrightPage)
      }
      captureController.abort(new Error(`Playwright page ${pageId} closed during capture`))
      pageCaptureControllers.delete(playwrightPage)
      pageLayoutScales.delete(playwrightPage)
      livePages.delete(pageId)
      void queuePageCaptureTask(pageId, async () => {
        await stopScreencast(pageId)
        if (streamOnly) {
          pages.delete(pageId)
          pageIds.delete(playwrightPage)
        }
      })
      if (active && recordedPage.closedAtMs === undefined) {
        recordedPage.closedAtMs = now()
      }

      if (active && activePageId === pageId) {
        const openerPageId = recordedPage.openerPageId
        if (openerPageId !== undefined && livePages.has(openerPageId)) {
          selectActivePage(openerPageId, 'closed')
        } else if (livePages.has(mainPageId)) {
          selectActivePage(mainPageId, 'closed')
        } else {
          const fallbackPageId = livePages.keys().next().value
          if (fallbackPageId !== undefined) selectActivePage(fallbackPageId, 'closed')
        }
      }
    }

    pageCloseListeners.set(playwrightPage, onClose)
    playwrightPage.once('close', onClose)
    playwrightPage.once('crash', onClose)
    const onDocumentLoaded = (): void => {
      const layoutScale = pageLayoutScales.get(playwrightPage) ?? 1
      void trackPageTask(
        Promise.allSettled([
          ...(layoutScale === 1
            ? []
            : [playwrightPage.evaluate(applySuiteCutLayoutScale, layoutScale)]),
          ...playwrightPage.frames().map(async (frame) => {
            await frame.evaluate(removeActivePageListeners, markerName)
            await frame.evaluate(installActivePageListeners, listenerOptions)
          }),
        ]).then(() => undefined),
      )
    }
    pageDocumentListeners.set(playwrightPage, onDocumentLoaded)
    playwrightPage.on('domcontentloaded', onDocumentLoaded)
    playwrightPage.on('load', onDocumentLoaded)

    if (kind !== 'main') {
      void trackPageTask(
        (async () => {
          const opener = await playwrightPage.opener()
          if (opener === null) {
            recordedPage.kind = 'secondary'
            return
          }

          recordedPage.kind = 'popup'
          const openerPageId =
            pageIds.get(opener) ?? registerPage(opener, opener === page ? 'main' : 'secondary')
          recordedPage.openerPageId = openerPageId
        })(),
      )
    }

    return pageId
  }

  const onContextPage = (newPage: Page): void => {
    selectActivePage(registerPage(newPage, newPage === page ? 'main' : 'secondary'), 'opened')
  }

  let bindingResource: AsyncDisposeResource | undefined
  let initScriptResource: AsyncDisposeResource | undefined

  const detachPageLifecycle = (): void => {
    context.off('page', onContextPage)
    for (const [playwrightPage, closeListener] of pageCloseListeners) {
      playwrightPage.off('close', closeListener)
      playwrightPage.off('crash', closeListener)
    }
    pageCloseListeners.clear()
    for (const [playwrightPage, documentListener] of pageDocumentListeners) {
      playwrightPage.off('domcontentloaded', documentListener)
      playwrightPage.off('load', documentListener)
    }
    pageDocumentListeners.clear()
    for (const controller of pageCaptureControllers.values()) {
      controller.abort(new Error('SuiteCut page capture is ending'))
    }
    pageCaptureControllers.clear()
    pageLayoutScales.clear()
  }

  const stopAllPageCaptures = async (): Promise<void> => {
    const pageIdsToStop = new Set([
      ...pages.keys(),
      ...screencasts.keys(),
      ...pageCaptureTasks.keys(),
    ])
    await Promise.all(
      [...pageIdsToStop].map((pageId) =>
        queuePageCaptureTask(pageId, () => stopScreencast(pageId)),
      ),
    )
    await drainPageTasks()
  }

  const createCheckpointClips = async (): Promise<void> => {
    for (const checkpoint of pendingCheckpointClips) {
      const source = recordedScreencasts.get(checkpoint.event.pageId)
      const timing = videos.find((video) => video.pageId === checkpoint.event.pageId)
      if (source === undefined || timing === undefined) {
        throw new Error(`SuiteCut checkpoint ${checkpoint.event.id} has no source video`)
      }

      const sourceAtMs = Math.max(0, checkpoint.event.atMs - timing.sourceStartedAtMs)
      const durationSeconds = (checkpoint.event.durationMs / 1_000).toFixed(6)
      const result = await runProcess(
        ffmpegPath,
        [
          '-loglevel',
          'error',
          '-ss',
          (sourceAtMs / 1_000).toFixed(6),
          '-i',
          source.outputPath,
          '-an',
          '-vf',
          `setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=${durationSeconds},trim=duration=${durationSeconds},fps=${String(captureFramesPerSecond)}`,
          '-c:v',
          'libvpx-vp9',
          '-crf',
          '18',
          '-b:v',
          '0',
          '-deadline',
          'good',
          '-cpu-used',
          '2',
          '-row-mt',
          '1',
          '-threads',
          '8',
          '-y',
          checkpoint.outputPath,
        ],
        {
          ...(signal === undefined ? {} : { signal }),
          timeoutMs: 60_000,
          forceKillAfterMs: CAPTURE_FORCE_KILL_AFTER_MS,
        },
      )
      if (result.exitCode !== 0) {
        throw new Error(
          `SuiteCut could not create checkpoint clip ${checkpoint.event.id}: ${result.stderr.trim()}`,
        )
      }
      await stat(checkpoint.outputPath)
      await output.attach(checkpoint.attachmentName, checkpoint.outputPath, 'video/webm')
      artifacts.push({
        id: checkpoint.event.artifactId,
        attachmentName: checkpoint.attachmentName,
        role: 'checkpoint',
        contentType: 'video/webm',
        capturedAtMs: checkpoint.event.atMs,
        durationMs: checkpoint.event.durationMs,
        pageId: checkpoint.event.pageId,
      })
    }
  }

  const disposePageInstrumentation = async (): Promise<void> => {
    const currentFrames = context.pages().flatMap((playwrightPage) => playwrightPage.frames())
    await Promise.allSettled(
      currentFrames.map((frame) => frame.evaluate(removeActivePageListeners, markerName)),
    )

    const resources = [initScriptResource, bindingResource].filter(
      (resource): resource is AsyncDisposeResource => resource !== undefined,
    )
    initScriptResource = undefined
    bindingResource = undefined
    const disposalResults = await Promise.allSettled(
      resources.map((resource) => resource.dispose()),
    )
    for (const result of disposalResults) {
      if (result.status === 'rejected') {
        pageTaskErrors.push(
          result.reason instanceof Error ? result.reason : new Error(String(result.reason)),
        )
      }
    }
  }

  const settleLifecycle = async (operations: readonly Promise<void>[]): Promise<void> => {
    const results = await Promise.allSettled(operations)
    for (const result of results) {
      if (result.status === 'rejected') {
        pageTaskErrors.push(
          result.reason instanceof Error ? result.reason : new Error(String(result.reason)),
        )
      }
    }
  }

  context.on('page', onContextPage)
  try {
    mainPageId = registerPage(page, 'main')
    activePageId = mainPageId

    for (const existingPage of context.pages()) {
      registerPage(existingPage, existingPage === page ? 'main' : 'secondary')
    }

    const pointerButton = (button: number): SuiteCutPointerButton => {
      switch (button) {
        case 0:
          return 'left'
        case 1:
          return 'middle'
        case 2:
          return 'right'
        case 3:
          return 'back'
        case 4:
          return 'forward'
        default:
          return 'none'
      }
    }

    bindingResource = await context.exposeBinding(
      bindingName,
      ({ page: sourcePage }, payload?: CapturedPointerPayload) => {
        if (!active) return

        const sourcePageId = pageIds.get(sourcePage)
        if (sourcePageId !== undefined && livePages.has(sourcePageId)) {
          selectActivePage(sourcePageId, 'interaction')
        }
        if (streamOnly || sourcePageId === undefined || payload === undefined) return
        if (!['pointer-move', 'pointer-down', 'pointer-up', 'click'].includes(payload.type)) return

        const geometry = parseCapturedGeometry(
          payload.targetRect ?? { x: payload.x, y: payload.y, width: 0, height: 0 },
          payload.viewport,
        )
        const eventBase = {
          id: randomUUID(),
          atMs: now(),
          pageId: sourcePageId,
          point: { x: payload.x, y: payload.y },
          pointerType: ['mouse', 'pen', 'touch'].includes(payload.pointerType)
            ? (payload.pointerType as 'mouse' | 'pen' | 'touch')
            : 'mouse',
          viewport: geometry.viewport,
          targetRect: geometry.rect,
        }
        if (payload.type === 'pointer-move') {
          if (!streamOnly)
            events.push({ ...eventBase, type: payload.type } satisfies SuiteCutPointerMoveEvent)
        } else {
          if (!streamOnly)
            events.push({
              ...eventBase,
              type: payload.type,
              button: pointerButton(payload.button),
            } satisfies SuiteCutPointerButtonEvent)
        }
      },
    )
    initScriptResource = await context.addInitScript(installActivePageListeners, listenerOptions)

    const existingFrames = context.pages().flatMap((playwrightPage) => playwrightPage.frames())
    await Promise.allSettled(
      existingFrames.map((frame) => frame.evaluate(installActivePageListeners, listenerOptions)),
    )
    await drainPageTasks()
    signal?.throwIfAborted()
    if (pageTaskErrors.length > 0) {
      throw new AggregateError(pageTaskErrors, 'Failed to start SuiteCut page recording')
    }
    if (parsedCaptureOptions.stream !== undefined) {
      const layout = resolveCaptureLayout(
        parsedCaptureOptions,
        page.viewportSize() ?? DEFAULT_SUITE_CUT_VIEWPORT,
      )
      liveStream = createLiveStream(
        ffmpegPath,
        parsedCaptureOptions.stream,
        captureFramesPerSecond,
        layout.captureSize,
        signal,
        (frame) => liveCamera.render(frame, activePageId),
      )
      const frame =
        latestFrames.get(activePageId) ?? streamingRecorders.get(activePageId)?.lastFrame
      if (frame !== undefined) liveStream.update(frame)
      await liveStream.ready()
    }
  } catch (error) {
    active = false
    liveCamera.stop()
    detachPageLifecycle()
    await settleLifecycle([
      disposePageInstrumentation(),
      stopAllPageCaptures(),
      liveStream?.stop() ?? Promise.resolve(),
    ])
    livePages.clear()
    throw new AggregateError(
      [error instanceof Error ? error : new Error(String(error)), ...pageTaskErrors],
      'Failed to initialize SuiteCut page recording',
      { cause: error },
    )
  }

  const endRecording = (): Promise<void> => {
    if (endingPromise !== undefined) return endingPromise
    endedAtMs = now()
    active = false
    liveCamera.stop()
    detachPageLifecycle()

    endingPromise = (async () => {
      try {
        await settleLifecycle([
          disposePageInstrumentation(),
          stopAllPageCaptures(),
          liveStream?.stop() ?? Promise.resolve(),
        ])
        await drainPageTasks()
        if (pageTaskErrors.length > 0) {
          throw new AggregateError(
            pageTaskErrors,
            'Failed to register one or more Playwright pages',
          )
        }
        await createCheckpointClips()
      } finally {
        livePages.clear()
      }
    })()
    return endingPromise
  }

  const seal = async (): Promise<SuiteCutEventAttachment> => {
    await endRecording()
    if (endedAtMs === undefined) throw new Error('SuiteCut recording did not capture its end time')
    return {
      attemptId,
      clock,
      endedAtMs,
      pages: streamOnly ? [] : structuredClone([...pages.values()]),
      events: structuredClone(events),
      artifacts: structuredClone(artifacts),
      videos: structuredClone(videos),
    }
  }

  const session: SuiteCutRecordingSession = {
    ...(liveStream === undefined ? {} : { streamFailure: liveStream.failure }),
    ...(parsedCaptureOptions.stream === undefined ? {} : { playLiveZoom: liveCamera.zoom }),
    retainArtifacts: !streamOnly,
    attemptId,
    clock,
    pages,
    get activePageId() {
      return activePageId
    },
    events,
    artifacts,
    screencasts,
    videos,
    now,
    pageFor: (pageId: SuiteCutPageId) => {
      const playwrightPage = livePages.get(pageId)
      if (playwrightPage !== undefined) return playwrightPage
      if (pages.has(pageId)) {
        throw new Error('Playwright page is closed or the recording session is sealed')
      }
      throw new Error('Page ID is not registered in this recording session')
    },
    pageIdFor: (playwrightPage: Page) => {
      const pageId = pageIds.get(playwrightPage)
      if (pageId === undefined) {
        throw new Error('Playwright page is not registered in this recording session')
      }
      return pageId
    },
    selectPage: (playwrightPage: Page) => {
      const pageId = pageIds.get(playwrightPage)
      if (pageId === undefined) {
        throw new Error('Playwright page is not registered in this recording session')
      }
      if (!livePages.has(pageId)) {
        throw new Error('Cannot select a closed Playwright page')
      }
      if (!active) {
        throw new Error('Cannot select a page after the recording session is sealed')
      }

      return selectActivePage(pageId, 'author')
    },
    nextEventId: () => randomUUID(),
    record: (event) => {
      if (!active) throw new Error('Cannot record an event after the recording session is sealed')
      if (!streamOnly) events.push(event)
    },
    captureCheckpoint: async (event) => {
      if (!active) throw new Error('Cannot capture a checkpoint after recording has ended')
      const checkpointPage = livePages.get(event.pageId)
      if (checkpointPage === undefined) {
        throw new Error('Cannot capture a checkpoint from a closed Playwright page')
      }
      if (streamOnly) {
        await raceWithAbort(checkpointPage.waitForTimeout(event.durationMs), signal)
        return
      }
      const attachmentName = `suitecut-checkpoint-${attemptId}-${event.artifactId}.webm`
      pendingCheckpointClips.push({
        event,
        attachmentName,
        outputPath: output.pathFor(attachmentName),
      })
      if (!streamOnly) events.push(event)
      await raceWithAbort(checkpointPage.waitForTimeout(event.durationMs), signal)
    },
    withCapturePaused,
    endRecording,
    seal: () => {
      sealedAttachment ??= seal()
      return sealedAttachment
    },
  }

  return session
}

export function createSuiteCutFixture(
  session: SuiteCutRecordingSession,
  narration: SuiteCutNarrationPipeline,
  captureOptions: SuiteCutCaptureOptions,
): SuiteCutFixture {
  let zoomQueue = Promise.resolve()

  const createPageEventBase = (
    pageId: SuiteCutPageId = session.activePageId,
  ): SuiteCutPageEventBase => ({
    id: session.nextEventId(),
    atMs: session.now(),
    pageId,
  })

  const captureLocatorGeometry = async (
    locator: Locator,
    operation: 'highlight' | 'zoom' | 'hover' | 'click',
    geometryMode: 'element' | 'content' = 'element',
  ) => {
    const page = locator.page()
    const pageId = session.selectPage(page)
    const [rect, viewport] = await Promise.all([
      geometryMode === 'element'
        ? locator.boundingBox()
        : locator.evaluate(readSuiteCutLocatorRect, geometryMode),
      page.evaluate(readSuiteCutViewport),
    ])

    if (rect === null) {
      throw new Error(
        `SuiteCut cannot ${operation} ${locator.toString()}: the locator is not visible or has no bounding box`,
      )
    }

    const geometry = parseCapturedGeometry(rect, viewport)
    return { pageId, ...geometry }
  }

  const suitecut: SuiteCutFixture = {
    selectPage: (page: Page) => {
      session.selectPage(page)
    },
    narrate: async (text: string, options?: SuiteCutNarrationOptions) => {
      const input = parseNarrationInput(text, options)

      const event: SuiteCutNarrationEvent = {
        ...createPageEventBase(),
        type: 'narration',
        text: input.text,
      }

      if (input.options?.provider !== undefined) {
        event.provider = input.options.provider
      }

      if (input.options?.voice !== undefined) {
        event.voice = input.options.voice
      }

      if (input.options?.speed !== undefined) {
        event.speed = input.options.speed
      }

      if (input.options?.caption !== undefined) {
        event.caption = input.options.caption
      }

      const clip = await session.withCapturePaused(() => narration.enqueue(event))
      event.atMs = session.now()
      session.record(event)
      const page = session.pageFor(event.pageId)
      const caption = event.caption ?? event.text
      if (caption.length > 0) {
        const words = clip.timing?.text === caption ? clip.timing.words : undefined
        await showPresentationCaption(page, caption, clip.durationMs, words)
      } else {
        await page.waitForTimeout(clip.durationMs)
      }
      const narrationTailMs = captureOptions.narrationTailMs ?? 350
      if (narrationTailMs > 0) await page.waitForTimeout(narrationTailMs)
    },
    checkpoint: async (label: string, options?: SuiteCutCheckpointOptions) => {
      const input = parseCheckpointInput(label, options)
      const pageId = session.activePageId
      const page = session.pageFor(pageId)
      const artifactId = randomUUID()
      const viewport = parseCapturedViewport(await page.evaluate(readSuiteCutViewport))
      const capturedAtMs = session.now()
      const event: SuiteCutCheckpointEvent = {
        id: session.nextEventId(),
        type: 'checkpoint',
        atMs: capturedAtMs,
        pageId,
        label: input.label,
        artifactId,
        durationMs: input.options?.durationMs ?? DEFAULT_SUITE_CUT_CHECKPOINT_DURATION_MS,
        viewport,
      }
      await session.captureCheckpoint(event)
    },
    hold: async (durationMs: Milliseconds) => {
      const parsedDurationMs = parseHoldInput(durationMs)

      const event: SuiteCutHoldEvent = {
        ...createPageEventBase(),
        type: 'hold',
        durationMs: parsedDurationMs,
        reason: 'author',
      }

      session.record(event)
      await session.pageFor(event.pageId).waitForTimeout(parsedDurationMs)
    },
    highlight: async (locator: Locator, options: SuiteCutHighlightOptions = {}) => {
      const parsedOptions = parseHighlightOptions(options)
      const { pageId, rect, viewport } = await captureLocatorGeometry(
        locator,
        'highlight',
        parsedOptions.geometry ?? 'element',
      )

      const event: SuiteCutHighlightEvent = {
        ...createPageEventBase(pageId),
        type: 'highlight',
        rect,
        viewport,
        options: parsedOptions,
      }
      session.record(event)
      await showPresentationHighlight(
        session.pageFor(pageId),
        rect,
        parsedOptions,
        parsedOptions.durationMs ?? 1_200,
      )
    },
    zoom: (locator: Locator, options: SuiteCutZoomOptions = {}) => {
      const runZoom = async () => {
        const parsedOptions = parseZoomOptions(options)
        await locator.scrollIntoViewIfNeeded()
        const { pageId, rect, viewport } = await captureLocatorGeometry(
          locator,
          'zoom',
          parsedOptions.geometry ?? 'element',
        )

        const event: SuiteCutZoomEvent = {
          ...createPageEventBase(pageId),
          type: 'zoom',
          rect,
          viewport,
          options: parsedOptions,
        }
        session.record(event)
        const resolvedZoom = resolveSuiteCutZoomOptions(parsedOptions)
        if (session.playLiveZoom !== undefined) {
          await session.playLiveZoom(event)
        } else {
          await session.pageFor(pageId).waitForTimeout(resolvedZoom.totalDurationMs)
        }
      }
      const pending = zoomQueue.then(runZoom, runZoom)
      zoomQueue = pending.catch(() => undefined)
      return pending
    },
    hover: async (locator: Locator, options: SuiteCutPointerActionOptions = {}) => {
      const parsedOptions = parsePointerActionOptions(options)
      await locator.scrollIntoViewIfNeeded()
      const { pageId, rect } = await captureLocatorGeometry(locator, 'hover')
      const page = session.pageFor(pageId)
      await movePresentationCursor(
        page,
        { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 },
        parsedOptions.moveDurationMs ?? 450,
      )
      await locator.hover()
      if (parsedOptions.waitForAnimations !== false) {
        await waitForPresentationAnimations(page, parsedOptions.animationTimeoutMs ?? 2_000)
      }
      const settleMs = parsedOptions.settleMs ?? 120
      if (settleMs > 0) await page.waitForTimeout(settleMs)
    },
    click: async (locator: Locator, options: SuiteCutPointerActionOptions = {}) => {
      const parsedOptions = parsePointerActionOptions(options)
      await locator.scrollIntoViewIfNeeded()
      const { pageId, rect } = await captureLocatorGeometry(locator, 'click')
      const page = session.pageFor(pageId)
      await movePresentationCursor(
        page,
        { x: rect.x + rect.width / 2, y: rect.y + rect.height / 2 },
        parsedOptions.moveDurationMs ?? 450,
      )
      await locator.click()

      const waits: Promise<void>[] = [pulsePresentationCursor(page, 280)]
      if (parsedOptions.waitForAnimations !== false) {
        waits.push(waitForPresentationAnimations(page, parsedOptions.animationTimeoutMs ?? 2_000))
      }
      await Promise.all(waits)
      const settleMs = parsedOptions.settleMs ?? 120
      if (settleMs > 0) await page.waitForTimeout(settleMs)
    },
    type: async (locator: Locator, text: string, options: SuiteCutTypeOptions = {}) => {
      const input = parseTypeInput(text, options)
      const page = locator.page()
      session.selectPage(page)
      await locator.scrollIntoViewIfNeeded()
      if (input.options?.clearExisting !== false) await locator.fill('')
      await locator.pressSequentially(input.text, {
        delay: input.options?.delayMs ?? 70,
      })
      const settleMs = input.options?.settleMs ?? 250
      if (settleMs > 0) await page.waitForTimeout(settleMs)
    },
    scrollTo: async (locator: Locator, options: SuiteCutScrollOptions = {}) => {
      const parsedOptions = parseScrollOptions(options)
      session.selectPage(locator.page())
      await scrollLocator(locator, parsedOptions)
    },
    scrollTop: async (options: SuiteCutScrollOptions = {}) => {
      const parsedOptions = parseScrollOptions(options)
      await scrollPageTop(session.pageFor(session.activePageId), parsedOptions)
    },
  }
  return suitecut
}
