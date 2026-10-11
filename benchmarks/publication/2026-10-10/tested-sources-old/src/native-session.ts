import { randomUUID } from 'node:crypto'
import { mkdir } from 'node:fs/promises'
import { dirname } from 'node:path'

import { raceWithAbort } from './abort.js'
import { type SuiteCutArtifactSink } from './artifact-sink.js'
import { type NativeAudioPacket, type NativeBrowser } from './native-browser.js'
import { createNativePage, type NativePage } from './native-page.js'
import { createNativeVideoRecorder } from './native-recorder.js'
import { removeSuiteCutPresentation } from './presentation.js'
import { runProcess } from './process.js'
import { type SuiteCutCaptureOptions } from './schemas.js'
import {
  type SuiteCutCheckpointEvent,
  type SuiteCutEventAttachment,
  type SuiteCutRecordingSession,
} from './types.js'

export async function createNativeRecordingSession(options: {
  source: NativeBrowser
  output: SuiteCutArtifactSink
  ffmpegPath: string
  capture: SuiteCutCaptureOptions & { audio: boolean }
  signal?: AbortSignal
}): Promise<{
  session: SuiteCutRecordingSession<NativePage>
  page: NativePage
  addSource: (source: NativeBrowser) => Promise<NativePage>
}> {
  const { output, signal, capture } = options
  const originEpochMs = Date.now()
  const originMonotonicMs = performance.now()
  const clock = {
    originEpochMs,
    originMonotonicMs,
    startedAt: new Date(originEpochMs).toISOString(),
  }
  const attemptId = randomUUID()
  const pages: SuiteCutRecordingSession<NativePage>['pages'] = new Map()
  const videos: SuiteCutRecordingSession<NativePage>['videos'] = []
  const artifacts: SuiteCutRecordingSession<NativePage>['artifacts'] = []
  const events: SuiteCutRecordingSession<NativePage>['events'] = []
  const entries = new Map<
    NativePage,
    {
      pageId: string
      artifactId: string
      attachmentName: string
      outputPath: string
      recorder: ReturnType<typeof createNativeVideoRecorder>
      unsubscribeFrame(): void
      unsubscribeAudio(): void
      latest?: Buffer
      startedAtMs: number
      startedAtMonotonicMs: number
      lastReceivedAtMs: number
      firstFrameEpochMs?: number
    }
  >()
  const sources = new Map<NativeBrowser, Promise<NativePage>>()
  const checkpoints: SuiteCutCheckpointEvent[] = []
  let selected = ''
  let active = true
  let ending: Promise<void> | undefined
  let sealed: SuiteCutEventAttachment | undefined
  let pausedAt: number | undefined
  let pauseDepth = 0
  let excluded = 0
  let endedAtMs = 0
  let rejectFailure: (error: Error) => void = () => undefined
  const failure = new Promise<never>((_, reject) => {
    rejectFailure = reject
  })
  void failure.catch(() => undefined)
  const now = () => (pausedAt ?? performance.now()) - originMonotonicMs - excluded
  const assertActive = () => {
    signal?.throwIfAborted()
    if (!active) throw new Error('Native recording has ended')
  }
  const frameTime = (timestampMs: number) => Math.max(0, timestampMs - originMonotonicMs - excluded)
  const beginSource = async (source: NativeBrowser): Promise<NativePage> => {
    assertActive()
    const page = createNativePage(source, signal)
    const pageId = randomUUID()
    const artifactId = randomUUID()
    const attachmentName = `suitecut-source-${attemptId}-${artifactId}.webm`
    const outputPath = output.pathFor(attachmentName)
    await mkdir(dirname(outputPath), { recursive: true })
    assertActive()
    const initialUrl = await page.evaluate(() => location.href)
    const recorder = createNativeVideoRecorder({
      source,
      outputPath,
      ffmpegPath: options.ffmpegPath,
      size: capture.size ?? {
        width: source.width - (source.width % 2),
        height: source.height - (source.height % 2),
      },
      audio: capture.audio,
    })
    const entry: typeof entries extends Map<NativePage, infer T> ? T : never = {
      pageId,
      artifactId,
      attachmentName,
      outputPath,
      recorder,
      startedAtMs: now(),
      startedAtMonotonicMs: performance.now(),
      lastReceivedAtMs: performance.now(),
      unsubscribeFrame: () => undefined,
      unsubscribeAudio: () => undefined,
    }
    entries.set(page, entry)
    pages.set(pageId, {
      id: pageId,
      kind: selected ? 'secondary' : 'main',
      initialUrl,
      createdAtMs: now(),
    })
    selected ||= pageId
    let received = () => undefined as void
    const first = new Promise<void>((resolve) => {
      received = resolve
    })
    entry.unsubscribeFrame = source.onFrame((frame) => {
      if (!active || pauseDepth) return
      entry.firstFrameEpochMs ??= Math.max(
        originEpochMs,
        originEpochMs + Math.max(entry.startedAtMonotonicMs, frame.timestampMs) - originMonotonicMs,
      )
      entry.lastReceivedAtMs = performance.now()
      entry.latest = frame.data
      recorder.push(frame.data, Math.max(entry.startedAtMs, frameTime(frame.timestampMs)))
      received()
    })
    if (capture.audio) {
      entry.unsubscribeAudio = source.onAudio((packet: NativeAudioPacket) => {
        if (!active || pauseDepth) return
        recorder.pushAudio(packet.data, Math.max(entry.startedAtMs, frameTime(packet.timestampMs)))
      })
    }
    void recorder.failure.catch((error: Error) => {
      if (active) rejectFailure(error)
    })
    void source.failure.catch((error: Error) => {
      if (active) rejectFailure(error)
    })
    void source.closed.then(() => {
      if (active) rejectFailure(new Error('Native source closed during recording'))
    })
    await raceWithAbort(
      Promise.race([first, failure]),
      AbortSignal.any([AbortSignal.timeout(30_000), ...(signal ? [signal] : [])]),
    )
    return page
  }
  const addSource = (source: NativeBrowser): Promise<NativePage> => {
    assertActive()
    if (pauseDepth) throw new Error('Cannot add a native source while recording is paused')
    const existing = sources.get(source)
    if (existing) return existing
    const pending = beginSource(source)
    sources.set(source, pending)
    return pending
  }
  const timer = setInterval(() => {
    if (!active || pauseDepth) return
    for (const entry of entries.values())
      if (entry.latest && performance.now() - entry.lastReceivedAtMs > 100)
        entry.recorder.push(entry.latest, now())
  }, 100)
  const endRecording = (): Promise<void> =>
    (ending ??= (async () => {
      endedAtMs = now()
      active = false
      clearInterval(timer)
      await Promise.allSettled(sources.values())
      const results = await Promise.allSettled(
        [...entries.entries()].map(async ([page, entry]) => {
          entry.unsubscribeFrame()
          entry.unsubscribeAudio()
          await entry.recorder.stop(endedAtMs)
          const firstAtMs = entry.recorder.firstAtMs
          if (firstAtMs === undefined)
            throw new Error('Native recording has no first-frame timestamp')
          await output.attach(entry.attachmentName, entry.outputPath, 'video/webm')
          videos.push({
            artifactId: entry.artifactId,
            pageId: entry.pageId,
            attachmentName: entry.attachmentName,
            firstFrameEpochMs: entry.firstFrameEpochMs ?? originEpochMs + firstAtMs,
            sourceStartedAtMs: firstAtMs,
          })
          if (!page.isClosed())
            await createNativePage(page.source).evaluate(removeSuiteCutPresentation)
        }),
      )
      const errors = results
        .filter((result) => result.status === 'rejected')
        .map((result) => result.reason as Error)
      if (errors.length) throw new AggregateError(errors, 'Native recording could not finish')
    })())
  const session: SuiteCutRecordingSession<NativePage> = {
    attemptId,
    clock,
    pages,
    events,
    artifacts,
    videos,
    screencasts: new Map(),
    streamFailure: failure,
    get activePageId() {
      return selected
    },
    now,
    pageFor(id) {
      for (const [page, entry] of entries) if (entry.pageId === id) return page
      throw new Error('Native page is not registered')
    },
    pageIdFor(page) {
      const entry = entries.get(page)
      if (!entry) throw new Error('Native page is not registered')
      return entry.pageId
    },
    selectPage(page) {
      assertActive()
      if (page.isClosed()) throw new Error('Native page is closed')
      const id = session.pageIdFor(page)
      if (id !== selected) {
        selected = id
        events.push({
          id: randomUUID(),
          atMs: now(),
          type: 'page-selected',
          pageId: id,
          reason: 'author',
        })
      }
      return id
    },
    nextEventId: randomUUID,
    record(event) {
      assertActive()
      events.push(event)
    },
    async captureCheckpoint(event) {
      assertActive()
      checkpoints.push(event)
      events.push(event)
      await session.pageFor(event.pageId).waitForTimeout(event.durationMs)
    },
    async withCapturePaused(operation) {
      assertActive()
      if (++pauseDepth === 1) {
        pausedAt = performance.now()
        for (const entry of entries.values())
          if (entry.latest) entry.recorder.push(entry.latest, now())
      }
      try {
        return await operation()
      } finally {
        if (--pauseDepth === 0 && pausedAt !== undefined) {
          excluded += performance.now() - pausedAt
          pausedAt = undefined
        }
      }
    },
    endRecording,
    async seal() {
      await endRecording()
      if (sealed) return sealed
      for (const event of checkpoints) {
        const source = [...entries.values()].find((entry) => entry.pageId === event.pageId)
        const video = videos.find((video) => video.pageId === event.pageId)
        if (!source || !video) throw new Error('Native checkpoint has no source video')
        const attachmentName = `suitecut-checkpoint-${attemptId}-${event.artifactId}.webm`
        const path = output.pathFor(attachmentName)
        const duration = (event.durationMs / 1000).toFixed(6)
        const result = await runProcess(
          options.ffmpegPath,
          [
            '-loglevel',
            'error',
            '-ss',
            (Math.max(0, event.atMs - video.sourceStartedAtMs) / 1000).toFixed(6),
            '-i',
            source.outputPath,
            '-an',
            '-vf',
            `setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=${duration},trim=duration=${duration}`,
            '-c:v',
            'libvpx-vp9',
            '-deadline',
            'realtime',
            '-cpu-used',
            '5',
            '-y',
            path,
          ],
          { timeoutMs: 60_000 },
        )
        if (result.exitCode !== 0)
          throw new Error(`Native checkpoint encoding failed: ${result.stderr}`)
        await output.attach(attachmentName, path, 'video/webm')
        artifacts.push({
          id: event.artifactId,
          attachmentName,
          role: 'checkpoint',
          contentType: 'video/webm',
          capturedAtMs: event.atMs,
          durationMs: event.durationMs,
          pageId: event.pageId,
        })
      }
      sealed = {
        attemptId,
        clock,
        endedAtMs,
        pages: [...pages.values()],
        events,
        artifacts,
        videos,
      }
      return sealed
    },
  }
  try {
    return { session, page: await addSource(options.source), addSource }
  } catch (error) {
    await endRecording().catch(() => undefined)
    throw error
  }
}
