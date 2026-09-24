import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { raceWithAbort } from './abort.js'
import { type PresentationLocator, type PresentationPage } from './browser-control.js'
import { type NativeBrowser } from './native-browser.js'
import { type SuiteCutRect } from './types/geometry.js'
import { type UntrustedInput } from './untrusted.js'

const EvaluationResult = z.object({
  result: z
    .object({ value: z.unknown().optional(), unserializableValue: z.string().optional() })
    .optional(),
  exceptionDetails: z
    .object({
      text: z.string(),
      exception: z.object({ description: z.string().optional() }).optional(),
    })
    .optional(),
})

/** A deliberately small browser-control API; this is not a Playwright Page. */
export class NativePage implements PresentationPage {
  private readonly signal: AbortSignal
  constructor(
    readonly source: NativeBrowser,
    signal?: AbortSignal,
  ) {
    const runtime = new AbortController()
    this.signal = signal ? AbortSignal.any([signal, runtime.signal]) : runtime.signal
    void source.failure.catch((error: Error) => runtime.abort(error))
    void source.closed.then(() => runtime.abort(new Error('Native source closed')))
  }

  goto(url: string): Promise<void> {
    this.signal?.throwIfAborted()
    return raceWithAbort(this.source.navigate(url), this.signal)
  }

  sendDevToolsCommand(
    method: string,
    params?: Record<string, UntrustedInput>,
  ): Promise<UntrustedInput> {
    this.signal?.throwIfAborted()
    return raceWithAbort(this.source.sendDevToolsCommand(method, params), this.signal)
  }

  locator(selector: string): NativeLocator {
    if (!selector.trim()) throw new Error('A native locator requires a CSS selector')
    return new NativeLocator(this, selector)
  }

  async evaluate<R>(fn: () => R | Promise<R>): Promise<R>
  async evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg: A): Promise<R>
  async evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg?: A): Promise<R> {
    this.signal?.throwIfAborted()
    const expression = `(${fn.toString()})(${JSON.stringify(arg) ?? 'undefined'})`
    const result = EvaluationResult.parse(
      await raceWithAbort(
        this.source.sendDevToolsCommand('Runtime.evaluate', {
          expression,
          awaitPromise: true,
          returnByValue: true,
          userGesture: true,
        }),
        this.signal,
      ),
    )
    if (result.exceptionDetails)
      throw new Error(
        `Native page evaluation failed: ${result.exceptionDetails.exception?.description ?? result.exceptionDetails.text}`,
      )
    if (result.result?.unserializableValue)
      throw new Error('Native evaluation requires a JSON-serializable result')
    // Like browser evaluate APIs, the caller declares the serialized result type.
    return result.result?.value as R
  }

  waitForTimeout(ms: number): Promise<void> {
    if (!Number.isFinite(ms) || ms < 0) return Promise.reject(new Error('Invalid wait duration'))
    return delay(ms, undefined, { signal: this.signal })
  }

  async waitForLoadState(state: 'domcontentloaded'): Promise<void> {
    if (state !== 'domcontentloaded') throw new Error('Unsupported native load state')
    const deadline = performance.now() + 30_000
    while (!(await this.evaluate(() => document.readyState !== 'loading'))) {
      if (performance.now() >= deadline) throw new Error('Native document load timed out')
      await this.waitForTimeout(50)
    }
  }

  isClosed(): boolean {
    return this.source.state !== 'running'
  }
}

/** Strict CSS locator with native input; scopes are the main document only. */
export class NativeLocator implements PresentationLocator<NativePage> {
  constructor(
    private readonly owner: NativePage,
    readonly selector: string,
  ) {}
  page(): NativePage {
    return this.owner
  }
  toString(): string {
    return `native.locator(${JSON.stringify(this.selector)})`
  }

  evaluate<R, A>(
    fn: (element: SVGElement | HTMLElement, arg: A) => R | Promise<R>,
    arg: A,
  ): Promise<R> {
    return this.owner.evaluate(
      async ({ selector, body, value }) => {
        const matches = document.querySelectorAll(selector)
        if (matches.length !== 1)
          throw new Error(`Native locator expected one element, found ${matches.length}`)
        const element = matches[0]
        if (!(element instanceof HTMLElement || element instanceof SVGElement))
          throw new Error('Unsupported native element')
        const evaluate = (0, eval)(`(${body})`) as (element: Element, arg: A) => R | Promise<R>
        return await evaluate(element, value)
      },
      { selector: this.selector, body: fn.toString(), value: arg },
    )
  }

  boundingBox(): Promise<SuiteCutRect | null> {
    return this.evaluate((element) => {
      const rect = element.getBoundingClientRect()
      const style = getComputedStyle(element)
      if (!rect.width || !rect.height || style.visibility === 'hidden' || style.display === 'none')
        return null
      return { x: rect.x, y: rect.y, width: rect.width, height: rect.height }
    }, undefined)
  }

  async scrollIntoViewIfNeeded(): Promise<void> {
    await this.evaluate(
      (element) =>
        element.scrollIntoView({ block: 'center', inline: 'nearest', behavior: 'instant' }),
      undefined,
    )
  }

  private async point(): Promise<{ x: number; y: number }> {
    await this.scrollIntoViewIfNeeded()
    return this.evaluate((element) => {
      const rect = element.getBoundingClientRect()
      const x =
        Math.max(0, rect.left) + (Math.min(innerWidth, rect.right) - Math.max(0, rect.left)) / 2
      const y =
        Math.max(0, rect.top) + (Math.min(innerHeight, rect.bottom) - Math.max(0, rect.top)) / 2
      const target = document.elementFromPoint(x, y)
      if (
        !rect.width ||
        !rect.height ||
        !target ||
        !(element === target || element.contains(target))
      )
        throw new Error('Native element is not visible or is covered')
      if (element.matches(':disabled')) throw new Error('Native element is disabled')
      return { x, y }
    }, undefined)
  }

  async hover(): Promise<void> {
    const point = await this.point()
    await this.owner.sendDevToolsCommand('Input.dispatchMouseEvent', {
      type: 'mouseMoved',
      ...point,
    })
  }

  async click(): Promise<void> {
    const point = await this.point()
    await this.owner.sendDevToolsCommand('Input.dispatchMouseEvent', {
      type: 'mouseMoved',
      ...point,
    })
    await this.owner.sendDevToolsCommand('Input.dispatchMouseEvent', {
      type: 'mousePressed',
      button: 'left',
      clickCount: 1,
      ...point,
    })
    await this.owner.sendDevToolsCommand('Input.dispatchMouseEvent', {
      type: 'mouseReleased',
      button: 'left',
      clickCount: 1,
      ...point,
    })
  }

  async fill(value: string): Promise<void> {
    await this.evaluate((element) => {
      if (
        !(element instanceof HTMLInputElement || element instanceof HTMLTextAreaElement) ||
        element.disabled ||
        element.readOnly
      )
        throw new Error('Native fill requires an editable input or textarea')
      element.focus()
      element.select()
    }, undefined)
    // Delete a selection through Chromium input rather than silently mutating its value.
    await this.owner.sendDevToolsCommand('Input.dispatchKeyEvent', {
      type: 'keyDown',
      key: 'Backspace',
      code: 'Backspace',
      windowsVirtualKeyCode: 8,
    })
    await this.owner.sendDevToolsCommand('Input.dispatchKeyEvent', {
      type: 'keyUp',
      key: 'Backspace',
      code: 'Backspace',
      windowsVirtualKeyCode: 8,
    })
    if (value) await this.owner.sendDevToolsCommand('Input.insertText', { text: value })
  }

  async pressSequentially(value: string, options: { delay: number }): Promise<void> {
    await this.evaluate((element) => {
      if (
        !(element instanceof HTMLElement) ||
        element.matches(':disabled') ||
        ('readOnly' in element && element.readOnly)
      )
        throw new Error('Native typing requires an editable element')
      if (!(
        element instanceof HTMLInputElement ||
        element instanceof HTMLTextAreaElement ||
        element.isContentEditable
      ))
        throw new Error('Native typing requires an editable element')
      element.focus()
    }, undefined)
    for (const text of value) {
      await this.owner.sendDevToolsCommand('Input.insertText', { text })
      if (options.delay > 0) await this.owner.waitForTimeout(options.delay)
    }
  }
}

/** Adds authored browser controls to an existing native source without owning it. */
export function createNativePage(source: NativeBrowser, signal?: AbortSignal): NativePage {
  if (source.state !== 'running') throw new Error('Native source is not running')
  return new NativePage(source, signal)
}
