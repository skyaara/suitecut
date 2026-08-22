import { spawn } from 'node:child_process'
import { randomUUID } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, stat } from 'node:fs/promises'
import { dirname } from 'node:path'

import { expect as baseExpect, test as baseTest } from '@playwright/test'
import { type Locator, type Page, type TestInfo } from '@playwright/test'

import { SUITECUT_EVENT_ATTACHMENT } from './constants.js'
import {
  type SuiteCutCheckpointOptions,
  type SuiteCutFixture,
  type SuiteCutNarrationOptions,
} from './fixtures.js'
import { createNarrationPipeline, type SuiteCutNarrationPipeline } from './narration.js'
import { resolveFfmpeg } from './process.js'
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
  type SuiteCutHoldEvent,
  type SuiteCutHighlightEvent,
  type SuiteCutHighlightOptions,
  type SuiteCutPointerButton,
  type SuiteCutPointerButtonEvent,
  type SuiteCutPointerMoveEvent,
  type SuiteCutZoomEvent,
} from './types.js'
import {
  parseCapturedGeometry,
  parseCapturedViewport,
  parseCheckpointInput,
  parseHighlightOptions,
  parseHoldInput,
  parseNarrationInput,
  parseZoomOptions,
} from './validation.js'

interface SuiteCutFixtures {
  suitecut: SuiteCutFixture
}

interface ActivePageListenerRegistration {
  dispose(): void
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
  readonly completion: Promise<void>
  readonly stdin: NodeJS.WritableStream
  readonly stderr: Buffer[]
  firstFrameEpochMs?: number
  lastFrame?: Buffer
  lastFrameNumber: number
  closing: boolean
}

function readSuiteCutViewport(): SuiteCutViewport {
  return {
    width: window.innerWidth,
    height: window.innerHeight,
    deviceScaleFactor: window.devicePixelRatio,
    scrollX: window.scrollX,
    scrollY: window.scrollY,
  }
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
        deviceScaleFactor: window.devicePixelRatio,
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

export const test = baseTest.extend<SuiteCutFixtures>({
  suitecut: async ({ page }, use, testInfo) => {
    const session = await createRecordingSession({ page, testInfo })
    const narration = createNarrationPipeline(testInfo, session)
    const suitecut = createSuiteCutFixture(session, testInfo, narration)

    let useError: Error | undefined
    const teardownErrors: Error[] = []
    try {
      await use(suitecut)
    } catch (error) {
      useError =
        error instanceof Error
          ? error
          : new Error('The Playwright test rejected with a non-Error value')
    } finally {
      const [recordingResult, narrationResult] = await Promise.allSettled([
        session.endRecording(),
        narration.finish(),
      ])
      if (recordingResult.status === 'rejected') {
        teardownErrors.push(
          recordingResult.reason instanceof Error
            ? recordingResult.reason
            : new Error(String(recordingResult.reason)),
        )
      }
      if (narrationResult.status === 'rejected') {
        teardownErrors.push(
          narrationResult.reason instanceof Error
            ? narrationResult.reason
            : new Error(String(narrationResult.reason)),
        )
      }
      if (recordingResult.status === 'fulfilled') {
        try {
          const attachment = await session.seal()
          await testInfo.attach(SUITECUT_EVENT_ATTACHMENT, {
            body: Buffer.from(JSON.stringify(attachment)),
            contentType: 'application/json',
          })
        } catch (error) {
          teardownErrors.push(error instanceof Error ? error : new Error(String(error)))
        }
      }
    }
    const failures = useError === undefined ? teardownErrors : [useError, ...teardownErrors]
    const firstFailure = failures.at(0)
    if (failures.length === 1 && firstFailure !== undefined) throw firstFailure
    if (failures.length > 1)
      throw new AggregateError(failures, 'SuiteCut test and recording failed')
  },
})

async function createRecordingSession({
  page,
  testInfo,
}: {
  page: Page
  testInfo: TestInfo
}): Promise<SuiteCutRecordingSession> {
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
  const pendingPageTasks = new Set<Promise<void>>()
  const pageTaskErrors: Error[] = []
  const events: SuiteCutRecordingSession['events'] = []
  const artifacts: SuiteCutRecordingSession['artifacts'] = []
  const screencasts: SuiteCutRecordingSession['screencasts'] = new Map()
  const videos: SuiteCutRecordingSession['videos'] = []
  const streamingRecorders = new Map<SuiteCutPageId, StreamingRecorder>()
  const context = page.context()
  const ffmpegPath = await resolveFfmpeg()
  let activePageId: SuiteCutPageId
  let active = true
  let endedAtMs: Milliseconds | undefined
  let endingPromise: Promise<void> | undefined
  let sealedAttachment: Promise<SuiteCutEventAttachment> | undefined

  const now = (): Milliseconds => performance.now() - originMonotonicMs

  const trackPageTask = (task: Promise<void>): void => {
    const trackedTask = task.catch((error) => {
      pageTaskErrors.push(error instanceof Error ? error : new Error(String(error)))
    })

    pendingPageTasks.add(trackedTask)
    void trackedTask.then(() => {
      pendingPageTasks.delete(trackedTask)
    })
  }

  const startScreencast = async (playwrightPage: Page, pageId: SuiteCutPageId): Promise<void> => {
    const artifactId = randomUUID()
    const attachmentName = `suitecut-source-${artifactId}.webm`
    const outputPath = testInfo.outputPath(attachmentName)
    const activeScreencast: SuiteCutActiveScreencast = {
      pageId,
      page: playwrightPage,
      outputPath,
      attachmentName,
    }
    screencasts.set(pageId, activeScreencast)
    await mkdir(dirname(outputPath), { recursive: true })
    const child = spawn(
      ffmpegPath,
      [
        '-loglevel',
        'error',
        '-f',
        'image2pipe',
        '-framerate',
        '25',
        '-vcodec',
        'mjpeg',
        '-i',
        'pipe:0',
        '-y',
        '-an',
        '-r',
        '25',
        '-c:v',
        'vp8',
        '-qmin',
        '0',
        '-qmax',
        '50',
        '-crf',
        '8',
        '-deadline',
        'realtime',
        '-speed',
        '8',
        '-b:v',
        '1M',
        '-threads',
        '1',
        outputPath,
      ],
      {
        shell: false,
        stdio: ['pipe', 'ignore', 'pipe'],
      },
    )
    const stderr: Buffer[] = []
    child.stderr.on('data', (chunk: Buffer) => stderr.push(chunk))
    const completion = new Promise<void>((resolveCompletion, rejectCompletion) => {
      let settled = false
      child.once('error', (error) => {
        if (settled) return
        settled = true
        rejectCompletion(error)
      })
      child.once('close', (exitCode) => {
        if (settled) return
        settled = true
        if (exitCode === 0) resolveCompletion()
        else
          rejectCompletion(
            new Error(
              `SuiteCut capture FFmpeg exited with ${String(exitCode)}: ${Buffer.concat(stderr).toString('utf8')}`,
            ),
          )
      })
    })
    const recorder: StreamingRecorder = {
      completion,
      stdin: child.stdin,
      stderr,
      lastFrameNumber: -1,
      closing: false,
    }
    streamingRecorders.set(pageId, recorder)

    try {
      await playwrightPage.screencast.start({
        onFrame: async ({ data, timestamp }) => {
          if (recorder.closing) return
          recorder.firstFrameEpochMs ??= timestamp
          activeScreencast.firstFrameEpochMs ??= timestamp
          const frameNumber = Math.max(
            recorder.lastFrameNumber + 1,
            Math.floor((timestamp - recorder.firstFrameEpochMs) / 40),
          )
          if (recorder.lastFrame !== undefined) {
            for (let current = recorder.lastFrameNumber + 1; current < frameNumber; current += 1) {
              if (!recorder.stdin.write(recorder.lastFrame)) await once(recorder.stdin, 'drain')
            }
          }
          if (!recorder.stdin.write(data)) await once(recorder.stdin, 'drain')
          recorder.lastFrame = data
          recorder.lastFrameNumber = frameNumber
        },
      })
    } catch (error) {
      screencasts.delete(pageId)
      streamingRecorders.delete(pageId)
      recorder.closing = true
      recorder.stdin.end()
      await recorder.completion.catch(() => undefined)
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
    const recorder = streamingRecorders.get(pageId)
    streamingRecorders.delete(pageId)
    if (recorder === undefined) throw new Error(`SuiteCut recorder is missing for page ${pageId}`)
    recorder.closing = true

    try {
      await activeScreencast.page.screencast.stop()
    } catch (error) {
      if (!activeScreencast.page.isClosed()) {
        throw error instanceof Error ? error : new Error('Chromium rejected screencast shutdown')
      }
    }
    if (recorder.lastFrame !== undefined && recorder.firstFrameEpochMs !== undefined) {
      const finalFrameNumber = Math.floor((Date.now() - recorder.firstFrameEpochMs) / 40)
      for (let current = recorder.lastFrameNumber + 1; current <= finalFrameNumber; current += 1) {
        if (!recorder.stdin.write(recorder.lastFrame)) await once(recorder.stdin, 'drain')
      }
    }
    delete recorder.lastFrame
    recorder.stdin.end()
    await recorder.completion
    await stat(activeScreencast.outputPath)

    if (activeScreencast.firstFrameEpochMs === undefined) {
      throw new Error(`SuiteCut did not receive a first frame for page ${pageId}`)
    }

    await testInfo.attach(activeScreencast.attachmentName, {
      path: activeScreencast.outputPath,
      contentType: 'video/webm',
    })
    videos.push({
      artifactId: activeScreencast.attachmentName
        .replace('suitecut-source-', '')
        .replace('.webm', ''),
      pageId,
      attachmentName: activeScreencast.attachmentName,
      firstFrameEpochMs: activeScreencast.firstFrameEpochMs,
      sourceStartedAtMs: activeScreencast.firstFrameEpochMs - originEpochMs,
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
    trackPageTask(startScreencast(playwrightPage, pageId))

    const onClose = (): void => {
      pageCloseListeners.delete(playwrightPage)
      livePages.delete(pageId)
      trackPageTask(stopScreencast(pageId))
      if (active && recordedPage.closedAtMs === undefined) {
        recordedPage.closedAtMs = now()
      }

      if (active && activePageId === pageId) {
        const openerPageId = recordedPage.openerPageId
        if (openerPageId !== undefined && livePages.has(openerPageId)) {
          activePageId = openerPageId
        } else if (livePages.has(mainPageId)) {
          activePageId = mainPageId
        } else {
          const fallbackPageId = livePages.keys().next().value
          if (fallbackPageId !== undefined) activePageId = fallbackPageId
        }
      }
    }

    pageCloseListeners.set(playwrightPage, onClose)
    playwrightPage.once('close', onClose)
    const onDocumentLoaded = (): void => {
      trackPageTask(
        Promise.allSettled(
          playwrightPage.frames().map(async (frame) => {
            await frame.evaluate(removeActivePageListeners, markerName)
            await frame.evaluate(installActivePageListeners, listenerOptions)
          }),
        ).then(() => undefined),
      )
    }
    pageDocumentListeners.set(playwrightPage, onDocumentLoaded)
    playwrightPage.on('domcontentloaded', onDocumentLoaded)

    if (kind !== 'main') {
      trackPageTask(
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
    activePageId = registerPage(newPage, newPage === page ? 'main' : 'secondary')
  }

  context.on('page', onContextPage)
  const mainPageId = registerPage(page, 'main')
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

  const bindingResource = await context.exposeBinding(
    bindingName,
    ({ page: sourcePage }, payload?: CapturedPointerPayload) => {
      if (!active) return

      const sourcePageId = pageIds.get(sourcePage)
      if (sourcePageId !== undefined && livePages.has(sourcePageId)) {
        activePageId = sourcePageId
      }
      if (sourcePageId === undefined || payload === undefined) return
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
        events.push({ ...eventBase, type: payload.type } satisfies SuiteCutPointerMoveEvent)
      } else {
        events.push({
          ...eventBase,
          type: payload.type,
          button: pointerButton(payload.button),
        } satisfies SuiteCutPointerButtonEvent)
      }
    },
  )
  const initScriptResource = await context.addInitScript(
    installActivePageListeners,
    listenerOptions,
  )

  const existingFrames = context.pages().flatMap((playwrightPage) => playwrightPage.frames())
  await Promise.allSettled(
    existingFrames.map((frame) => frame.evaluate(installActivePageListeners, listenerOptions)),
  )
  await Promise.all([...pendingPageTasks])
  if (pageTaskErrors.length > 0) {
    throw new AggregateError(pageTaskErrors, 'Failed to start SuiteCut page recording')
  }

  const endRecording = (): Promise<void> => {
    if (endingPromise !== undefined) return endingPromise
    endedAtMs = now()
    active = false
    context.off('page', onContextPage)

    for (const [playwrightPage, closeListener] of pageCloseListeners) {
      playwrightPage.off('close', closeListener)
    }
    pageCloseListeners.clear()
    for (const [playwrightPage, documentListener] of pageDocumentListeners) {
      playwrightPage.off('domcontentloaded', documentListener)
    }
    pageDocumentListeners.clear()

    endingPromise = (async () => {
      try {
        const currentFrames = context.pages().flatMap((playwrightPage) => playwrightPage.frames())
        await Promise.allSettled(
          currentFrames.map((frame) => frame.evaluate(removeActivePageListeners, markerName)),
        )

        const disposalResults = await Promise.allSettled([
          initScriptResource.dispose(),
          bindingResource.dispose(),
        ])
        for (const result of disposalResults) {
          if (result.status === 'rejected') {
            pageTaskErrors.push(
              result.reason instanceof Error ? result.reason : new Error(String(result.reason)),
            )
          }
        }

        await Promise.all([...screencasts.keys()].map((pageId) => stopScreencast(pageId)))

        await Promise.all([...pendingPageTasks])
        if (pageTaskErrors.length > 0) {
          throw new AggregateError(
            pageTaskErrors,
            'Failed to register one or more Playwright pages',
          )
        }
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
      pages: structuredClone([...pages.values()]),
      events: structuredClone(events),
      artifacts: structuredClone(artifacts),
      videos: structuredClone(videos),
    }
  }

  const session: SuiteCutRecordingSession = {
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

      activePageId = pageId
      return pageId
    },
    nextEventId: () => randomUUID(),
    record: (event) => {
      if (!active) throw new Error('Cannot record an event after the recording session is sealed')
      events.push(event)
    },
    endRecording,
    seal: () => {
      sealedAttachment ??= seal()
      return sealedAttachment
    },
  }

  return session
}

function createSuiteCutFixture(
  session: SuiteCutRecordingSession,
  testInfo: TestInfo,
  narration: SuiteCutNarrationPipeline,
): SuiteCutFixture {
  const createPageEventBase = (
    pageId: SuiteCutPageId = session.activePageId,
  ): SuiteCutPageEventBase => ({
    id: session.nextEventId(),
    atMs: session.now(),
    pageId,
  })

  const captureLocatorGeometry = async (locator: Locator, operation: 'highlight' | 'zoom') => {
    const page = locator.page()
    const pageId = session.selectPage(page)
    const [rect, viewport] = await Promise.all([
      locator.boundingBox(),
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
    narrate: (text: string, options?: SuiteCutNarrationOptions) => {
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

      session.record(event)
      narration.enqueue(event)
    },
    checkpoint: async (label: string, options?: SuiteCutCheckpointOptions) => {
      const input = parseCheckpointInput(label, options)
      const pageId = session.activePageId
      const page = session.pageFor(pageId)
      const artifactId = randomUUID()
      const attachmentName = `suitecut-checkpoint-${artifactId}.png`
      const outputPath = testInfo.outputPath(attachmentName)
      const viewport = parseCapturedViewport(await page.evaluate(readSuiteCutViewport))

      await page.screenshot({
        path: outputPath,
        type: 'png',
        fullPage: input.options?.fullPage ?? false,
      })

      const capturedAtMs = session.now()
      await testInfo.attach(attachmentName, {
        path: outputPath,
        contentType: 'image/png',
      })

      session.artifacts.push({
        id: artifactId,
        attachmentName,
        role: 'checkpoint',
        contentType: 'image/png',
        capturedAtMs,
        pageId,
      })

      const event: SuiteCutCheckpointEvent = {
        id: session.nextEventId(),
        type: 'checkpoint',
        atMs: capturedAtMs,
        pageId,
        label: input.label,
        artifactId,
        viewport,
      }
      session.record(event)
    },
    hold: (durationMs: Milliseconds) => {
      const parsedDurationMs = parseHoldInput(durationMs)

      const event: SuiteCutHoldEvent = {
        ...createPageEventBase(),
        type: 'hold',
        durationMs: parsedDurationMs,
        reason: 'author',
      }

      session.record(event)
    },
    highlight: async (locator: Locator, options: SuiteCutHighlightOptions = {}) => {
      const parsedOptions = parseHighlightOptions(options)
      const { pageId, rect, viewport } = await captureLocatorGeometry(locator, 'highlight')

      const event: SuiteCutHighlightEvent = {
        ...createPageEventBase(pageId),
        type: 'highlight',
        rect,
        viewport,
        options: parsedOptions,
      }
      session.record(event)
    },
    zoom: async (locator: Locator, options: SuiteCutZoomOptions = {}) => {
      const parsedOptions = parseZoomOptions(options)
      const { pageId, rect, viewport } = await captureLocatorGeometry(locator, 'zoom')

      const event: SuiteCutZoomEvent = {
        ...createPageEventBase(pageId),
        type: 'zoom',
        rect,
        viewport,
        options: parsedOptions,
      }
      session.record(event)
    },
  }
  return suitecut
}

export const expect = baseExpect
