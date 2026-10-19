import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

type InputDispatch = (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>
export type NativeMouseBackend = 'devtools' | 'cef' | 'widget'
const backend = z.enum(['devtools', 'cef', 'widget']).optional()

const delay = z.number().int().min(0).max(120_000).optional()
const button = z.enum(['left', 'middle', 'right']).optional()
const clickCount = z.number().int().min(1).max(1000).optional()
const point = z.number().finite().min(-1_000_000).max(1_000_000)

/** Genuine Chromium touch input; configuration completes after its native acknowledgment. */
export class NativeTouchscreen {
  constructor(private readonly dispatch: InputDispatch) {}
  async setEnabled(enabled: boolean): Promise<void> {
    await this.dispatch({ kind: 'touch', action: 'configure', enabled: z.boolean().parse(enabled) })
  }
  async tap(x: number, y: number): Promise<void> {
    await this.dispatch({ kind: 'touch', action: 'tap', x: point.parse(x), y: point.parse(y) })
  }
}

/**
 * Pointer operations dispatched and sequenced by the C++ engine.
 * `backend: 'widget'` requires the probed custom CEF receipt API. It provides raw
 * original-widget mouse dispatch; wheel/touch and synthetic drag use their
 * existing native paths. A held gesture must keep the same backend throughout.
 */
export class NativeMouse {
  constructor(private readonly dispatch: InputDispatch) {}
  async move(
    x: number,
    y: number,
    options: { steps?: number; backend?: NativeMouseBackend } = {},
  ): Promise<void> {
    const config = z
      .strictObject({ steps: z.number().int().min(1).max(10_000).optional(), backend })
      .parse(options)
    await this.dispatch({
      kind: 'mouse',
      action: 'move',
      x: point.parse(x),
      y: point.parse(y),
      steps: config.steps ?? 1,
      ...(config.backend ? { backend: config.backend } : {}),
    })
  }
  async down(
    options: {
      button?: 'left' | 'middle' | 'right'
      clickCount?: number
      backend?: NativeMouseBackend
    } = {},
  ): Promise<void> {
    const config = z.strictObject({ button, clickCount, backend }).parse(options)
    await this.dispatch({
      kind: 'mouse',
      action: 'down',
      button: config.button ?? 'left',
      clickCount: config.clickCount ?? 1,
      ...(config.backend ? { backend: config.backend } : {}),
    })
  }
  async up(
    options: {
      button?: 'left' | 'middle' | 'right'
      clickCount?: number
      backend?: NativeMouseBackend
    } = {},
  ): Promise<void> {
    const config = z.strictObject({ button, clickCount, backend }).parse(options)
    await this.dispatch({
      kind: 'mouse',
      action: 'up',
      button: config.button ?? 'left',
      clickCount: config.clickCount ?? 1,
      ...(config.backend ? { backend: config.backend } : {}),
    })
  }
  async click(
    x: number,
    y: number,
    options: {
      button?: 'left' | 'middle' | 'right'
      clickCount?: number
      delay?: number
      backend?: NativeMouseBackend
    } = {},
  ): Promise<void> {
    const config = z.strictObject({ button, clickCount, delay, backend }).parse(options)
    await this.dispatch({
      kind: 'mouse',
      action: 'click',
      x: point.parse(x),
      y: point.parse(y),
      button: config.button ?? 'left',
      clickCount: config.clickCount ?? 1,
      delay: config.delay ?? 0,
      ...(config.backend ? { backend: config.backend } : {}),
    })
  }
  dblclick(
    x: number,
    y: number,
    options: {
      button?: 'left' | 'middle' | 'right'
      delay?: number
      backend?: NativeMouseBackend
    } = {},
  ): Promise<void> {
    return this.click(x, y, { ...options, clickCount: 2 })
  }
  async wheel(deltaX: number, deltaY: number): Promise<void> {
    await this.dispatch({
      kind: 'mouse',
      action: 'wheel',
      deltaX: point.parse(deltaX),
      deltaY: point.parse(deltaY),
    })
  }
}

/** Keyboard state and delayed typing live in the native engine. */
export class NativeKeyboard {
  constructor(private readonly dispatch: InputDispatch) {}
  async down(key: string): Promise<void> {
    await this.dispatch({
      kind: 'keyboard',
      action: 'down',
      key: z.string().min(1).max(128).parse(key),
    })
  }
  async up(key: string): Promise<void> {
    await this.dispatch({
      kind: 'keyboard',
      action: 'up',
      key: z.string().min(1).max(128).parse(key),
    })
  }
  async press(key: string, options: { delay?: number } = {}): Promise<void> {
    const config = z.strictObject({ delay }).parse(options)
    await this.dispatch({
      kind: 'keyboard',
      action: 'press',
      key: z.string().min(1).max(128).parse(key),
      delay: config.delay ?? 0,
    })
  }
  async insertText(text: string): Promise<void> {
    await this.dispatch({ kind: 'keyboard', action: 'insertText', text: z.string().parse(text) })
  }
  async type(text: string, options: { delay?: number } = {}): Promise<void> {
    const config = z.strictObject({ delay }).parse(options)
    await this.dispatch({
      kind: 'keyboard',
      action: 'type',
      text: z.string().parse(text),
      delay: config.delay ?? 0,
    })
  }
}
