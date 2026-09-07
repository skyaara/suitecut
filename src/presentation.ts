import { type Page } from 'playwright'

import {
  type SuiteCutHighlightOptions,
  type SuiteCutPoint,
  type SuiteCutRect,
  type SuiteCutWordTiming,
} from './types.js'

interface PresentationHighlightInput {
  rect: SuiteCutRect
  options: SuiteCutHighlightOptions
  durationMs: number
}

interface PresentationCaptionInput {
  text: string
  durationMs: number
  words?: SuiteCutWordTiming[]
}

interface PresentationCursorInput {
  point: SuiteCutPoint
  durationMs: number
}

/** Installs SuiteCut's non-interactive recording layer in the current document. */
export function installSuiteCutPresentation(): void {
  const syncZoom = (host: HTMLElement): void => {
    const computedZoom = Number.parseFloat(getComputedStyle(document.documentElement).zoom)
    const rootZoom = Number.isFinite(computedZoom) && computedZoom > 0 ? computedZoom : 1
    host.style.setProperty('zoom', String(1 / rootZoom))
  }
  const existingHost = document.documentElement.querySelector<HTMLElement>(
    '[data-suitecut-presentation]',
  )
  if (existingHost !== null) {
    syncZoom(existingHost)
    return
  }

  const host = document.createElement('div')
  host.setAttribute('data-suitecut-presentation', '')
  host.style.cssText =
    'position:fixed;inset:0;z-index:2147483647;pointer-events:none;overflow:hidden;contain:strict;'
  const shadow = host.attachShadow({ mode: 'open' })

  const style = document.createElement('style')
  style.textContent = `
    :host { all: initial; }
    #layer { position: fixed; inset: 0; pointer-events: none; overflow: hidden; }
    #cursor {
      position: absolute;
      left: 0;
      top: 0;
      width: 22px;
      height: 28px;
      opacity: 0;
      transform: translate3d(0, 0, 0);
      transform-origin: 3px 3px;
      filter: drop-shadow(0 2px 3px rgb(0 0 0 / 55%));
      will-change: transform, opacity;
    }
    #cursor::before {
      content: '';
      position: absolute;
      inset: 0;
      background: white;
      clip-path: polygon(0 0, 0 85%, 24% 64%, 39% 100%, 55% 92%, 40% 58%, 72% 58%);
    }
    #cursor::after {
      content: '';
      position: absolute;
      inset: 2px;
      background: #111827;
      clip-path: polygon(0 0, 0 73%, 23% 53%, 39% 88%, 47% 84%, 32% 49%, 61% 49%);
    }
    .highlight {
      position: absolute;
      box-sizing: border-box;
      pointer-events: none;
      will-change: opacity, filter;
    }
    .highlight-label {
      position: absolute;
      left: -1px;
      bottom: calc(100% + 8px);
      max-width: 280px;
      padding: 5px 8px;
      border-radius: 6px;
      color: white;
      background: rgb(2 6 23 / 90%);
      font: 650 13px/1.25 ui-sans-serif, system-ui, sans-serif;
      white-space: nowrap;
      overflow: hidden;
      text-overflow: ellipsis;
    }
    .caption {
      position: absolute;
      left: 50%;
      bottom: 42px;
      max-width: min(920px, calc(100vw - 80px));
      padding: 13px 20px;
      border: 1px solid rgb(255 255 255 / 13%);
      border-radius: 13px;
      color: white;
      background: rgb(2 6 23);
      box-shadow: 0 10px 36px rgb(0 0 0 / 47%);
      font: 650 24px/1.35 ui-sans-serif, system-ui, sans-serif;
      text-align: center;
      transform: translateX(-50%);
    }
    .caption-word {
      border-radius: 5px;
      transition: color 80ms linear, background-color 80ms linear;
    }
    .caption-word-current {
      color: #fde047;
      background: rgb(250 204 21 / 18%);
    }
    .ripple {
      position: absolute;
      width: 30px;
      height: 30px;
      margin: -15px 0 0 -15px;
      border: 3px solid rgb(250 204 21 / 85%);
      border-radius: 999px;
      will-change: opacity, transform;
    }
  `
  const layer = document.createElement('div')
  layer.id = 'layer'
  const cursor = document.createElement('div')
  cursor.id = 'cursor'
  cursor.setAttribute('data-suitecut-cursor', '')
  layer.append(cursor)
  shadow.append(style, layer)
  document.documentElement.append(host)
  syncZoom(host)
}

/** Removes SuiteCut's recording layer from the current document. */
export function removeSuiteCutPresentation(): void {
  document.documentElement.querySelector('[data-suitecut-presentation]')?.remove()
}

/** Animates the injected cursor to a viewport point. */
export async function moveSuiteCutCursor(input: PresentationCursorInput): Promise<void> {
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const cursor = host?.shadowRoot?.querySelector<HTMLElement>('#cursor')
  if (cursor === null || cursor === undefined) {
    throw new Error('SuiteCut cursor is not installed')
  }

  const previousX = Number(cursor.dataset.x ?? input.point.x)
  const previousY = Number(cursor.dataset.y ?? input.point.y)
  const from = `translate3d(${previousX}px, ${previousY}px, 0)`
  const to = `translate3d(${input.point.x}px, ${input.point.y}px, 0)`
  cursor.style.opacity = '1'
  cursor.style.transform = to
  cursor.dataset.x = String(input.point.x)
  cursor.dataset.y = String(input.point.y)

  const animation = cursor.animate(
    [
      {
        opacity: previousX === input.point.x && previousY === input.point.y ? 0 : 1,
        transform: from,
      },
      { opacity: 1, transform: to },
    ],
    { duration: input.durationMs, easing: 'cubic-bezier(.22,.8,.22,1)', fill: 'both' },
  )
  await animation.finished
  animation.cancel()
  cursor.style.opacity = '1'
  cursor.style.transform = to
}

/** Draws a click ripple at the injected cursor's current position. */
export async function pulseSuiteCutCursor(durationMs: number): Promise<void> {
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const shadow = host?.shadowRoot
  const layer = shadow?.querySelector<HTMLElement>('#layer')
  const cursor = shadow?.querySelector<HTMLElement>('#cursor')
  if (layer === null || layer === undefined || cursor === null || cursor === undefined) {
    throw new Error('SuiteCut cursor is not installed')
  }

  const ripple = document.createElement('div')
  ripple.className = 'ripple'
  ripple.style.left = `${Number(cursor.dataset.x ?? 0)}px`
  ripple.style.top = `${Number(cursor.dataset.y ?? 0)}px`
  layer.append(ripple)
  const animation = ripple.animate(
    [
      { opacity: 0.95, transform: 'scale(.35)' },
      { opacity: 0, transform: 'scale(1.35)' },
    ],
    { duration: durationMs, easing: 'ease-out', fill: 'both' },
  )
  await animation.finished
  ripple.remove()
}

/** Shows a highlight inside the page so the screencast records its animation. */
export async function showSuiteCutHighlight(input: PresentationHighlightInput): Promise<void> {
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const layer = host?.shadowRoot?.querySelector<HTMLElement>('#layer')
  if (layer === null || layer === undefined) {
    throw new Error('SuiteCut highlight layer is not installed')
  }

  const padding = input.options.paddingPx ?? 8
  const highlight = document.createElement('div')
  highlight.className = 'highlight'
  highlight.setAttribute('data-suitecut-highlight', '')
  highlight.style.left = `${input.rect.x - padding}px`
  highlight.style.top = `${input.rect.y - padding}px`
  highlight.style.width = `${input.rect.width + padding * 2}px`
  highlight.style.height = `${input.rect.height + padding * 2}px`
  highlight.style.border = `${input.options.borderWidthPx ?? 4}px ${input.options.borderStyle ?? 'solid'} ${input.options.borderColor ?? '#7C3AED'}`
  highlight.style.borderRadius = `${input.options.borderRadiusPx ?? 10}px`
  if (input.options.mode === 'fill' || input.options.mode === 'spotlight') {
    const fill = input.options.fillColor ?? '#7C3AED'
    const fillOpacity = input.options.fillOpacity ?? 0.08
    highlight.style.background = `color-mix(in srgb, ${fill} ${fillOpacity * 100}%, transparent)`
  }
  if (input.options.mode === 'spotlight') {
    const backdrop = input.options.backdropColor ?? '#000000'
    const backdropOpacity = input.options.backdropOpacity ?? 0.45
    highlight.style.boxShadow = `0 0 0 9999px color-mix(in srgb, ${backdrop} ${backdropOpacity * 100}%, transparent)`
  }
  if (input.options.label !== undefined) {
    const label = document.createElement('span')
    label.className = 'highlight-label'
    label.setAttribute('data-suitecut-highlight-label', '')
    label.textContent = input.options.label
    if (input.rect.y - padding < 48) {
      label.style.top = 'calc(100% + 8px)'
      label.style.bottom = 'auto'
    }
    if (input.rect.x - padding + 280 > window.innerWidth) {
      label.style.right = '-1px'
      label.style.left = 'auto'
    }
    highlight.append(label)
  }
  layer.append(highlight)

  const enterType = input.options.enter?.type ?? 'fade'
  const exitType = input.options.exit?.type ?? 'fade'
  const enterDuration = enterType === 'none' ? 0 : (input.options.enter?.durationMs ?? 180)
  const exitDuration = exitType === 'none' ? 0 : (input.options.exit?.durationMs ?? 140)
  const stableDuration = Math.max(0, input.durationMs - enterDuration - exitDuration)
  const enterOpacity = enterType.includes('fade') ? 0 : 1
  const exitOpacity = exitType.includes('fade') ? 0 : 1
  const enterFilter = enterType.includes('scale') ? 'brightness(1.35)' : 'brightness(1)'
  const exitFilter = exitType.includes('scale') ? 'brightness(1.25)' : 'brightness(1)'
  const enter = highlight.animate(
    [
      { opacity: enterOpacity, filter: enterFilter },
      { opacity: 1, filter: 'brightness(1)' },
    ],
    {
      duration: enterDuration,
      easing: input.options.enter?.easing ?? 'ease-out',
      fill: 'both',
    },
  )
  await enter.finished
  await new Promise((resolve) => window.setTimeout(resolve, stableDuration))
  const exit = highlight.animate(
    [
      { opacity: 1, filter: 'brightness(1)' },
      { opacity: exitOpacity, filter: exitFilter },
    ],
    {
      duration: exitDuration,
      easing: input.options.exit?.easing ?? 'ease-in',
      fill: 'both',
    },
  )
  await exit.finished
  highlight.remove()
}

/** Shows a caption for an already measured narration clip. */
export async function showSuiteCutCaption(input: PresentationCaptionInput): Promise<void> {
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const layer = host?.shadowRoot?.querySelector<HTMLElement>('#layer')
  if (layer === null || layer === undefined) {
    throw new Error('SuiteCut caption layer is not installed')
  }

  const caption = document.createElement('div')
  caption.className = 'caption'
  caption.setAttribute('data-suitecut-caption', '')
  const wordElements: HTMLElement[] = []
  let textOffset = 0
  for (const word of input.words ?? []) {
    caption.append(document.createTextNode(input.text.slice(textOffset, word.startOffset)))
    const element = document.createElement('span')
    element.className = 'caption-word'
    element.textContent = word.text
    caption.append(element)
    wordElements.push(element)
    textOffset = word.endOffset
  }
  if (wordElements.length === 0) caption.textContent = input.text
  else caption.append(document.createTextNode(input.text.slice(textOffset)))
  layer.append(caption)
  const timers: number[] = []
  for (const [index, word] of (input.words ?? []).entries()) {
    const element = wordElements[index]
    if (element === undefined) continue
    timers.push(
      window.setTimeout(() => element.classList.add('caption-word-current'), word.startMs),
      window.setTimeout(() => element.classList.remove('caption-word-current'), word.endMs),
    )
  }
  try {
    await new Promise((resolve) => window.setTimeout(resolve, input.durationMs))
  } finally {
    for (const timer of timers) window.clearTimeout(timer)
    caption.remove()
  }
}

/** Waits for application animations without waiting for SuiteCut's own recording layer. */
export async function waitForSuiteCutPageAnimations(timeoutMs: number): Promise<void> {
  await new Promise((resolve) => requestAnimationFrame(() => requestAnimationFrame(resolve)))
  const host = document.documentElement.querySelector('[data-suitecut-presentation]')
  const animations = document.getAnimations().filter((animation) => {
    const target = animation.effect instanceof KeyframeEffect ? animation.effect.target : null
    return !(target instanceof Node && host?.shadowRoot?.contains(target))
  })
  if (animations.length === 0) return

  await Promise.race([
    Promise.allSettled(animations.map((animation) => animation.finished)),
    new Promise((resolve) => window.setTimeout(resolve, timeoutMs)),
  ])
}

async function ensurePresentation(page: Page): Promise<void> {
  await page.evaluate(installSuiteCutPresentation)
}

export async function movePresentationCursor(
  page: Page,
  point: SuiteCutPoint,
  durationMs: number,
): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(moveSuiteCutCursor, { point, durationMs })
}

export async function pulsePresentationCursor(page: Page, durationMs: number): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(pulseSuiteCutCursor, durationMs)
}

export async function showPresentationHighlight(
  page: Page,
  rect: SuiteCutRect,
  options: SuiteCutHighlightOptions,
  durationMs: number,
): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(showSuiteCutHighlight, { rect, options, durationMs })
}

export async function showPresentationCaption(
  page: Page,
  text: string,
  durationMs: number,
  words?: SuiteCutWordTiming[],
): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(showSuiteCutCaption, {
    text,
    durationMs,
    ...(words === undefined ? {} : { words }),
  })
}

export async function waitForPresentationAnimations(page: Page, timeoutMs: number): Promise<void> {
  try {
    await page.evaluate(waitForSuiteCutPageAnimations, timeoutMs)
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error)
    if (
      message.includes('Execution context was destroyed') ||
      message.includes('Cannot find context with specified id')
    ) {
      await page.waitForLoadState('domcontentloaded')
      return
    }
    throw error
  }
}
