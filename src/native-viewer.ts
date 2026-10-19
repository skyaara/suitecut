// SPDX-License-Identifier: MIT
import { randomBytes, timingSafeEqual } from 'node:crypto'
import { createServer, type IncomingMessage, type ServerResponse } from 'node:http'
import { type Socket } from 'node:net'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { type NativeBrowserController } from './native-browser-controller.js'
import { type NativeVideoFrame } from './native-browser.js'
import { nativeContextDeviceScaleFactor } from './native-context.js'
import { type NativePage } from './native-page.js'
import { NativeViewerGate, type NativeViewerMode } from './native-viewer-gate.js'
import { nativeViewerRGBAsync } from './native-viewer-pixels.js'
import { nativeViewerHTML, nativeViewerScript, nativeViewerStyles } from './native-viewer-ui.js'
import { toError, type UntrustedInput } from './untrusted.js'

export type NativeViewerResolutionMode = 'fixed' | 'auto'
export interface NativeBrowserViewerOptions {
  browser: NativeBrowserController
  page?: NativePage
  /** Loopback HTTP port; zero selects a free port. Defaults to zero. */
  port?: number
  /** Maximum shared JPEG encoding rate, independent of native capture cadence. */
  framesPerSecond?: number
  quality?: number
  /** Maximum time to drain an agent step and actual native automation work. */
  takeoverTimeoutMs?: number
  /** Auto follows the controlling viewer's available area; fixed preserves the viewport. */
  resolutionMode?: NativeViewerResolutionMode
  signal?: AbortSignal
}
export interface NativeBrowserViewerState {
  mode: NativeViewerMode
  owner: string | null
  revision: number
  pages: { id: number; url: string }[]
  pageId: number | null
  width: number
  height: number
  frameSequence: number
  frameError: string | null
  resolutionMode: NativeViewerResolutionMode
  maxWidth: number
  maxHeight: number
  canResize: boolean
}
export interface NativeBrowserViewer {
  /** Local URL with an unguessable bearer token in its fragment. Treat it as a credential. */
  readonly url: string
  readonly closed: Promise<void>
  state(): NativeBrowserViewerState
  /** One fully awaited AI step; waits while a human owns the browser. */
  runAgent<T>(step: () => Promise<T>, signal?: AbortSignal): Promise<T>
  /** Closes the viewer, releases held input and removes its SDK admission gate. */
  close(): Promise<void>
}

const Options = z.strictObject({
  port: z.number().int().min(0).max(65535).default(0),
  framesPerSecond: z.number().int().min(1).max(120).default(30),
  quality: z.number().int().min(20).max(95).default(75),
  takeoverTimeoutMs: z.number().int().min(100).max(120000).default(15000),
  resolutionMode: z.enum(['fixed', 'auto']).default('fixed'),
})
const base = {
  clientId: z.string().regex(/^[a-f0-9]{32}$/u),
  revision: z.number().int().positive(),
}
const button = z.enum(['left', 'middle', 'right'])
const keyName = z
  .string()
  .regex(
    /^(?:[\x20-\x7e]|Key[A-Z]|Digit[0-9]|F(?:[1-9]|1[0-9]|2[0-4])|Arrow(?:Left|Right|Up|Down)|Backspace|Tab|Enter|Escape|Delete|Insert|Home|End|PageUp|PageDown|Space|Shift(?:Left|Right)?|Control(?:Left|Right)?|Alt(?:Left|Right)?|Meta(?:Left|Right)?|CapsLock|NumLock|ScrollLock|Pause|PrintScreen|ContextMenu|Numpad(?:[0-9]|Enter|Add|Subtract|Multiply|Divide|Decimal))$/u,
  )
const Action = z.discriminatedUnion('type', [
  z.strictObject({ ...base, type: z.literal('takeover') }),
  z.strictObject({ ...base, type: z.literal('release') }),
  z.strictObject({ ...base, type: z.literal('heartbeat') }),
  z.strictObject({ ...base, type: z.literal('resetInput') }),
  z.strictObject({ ...base, type: z.literal('back') }),
  z.strictObject({ ...base, type: z.literal('forward') }),
  z.strictObject({ ...base, type: z.literal('reload') }),
  z.strictObject({ ...base, type: z.literal('selectPage'), pageId: z.number().int().positive() }),
  z.strictObject({ ...base, type: z.literal('navigate'), url: z.string().min(1).max(8192) }),
  z.strictObject({
    ...base,
    type: z.literal('resize'),
    width: z.number().int().min(2).max(3840),
    height: z.number().int().min(2).max(2160),
    mode: z.enum(['fixed', 'auto']),
  }),
  z.strictObject({
    ...base,
    type: z.literal('pointer'),
    action: z.enum(['move', 'down', 'up']),
    x: z.number().finite().min(0).max(3840),
    y: z.number().finite().min(0).max(2160),
    button,
  }),
  z.strictObject({
    ...base,
    type: z.literal('wheel'),
    deltaX: z.number().finite().min(-100000).max(100000),
    deltaY: z.number().finite().min(-100000).max(100000),
  }),
  z.strictObject({ ...base, type: z.literal('key'), action: z.enum(['down', 'up']), key: keyName }),
  z.strictObject({ ...base, type: z.literal('text'), text: z.string().min(1).max(8192) }),
])
class ViewerHTTPError extends Error {
  constructor(
    readonly status: number,
    message: string,
  ) {
    super(message)
  }
}
function sendJSON(response: ServerResponse, status: number, value: UntrustedInput): void {
  response.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8' })
  response.end(JSON.stringify(value))
}
async function body(request: IncomingMessage): Promise<UntrustedInput> {
  if (!request.headers['content-type']?.startsWith('application/json'))
    throw new ViewerHTTPError(415, 'Expected application/json')
  const chunks: Buffer[] = []
  let size = 0
  for await (const input of request) {
    const chunk = Buffer.isBuffer(input) ? input : Buffer.from(String(input))
    size += chunk.length
    if (size > 49152) throw new ViewerHTTPError(413, 'Viewer action exceeds 48 KiB')
    chunks.push(chunk)
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString('utf8')) as UntrustedInput
  } catch {
    throw new ViewerHTTPError(400, 'Invalid JSON action')
  }
}

/** Adds a live human view to an existing helper without creating a second browser session. */
export async function createBrowserViewer(
  options: NativeBrowserViewerOptions,
): Promise<NativeBrowserViewer> {
  const config = Options.parse({
    ...(options.port === undefined ? {} : { port: options.port }),
    ...(options.framesPerSecond === undefined ? {} : { framesPerSecond: options.framesPerSecond }),
    ...(options.quality === undefined ? {} : { quality: options.quality }),
    ...(options.takeoverTimeoutMs === undefined
      ? {}
      : { takeoverTimeoutMs: options.takeoverTimeoutMs }),
    ...(options.resolutionMode === undefined ? {} : { resolutionMode: options.resolutionMode }),
  })
  options.signal?.throwIfAborted()
  const { browser } = options
  if (!browser.isConnected()) throw new Error('Browser viewer requires a running native browser')
  if (browser.source.pixelFormat !== 'bgra')
    throw new Error('Browser viewer requires a browser launched with pixelFormat: bgra')
  const pages = (): NativePage[] =>
    browser
      .contexts()
      .flatMap((context) => context.pages())
      .filter((page) => !page.isClosed())
  let selected = options.page ?? pages()[0]
  if (options.page && !pages().includes(options.page))
    throw new Error('Viewer page belongs to another browser')
  if (selected && !selected.viewportSize())
    throw new Error('Browser viewer requires a fixed page viewport')
  const sharp = (await import('sharp')).default
  const gate = new NativeViewerGate(browser.source.processSignal)
  const token = randomBytes(32).toString('hex')
  const tokenBytes = Buffer.from(`Bearer ${token}`)
  const clients = new Map<string, number>()
  let owner: string | null = null
  let ownerSeen = 0
  let revision = 1
  let frameSequence = 0
  let frameError: string | null = null
  let latest: NativeVideoFrame | undefined
  let jpeg: Buffer | undefined
  let jpegRevision = 0
  let encodedAt = 0
  let encodeRequested = false
  const frameWaiters = new Set<() => void>()
  let resizing = false
  let resolutionMode: NativeViewerResolutionMode = config.resolutionMode
  let displayWidth = selected?.viewportSize()?.width ?? browser.source.width
  let displayHeight = selected?.viewportSize()?.height ?? browser.source.height
  const geometrySources = new WeakSet<NativePage>()
  let encoding: Promise<void> | undefined
  let rgbScratch: Buffer | undefined
  let rgbIdleTimer: ReturnType<typeof setTimeout> | undefined
  let encodeTimer: ReturnType<typeof setTimeout> | undefined
  let detachFrame: (() => void) | undefined
  let closing: Promise<void> | undefined
  let actionBusy = false
  let activeAction: Promise<void> | undefined
  let resolveClosed: () => void = () => undefined
  const closed = new Promise<void>((resolve) => {
    resolveClosed = resolve
  })
  const heldKeys = new Set<string>()
  const heldButtons = new Set<z.infer<typeof button>>()
  const allSockets = new Set<Socket>()
  const server = createServer({ requestTimeout: 10000, headersTimeout: 10000, maxHeaderSize: 8192 })
  server.maxConnections = 32
  let origin = ''

  function releaseRGB(): void {
    // An active job retains its lease until packing and Sharp have settled.
    clearTimeout(rgbIdleTimer)
    rgbIdleTimer = undefined
    rgbScratch = undefined
  }
  function releaseIdleRGB(): void {
    // Reuse across neighboring polls, then release when client demand pauses.
    if (!rgbScratch || rgbIdleTimer || encoding || encodeRequested || frameWaiters.size) return
    rgbIdleTimer = setTimeout(() => {
      rgbIdleTimer = undefined
      if (!encoding && !encodeRequested && !frameWaiters.size) rgbScratch = undefined
    }, 250)
    rgbIdleTimer.unref()
  }

  function scheduleEncode(): void {
    if (closing || encoding || encodeTimer || !latest || (!encodeRequested && !frameWaiters.size))
      return
    const remaining = Math.max(0, 1000 / config.framesPerSecond - (Date.now() - encodedAt))
    encodeTimer = setTimeout(() => {
      encodeTimer = undefined
      if (closing || (!encodeRequested && !frameWaiters.size)) return
      const frame = latest,
        generation = revision
      latest = undefined
      if (!frame || closing) return
      encodeRequested = false
      encodedAt = Date.now()
      clearTimeout(rgbIdleTimer)
      rgbIdleTimer = undefined
      // Claim the job before packing can yield; close awaits packing and the native codec.
      const isCurrent = (): boolean => !closing && revision === generation
      encoding = Promise.resolve()
        .then(async () => {
          const rgb = await nativeViewerRGBAsync(frame, isCurrent, (bytes) => {
            if (!rgbScratch || rgbScratch.length < bytes) rgbScratch = Buffer.allocUnsafe(bytes)
            return rgbScratch
          })
          if (!rgb || !isCurrent()) return undefined
          return sharp(rgb.data, {
            raw: { width: rgb.width, height: rgb.height, channels: 3 },
            limitInputPixels: 3840 * 2160,
          })
            .jpeg({ quality: config.quality, chromaSubsampling: '4:2:0' })
            .toBuffer()
        })
        .then(
          (bytes) => {
            if (bytes && isCurrent()) {
              jpeg = bytes
              jpegRevision = generation
              ++frameSequence
              frameError = null
              notifyFrames()
            }
          },
          (error: UntrustedInput) => {
            frameError = toError(error).message
          },
        )
        .finally(() => {
          encoding = undefined
          scheduleEncode()
          releaseIdleRGB()
        })
    }, remaining)
    encodeTimer.unref()
  }
  function notifyFrames(): void {
    for (const notify of frameWaiters) notify()
  }
  function attachFrame(): void {
    releaseRGB()
    detachFrame?.()
    detachFrame = undefined
    latest = undefined
    jpeg = undefined
    notifyFrames()
    if (selected && !selected.isClosed()) {
      detachFrame = selected.source.onFrame((frame) => {
        if (frame.pixelFormat !== 'bgra' || frame.width * frame.height > 3840 * 2160) return
        if (frame.viewport !== undefined && frame.contentRect && selected)
          geometrySources.add(selected)
        if (resizing) return
        if (syncViewport()) return
        const expected = selected?.viewportSize()
        if (!expected) return
        if (frame.viewport === null) return
        if (
          frame.viewport &&
          (frame.viewport.width !== expected.width || frame.viewport.height !== expected.height)
        )
          return
        if (
          frame.viewport === undefined &&
          selected?.source.viewport &&
          (selected.source.viewport.width !== expected.width ||
            selected.source.viewport.height !== expected.height)
        ) {
          frameError = 'Update the native helper to view resized viewport geometry'
          return
        }
        // Share the source's cached frame, including for a late viewer of a
        // paused page. Encoding still requires explicit current client demand.
        latest = frame
        scheduleEncode()
      })
    }
  }
  function currentPage(): NativePage {
    if (!selected || selected.isClosed())
      throw new ViewerHTTPError(409, 'Select an open browser tab')
    return selected
  }
  function state(): NativeBrowserViewerState {
    syncViewport()
    const viewport = selected?.viewportSize()
    const limits = viewportLimits()
    return {
      mode: gate.mode,
      owner,
      revision,
      pages: pages().map((page) => ({ id: page.source.pageId ?? 1, url: page.url() })),
      pageId: selected && !selected.isClosed() ? (selected.source.pageId ?? 1) : null,
      width: viewport?.width ?? selected?.source.width ?? browser.source.width,
      height: viewport?.height ?? selected?.source.height ?? browser.source.height,
      frameSequence,
      frameError,
      resolutionMode,
      ...limits,
      canResize: !!selected && geometrySources.has(selected),
    }
  }
  function viewportLimits(): { maxWidth: number; maxHeight: number } {
    const source = selected?.source ?? browser.source
    const original = source.viewport
    const scale =
      (selected && nativeContextDeviceScaleFactor(selected.context())) ??
      (original ? Math.max(source.width / original.width, source.height / original.height) : 1)
    return {
      maxWidth: Math.min(3840, Math.floor(3840 / scale)),
      maxHeight: Math.min(2160, Math.floor(2160 / scale)),
    }
  }
  function syncViewport(): boolean {
    if (resizing) return false
    const viewport = selected?.viewportSize()
    if (viewport && (viewport.width !== displayWidth || viewport.height !== displayHeight)) {
      displayWidth = viewport.width
      displayHeight = viewport.height
      ++revision
      attachFrame()
      return true
    }
    return false
  }
  async function resizeViewport(
    width: number,
    height: number,
    mode: NativeViewerResolutionMode,
  ): Promise<void> {
    const page = currentPage(),
      before = page.viewportSize(),
      limits = viewportLimits()
    if (!before || !geometrySources.has(page))
      throw new ViewerHTTPError(
        409,
        'Update the native helper: live resizing requires frame viewport geometry',
      )
    if (mode === 'auto') {
      width = Math.min(width, limits.maxWidth)
      height = Math.min(height, limits.maxHeight)
    }
    if (width > limits.maxWidth || height > limits.maxHeight)
      throw new ViewerHTTPError(
        400,
        `Viewport exceeds ${limits.maxWidth} × ${limits.maxHeight} at this device scale`,
      )
    if (width === before.width && height === before.height) {
      resolutionMode = mode
      return
    }
    await resetInput()
    ++revision
    latest = undefined
    jpeg = undefined
    resizing = true
    try {
      await page.setViewportSize({ width, height })
      resolutionMode = mode
    } catch (error) {
      try {
        await page.setViewportSize(before)
      } catch {
        await browser.close()
      }
      throw error
    } finally {
      resizing = false
      const actual = page.viewportSize()
      displayWidth = actual?.width ?? before.width
      displayHeight = actual?.height ?? before.height
      attachFrame()
    }
  }
  function transition(mode: NativeViewerMode): void {
    ++revision
    gate.transition(mode)
    jpeg = undefined
    attachFrame()
  }
  async function resetInput(): Promise<void> {
    if (!selected || selected.isClosed()) {
      heldKeys.clear()
      heldButtons.clear()
      return
    }
    // Keep an entry until its actual native release acknowledges success.
    for (const name of [...heldKeys].reverse()) {
      await selected.keyboard.up(name)
      heldKeys.delete(name)
    }
    for (const name of heldButtons) {
      await selected.mouse.up({ button: name })
      heldButtons.delete(name)
    }
  }
  async function nativeIdle(signal: AbortSignal): Promise<void> {
    do {
      signal.throwIfAborted()
      const result = z
        .strictObject({ viewerIdleVersion: z.literal(1), idle: z.boolean() })
        .parse(await browser.source.runAutomation('viewerIdle', { timeoutMs: 2000 }, signal))
      if (result.idle) {
        for (const page of pages()) {
          const input = z
            .object({
              pressedKeys: z.number(),
              keyModifiers: z.number(),
              mouseButtons: z.number(),
              touchContactHeld: z.boolean(),
            })
            .parse(await page.source.runAutomation('diagnostics', { timeoutMs: 2000 }, signal))
          if (
            input.pressedKeys ||
            input.keyModifiers ||
            input.mouseButtons ||
            input.touchContactHeld
          )
            throw new Error(
              'Release held agent keys, pointer buttons or touch contacts before transferring control',
            )
        }
        return
      }
      await delay(20, undefined, { signal })
    } while (!signal.aborted)
    signal.throwIfAborted()
  }
  async function takeover(clientId: string): Promise<void> {
    if (gate.mode !== 'agent' && !(gate.mode === 'human' && owner === null))
      throw new ViewerHTTPError(409, 'Another viewer owns or is taking browser control')
    const previous = gate.mode
    owner = clientId
    ownerSeen = Date.now()
    transition('taking-over')
    try {
      const signal = AbortSignal.any([
        browser.source.processSignal,
        AbortSignal.timeout(config.takeoverTimeoutMs),
      ])
      await gate.drain(signal)
      await gate.runViewer(() => nativeIdle(signal))
      transition('human')
    } catch (error) {
      owner = null
      transition(previous)
      throw new ViewerHTTPError(
        409,
        `Browser control could not transfer while native work is active: ${toError(error as UntrustedInput).message}`,
      )
    }
  }
  async function release(resumeAgent: boolean): Promise<void> {
    transition('releasing')
    try {
      await gate.runViewer(async () => {
        await resetInput()
        await nativeIdle(
          AbortSignal.any([
            browser.source.processSignal,
            AbortSignal.timeout(config.takeoverTimeoutMs),
          ]),
        )
      })
      owner = null
      transition(resumeAgent ? 'agent' : 'human')
    } catch (error) {
      transition('human')
      ownerSeen = Date.now()
      throw error
    }
  }
  function authorized(request: IncomingMessage): void {
    if (request.headers.host !== new URL(origin).host)
      throw new ViewerHTTPError(403, 'Invalid viewer host')
    if (request.headers.origin && request.headers.origin !== origin)
      throw new ViewerHTTPError(403, 'Invalid viewer origin')
    const provided = Buffer.from(request.headers.authorization ?? '')
    if (provided.length !== tokenBytes.length || !timingSafeEqual(provided, tokenBytes))
      throw new ViewerHTTPError(401, 'Open the complete viewer link to authenticate')
  }
  async function action(input: UntrustedInput): Promise<void> {
    syncViewport()
    const parsed = Action.safeParse(input)
    if (!parsed.success) throw new ViewerHTTPError(400, 'Invalid viewer action')
    const command = parsed.data
    if (!clients.has(command.clientId))
      throw new ViewerHTTPError(401, 'Viewer session expired; reconnect')
    clients.set(command.clientId, Date.now())
    if (command.revision !== revision)
      throw new ViewerHTTPError(409, 'Viewer state changed; observe again')
    if (command.type === 'heartbeat') {
      if (owner === command.clientId) ownerSeen = Date.now()
      return
    }
    if (command.type === 'takeover') {
      await takeover(command.clientId)
      return
    }
    if (owner !== command.clientId || gate.mode !== 'human')
      throw new ViewerHTTPError(409, 'Take control before interacting')
    ownerSeen = Date.now()
    if (command.type === 'release') {
      await release(true)
      return
    }
    await gate.runViewer(async () => {
      if (command.type === 'resetInput') {
        await resetInput()
        return
      }
      if (command.type === 'resize') {
        await resizeViewport(command.width, command.height, command.mode)
        return
      }
      if (command.type === 'selectPage') {
        const page = pages().find((page) => page.source.pageId === command.pageId)
        if (!page) throw new ViewerHTTPError(404, 'Browser tab closed')
        if (!page.viewportSize())
          throw new ViewerHTTPError(409, 'Browser viewer requires a fixed page viewport')
        await resetInput()
        selected = page
        displayWidth = page.viewportSize()?.width ?? page.source.width
        displayHeight = page.viewportSize()?.height ?? page.source.height
        ++revision
        attachFrame()
        return
      }
      const page = currentPage()
      switch (command.type) {
        case 'navigate': {
          let url: URL
          try {
            url = new URL(command.url)
          } catch {
            throw new ViewerHTTPError(400, 'Enter a complete http or https URL')
          }
          if (!['http:', 'https:'].includes(url.protocol))
            throw new ViewerHTTPError(400, 'Only HTTP(S) navigation is supported')
          await resetInput()
          ++revision
          jpeg = undefined
          await page.goto(url.href, { timeout: 15000, waitUntil: 'domcontentloaded' })
          break
        }
        case 'back':
        case 'forward':
        case 'reload':
          await resetInput()
          ++revision
          jpeg = undefined
          if (command.type === 'back')
            await page.goBack({ timeout: 15000, waitUntil: 'domcontentloaded' })
          else if (command.type === 'forward')
            await page.goForward({ timeout: 15000, waitUntil: 'domcontentloaded' })
          else await page.reload({ timeout: 15000, waitUntil: 'domcontentloaded' })
          break
        case 'pointer': {
          const viewport = state()
          if (command.x >= viewport.width || command.y >= viewport.height)
            throw new ViewerHTTPError(400, 'Pointer is outside the browser viewport')
          await page.mouse.move(command.x, command.y)
          if (command.action === 'down') {
            if (heldButtons.has(command.button))
              throw new ViewerHTTPError(409, 'Pointer button is already held')
            heldButtons.add(command.button)
            await page.mouse.down({ button: command.button })
          } else if (command.action === 'up') {
            await page.mouse.up({ button: command.button })
            heldButtons.delete(command.button)
          }
          break
        }
        case 'wheel':
          await page.mouse.wheel(command.deltaX, command.deltaY)
          break
        case 'key':
          if (command.action === 'down') {
            if (heldKeys.size >= 32 && !heldKeys.has(command.key))
              throw new ViewerHTTPError(429, 'Too many held keys')
            heldKeys.add(command.key)
            await page.keyboard.down(command.key)
          } else {
            await page.keyboard.up(command.key)
            heldKeys.delete(command.key)
          }
          break
        case 'text':
          await page.keyboard.insertText(command.text)
          break
        default:
          break
      }
    })
  }
  async function handle(request: IncomingMessage, response: ServerResponse): Promise<void> {
    response.setHeader('Cache-Control', 'no-store')
    response.setHeader('X-Content-Type-Options', 'nosniff')
    response.setHeader('Referrer-Policy', 'no-referrer')
    response.setHeader(
      'Content-Security-Policy',
      "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self' blob:; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'",
    )
    if (request.headers.host !== new URL(origin).host)
      throw new ViewerHTTPError(403, 'Invalid viewer host')
    const url = new URL(request.url ?? '/', origin)
    if (request.method === 'GET' && ['/', '/viewer.js', '/viewer.css'].includes(url.pathname)) {
      response.writeHead(200, {
        'Content-Type':
          url.pathname === '/'
            ? 'text/html; charset=utf-8'
            : url.pathname.endsWith('.js')
              ? 'text/javascript; charset=utf-8'
              : 'text/css; charset=utf-8',
      })
      response.end(
        url.pathname === '/'
          ? nativeViewerHTML()
          : url.pathname.endsWith('.js')
            ? nativeViewerScript()
            : nativeViewerStyles(),
      )
      return
    }
    authorized(request)
    if (closing) throw new ViewerHTTPError(410, 'Browser viewer closed')
    if (request.method === 'POST' && url.pathname === '/session') {
      if (clients.size >= 8) throw new ViewerHTTPError(429, 'Viewer allows eight connected clients')
      const clientId = randomBytes(16).toString('hex')
      clients.set(clientId, Date.now())
      sendJSON(response, 200, { clientId })
      return
    }
    if (request.method === 'GET' && url.pathname === '/state') {
      sendJSON(response, 200, state())
      return
    }
    if (request.method === 'GET' && url.pathname === '/frame') {
      syncViewport()
      const generation = Number(url.searchParams.get('revision'))
      const after = Number(url.searchParams.get('after') ?? -1)
      const available = (): boolean =>
        !!jpeg && jpegRevision === revision && generation === revision && after < frameSequence
      const sendFrame = (): void => {
        if (!available() || !jpeg) {
          response.writeHead(204)
          response.end()
          return
        }
        response.writeHead(200, {
          'Content-Type': 'image/jpeg',
          'Content-Length': jpeg.length,
          'X-Frame-Sequence': frameSequence,
          'X-Viewer-Revision': revision,
        })
        response.end(jpeg)
      }
      if (available() || generation !== revision || url.searchParams.get('wait') !== '1') {
        if (!available() && generation === revision) encodeRequested = true
        scheduleEncode()
        sendFrame()
        return
      }
      if (frameWaiters.size >= 32) throw new ViewerHTTPError(429, 'Too many pending viewer frames')
      // At most one bounded wait per polling client. A new JPEG wakes all clients;
      // disconnect and shutdown remove their listeners without retaining frames.
      await new Promise<void>((resolve) => {
        const finish = (): void => {
          clearTimeout(timeout)
          frameWaiters.delete(notify)
          releaseIdleRGB()
          response.removeListener('close', finish)
          resolve()
        }
        const notify = (): void => {
          if (available() || generation !== revision || closing) finish()
        }
        const timeout = setTimeout(finish, 1000)
        timeout.unref()
        frameWaiters.add(notify)
        response.once('close', finish)
        scheduleEncode()
      })
      if (!response.destroyed) sendFrame()
      return
    }
    if (request.method === 'POST' && url.pathname === '/action') {
      if (actionBusy) throw new ViewerHTTPError(429, 'Wait for the current viewer action to finish')
      actionBusy = true
      try {
        const input = await body(request)
        if (closing) throw new ViewerHTTPError(410, 'Browser viewer closed')
        activeAction = action(input)
        await activeAction
        if (!response.destroyed) sendJSON(response, 200, state())
      } finally {
        activeAction = undefined
        actionBusy = false
      }
      return
    }
    throw new ViewerHTTPError(404, 'Unknown viewer endpoint')
  }
  server.on('request', (request, response) => {
    void handle(request, response).catch((error: UntrustedInput) => {
      if (!response.headersSent && !response.destroyed)
        sendJSON(response, error instanceof ViewerHTTPError ? error.status : 500, {
          error: toError(error).message,
        })
      else response.destroy()
    })
  })
  server.on('connection', (socket) => {
    allSockets.add(socket)
    socket.on('close', () => allSockets.delete(socket))
  })
  const maintenance = setInterval(() => {
    const now = Date.now()
    for (const [id, seen] of clients) if (id !== owner && now - seen > 60000) clients.delete(id)
    if (owner && gate.mode === 'human' && now - ownerSeen > 20000 && !actionBusy) {
      actionBusy = true
      activeAction = release(false)
        .catch((error: UntrustedInput) => {
          frameError = `Input release failed: ${toError(error).message}`
        })
        .finally(() => {
          activeAction = undefined
          actionBusy = false
        })
    }
  }, 1000)
  maintenance.unref()
  const abort = (): void => {
    void close().catch(() => undefined)
  }
  function close(): Promise<void> {
    if (closing) return closing
    closing = (async () => {
      clearInterval(maintenance)
      clearTimeout(encodeTimer)
      releaseRGB()
      detachFrame?.()
      options.signal?.removeEventListener('abort', abort)
      browser.source.processSignal.removeEventListener('abort', abort)
      const serverClosed = new Promise<void>((resolve) => {
        server.close(() => resolve())
      })
      for (const socket of allSockets) socket.destroy()
      try {
        await activeAction?.catch(() => undefined)
        if (browser.isConnected() && gate.mode !== 'agent') {
          gate.transition('releasing')
          try {
            await gate.runViewer(async () => {
              await resetInput()
              await nativeIdle(
                AbortSignal.any([
                  browser.source.processSignal,
                  AbortSignal.timeout(config.takeoverTimeoutMs),
                ]),
              )
            })
          } catch (error) {
            // Never reopen AI admission after an uncertain human input release.
            await browser.close()
            throw error
          }
        }
        if (encoding) await encoding
      } finally {
        await serverClosed
        gate.dispose()
        owner = null
        clients.clear()
        jpeg = undefined
        latest = undefined
        resolveClosed()
      }
    })()
    return closing
  }
  try {
    await new Promise<void>((resolve, reject) => {
      const failed = (error: Error): void => {
        server.removeListener('listening', ready)
        reject(error)
      }
      const ready = (): void => {
        server.removeListener('error', failed)
        resolve()
      }
      server.once('error', failed)
      server.once('listening', ready)
      server.listen(config.port, '127.0.0.1')
    })
    const address = server.address()
    if (!address || typeof address === 'string')
      throw new Error('Viewer did not bind a local TCP port')
    origin = `http://127.0.0.1:${address.port}`
    attachFrame()
    options.signal?.addEventListener('abort', abort, { once: true })
    browser.source.processSignal.addEventListener('abort', abort, { once: true })
    if (options.signal?.aborted || browser.source.processSignal.aborted) await close()
    return {
      url: `${origin}/#${token}`,
      closed,
      state,
      runAgent: (step, signal) => gate.runAgent(step, signal),
      close,
    }
  } catch (error) {
    await close()
    throw error
  }
}
