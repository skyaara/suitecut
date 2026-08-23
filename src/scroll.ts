import { type Locator, type Page } from '@playwright/test'

import { type SuiteCutScrollOptions } from './schemas.js'

interface ResolvedScrollOptions {
  behavior: ScrollBehavior
  block: ScrollLogicalPosition
  inline: ScrollLogicalPosition
  timeoutMs: number
}

function resolveScrollOptions(options: SuiteCutScrollOptions): ResolvedScrollOptions {
  return {
    behavior: options.behavior ?? 'smooth',
    block: options.block ?? 'center',
    inline: options.inline ?? 'nearest',
    timeoutMs: options.timeoutMs ?? 2_000,
  }
}

async function scrollElement(element: Element, options: ResolvedScrollOptions): Promise<void> {
  element.scrollIntoView({
    behavior: options.behavior,
    block: options.block,
    inline: options.inline,
  })

  const startedAt = performance.now()
  let previous = element.getBoundingClientRect()
  let stableFrames = 0

  await new Promise<void>((resolve, reject) => {
    const sample = (): void => {
      const current = element.getBoundingClientRect()
      const stable =
        Math.abs(current.x - previous.x) < 0.25 && Math.abs(current.y - previous.y) < 0.25
      stableFrames = stable ? stableFrames + 1 : 0
      previous = current

      if (stableFrames >= 3) {
        resolve()
        return
      }
      if (performance.now() - startedAt >= options.timeoutMs) {
        reject(new Error(`Native element scroll did not settle within ${options.timeoutMs}ms`))
        return
      }
      requestAnimationFrame(sample)
    }

    requestAnimationFrame(sample)
  })
}

async function scrollWindowToTop(options: ResolvedScrollOptions): Promise<void> {
  window.scrollTo({ top: 0, left: 0, behavior: options.behavior })

  const startedAt = performance.now()
  let previous = { x: window.scrollX, y: window.scrollY }
  let stableFrames = 0

  await new Promise<void>((resolve, reject) => {
    const sample = (): void => {
      const current = { x: window.scrollX, y: window.scrollY }
      const stable =
        Math.abs(current.x - previous.x) < 0.25 && Math.abs(current.y - previous.y) < 0.25
      stableFrames = stable ? stableFrames + 1 : 0
      previous = current

      if (stableFrames >= 3) {
        resolve()
        return
      }
      if (performance.now() - startedAt >= options.timeoutMs) {
        reject(new Error(`Native page scroll did not settle within ${options.timeoutMs}ms`))
        return
      }
      requestAnimationFrame(sample)
    }

    requestAnimationFrame(sample)
  })
}

export async function scrollLocator(
  locator: Locator,
  options: SuiteCutScrollOptions,
): Promise<void> {
  await locator.evaluate(scrollElement, resolveScrollOptions(options))
  const settleMs = options.settleMs ?? 120
  if (settleMs > 0) await locator.page().waitForTimeout(settleMs)
}

export async function scrollPageTop(page: Page, options: SuiteCutScrollOptions): Promise<void> {
  await page.evaluate(scrollWindowToTop, resolveScrollOptions(options))
  const settleMs = options.settleMs ?? 120
  if (settleMs > 0) await page.waitForTimeout(settleMs)
}
