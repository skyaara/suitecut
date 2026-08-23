import { type Page } from '@playwright/test'

import { type SuiteCutHighlightOptions, type SuiteCutPoint, type SuiteCutRect } from './types.js'

interface PresentationHighlightInput {
  rect: SuiteCutRect
  options: SuiteCutHighlightOptions
  durationMs: number
}

interface PresentationCaptionInput {
  text: string
  durationMs: number
}

interface PresentationCursorInput {
  point: SuiteCutPoint
  durationMs: number
}

/** Installs SuiteCut's non-interactive recording layer in the current document. */
export function installSuiteCutPresentation(): void {
  if (document.documentElement.querySelector('[data-suitecut-presentation]') !== null) return

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
      pointer-events: none;
      will-change: opacity, transform;
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
      background: rgb(2 6 23 / 88%);
      box-shadow: 0 10px 36px rgb(0 0 0 / 47%);
      font: 650 24px/1.35 ui-sans-serif, system-ui, sans-serif;
      text-align: center;
      transform: translateX(-50%);
      will-change: opacity, transform;
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
  layer.append(highlight)

  const enterDuration = input.options.enter?.durationMs ?? 180
  const exitDuration = input.options.exit?.durationMs ?? 140
  const stableDuration = Math.max(0, input.durationMs - enterDuration - exitDuration)
  const enterScale = input.options.enter?.type?.includes('scale') === true ? 0.94 : 1
  const exitScale = input.options.exit?.type?.includes('scale') === true ? 0.96 : 1
  const enter = highlight.animate(
    [
      { opacity: 0, transform: `scale(${enterScale})` },
      { opacity: 1, transform: 'scale(1)' },
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
      { opacity: 1, transform: 'scale(1)' },
      { opacity: 0, transform: `scale(${exitScale})` },
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
  caption.textContent = input.text
  layer.append(caption)
  const enterDuration = Math.min(180, input.durationMs / 2)
  const exitDuration = Math.min(140, input.durationMs / 2)
  const stableDuration = Math.max(0, input.durationMs - enterDuration - exitDuration)
  const enter = caption.animate(
    [
      { opacity: 0, transform: 'translate(-50%, 14px)' },
      { opacity: 1, transform: 'translate(-50%, 0)' },
    ],
    { duration: enterDuration, easing: 'ease-out', fill: 'both' },
  )
  await enter.finished
  await new Promise((resolve) => window.setTimeout(resolve, stableDuration))
  const exit = caption.animate(
    [
      { opacity: 1, transform: 'translate(-50%, 0)' },
      { opacity: 0, transform: 'translate(-50%, 8px)' },
    ],
    { duration: exitDuration, easing: 'ease-in', fill: 'both' },
  )
  await exit.finished
  caption.remove()
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
): Promise<void> {
  await ensurePresentation(page)
  await page.evaluate(showSuiteCutCaption, { text, durationMs })
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
