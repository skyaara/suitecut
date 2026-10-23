// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'

import { type PresentationPage, type PresentationLocator } from './browser-control.js'
import { DEFAULT_SUITE_CUT_CHECKPOINT_DURATION_MS } from './capture.js'
import {
  type SuiteCutCaptureOptions,
  type SuiteCutCheckpointOptions,
  type SuiteCutFixture,
  type SuiteCutNarrationOptions,
  type SuiteCutPointerActionOptions,
  type SuiteCutScrollOptions,
  type SuiteCutTypeOptions,
} from './fixtures.js'
import { type SuiteCutNarrationPipeline } from './narration.js'
import {
  movePresentationCursor,
  pulsePresentationCursor,
  showPresentationCaption,
  showPresentationHighlight,
  waitForPresentationAnimations,
} from './presentation.js'
import { scrollLocator, scrollPageTop } from './scroll.js'
import {
  type Milliseconds,
  type SuiteCutCheckpointEvent,
  type SuiteCutRecordingSession,
  type SuiteCutZoomOptions,
  type SuiteCutPageId,
  type SuiteCutPageEventBase,
  type SuiteCutHoldEvent,
  type SuiteCutHighlightEvent,
  type SuiteCutHighlightOptions,
  type SuiteCutNarrationEvent,
  type SuiteCutRect,
  type SuiteCutViewport,
  type SuiteCutZoomEvent,
} from './types.js'
import {
  parseCapturedGeometry,
  parseCapturedViewport,
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

export function createSuiteCutFixture<
  P extends PresentationPage = PresentationPage,
  L extends PresentationLocator<P> = PresentationLocator<P>,
>(
  session: SuiteCutRecordingSession<P>,
  narration: SuiteCutNarrationPipeline,
  captureOptions: SuiteCutCaptureOptions,
): SuiteCutFixture<P, L> {
  let zoomQueue = Promise.resolve()

  const createPageEventBase = (
    pageId: SuiteCutPageId = session.activePageId,
  ): SuiteCutPageEventBase => ({
    id: session.nextEventId(),
    atMs: session.now(),
    pageId,
  })

  const captureLocatorGeometry = async (
    locator: L,
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

  const withActionZoom = (
    locator: L,
    zoom: boolean | SuiteCutZoomOptions | undefined,
    action: () => Promise<void>,
  ): Promise<void> => {
    if (zoom === undefined || zoom === false) return action()
    const run = async (): Promise<void> => {
      const options = parseZoomOptions(zoom === true ? {} : zoom)
      await locator.scrollIntoViewIfNeeded()
      const { pageId, rect, viewport } = await captureLocatorGeometry(
        locator,
        'zoom',
        options.geometry ?? 'element',
      )
      const resolved = resolveSuiteCutZoomOptions(options)
      const page = session.pageFor(pageId)
      const event: SuiteCutZoomEvent = {
        ...createPageEventBase(pageId),
        type: 'zoom',
        rect,
        viewport,
        // Keep the live camera close until the action's actual duration is known.
        options: { ...options, holdMs: Number.MAX_SAFE_INTEGER },
      }
      session.record(event)
      const playback = async (): Promise<void> => {
        try {
          await page.waitForTimeout(resolved.enter.durationMs)
          await action()
          const heldMs = session.now() - event.atMs - resolved.enter.durationMs
          await page.waitForTimeout(Math.max(0, resolved.holdMs - heldMs))
        } finally {
          event.options.holdMs = Math.max(0, session.now() - event.atMs - resolved.enter.durationMs)
          if (!page.isClosed()) await page.waitForTimeout(resolved.exit.durationMs)
        }
      }
      if (session.playLiveZoom !== undefined) await session.playLiveZoom(event, playback)
      else await playback()
    }
    const pending = zoomQueue.then(run, run)
    zoomQueue = pending.catch(() => undefined)
    return pending
  }

  const suitecut: SuiteCutFixture<P, L> = {
    selectPage: (page: P) => {
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
    highlight: async (locator: L, options: SuiteCutHighlightOptions = {}) => {
      const parsedOptions = parseHighlightOptions(options)
      return withActionZoom(locator, parsedOptions.zoom, async () => {
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
      })
    },
    zoom: (locator: L, options: SuiteCutZoomOptions = {}) => {
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
    hover: async (locator: L, options: SuiteCutPointerActionOptions = {}) => {
      const parsedOptions = parsePointerActionOptions(options)
      return withActionZoom(locator, parsedOptions.zoom, async () => {
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
      })
    },
    click: async (locator: L, options: SuiteCutPointerActionOptions = {}) => {
      const parsedOptions = parsePointerActionOptions(options)
      return withActionZoom(locator, parsedOptions.zoom, async () => {
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
      })
    },
    type: async (locator: L, text: string, options: SuiteCutTypeOptions = {}) => {
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
    scrollTo: async (locator: L, options: SuiteCutScrollOptions = {}) => {
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
