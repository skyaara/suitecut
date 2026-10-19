// SPDX-License-Identifier: MIT
import { createCDPDialogControls } from './browser-cdp-dialogs.js'
import { createCDPFileControls } from './browser-cdp-files.js'
import { createCDPFrames } from './browser-cdp-frames.js'
import {
  type BrowserCookie,
  type BrowserProtocolDriver,
  type BrowserScreenshotOptions,
  type DriverPage,
} from './browser-protocol-types.js'
import { type BrowserWire, protocolRecord, protocolString } from './browser-wire.js'
import { type UntrustedInput } from './untrusted.js'

interface Session {
  id: string
  invalidate: Set<(error: Error) => void>
}
type Target = DriverPage & { type: string }

const modifierKeys: Record<string, { bit: number; key: string; code: string; virtual: number }> = {
  Alt: { bit: 1, key: 'Alt', code: 'AltLeft', virtual: 18 },
  Control: { bit: 2, key: 'Control', code: 'ControlLeft', virtual: 17 },
  Ctrl: { bit: 2, key: 'Control', code: 'ControlLeft', virtual: 17 },
  Meta: { bit: 4, key: 'Meta', code: 'MetaLeft', virtual: 91 },
  Shift: { bit: 8, key: 'Shift', code: 'ShiftLeft', virtual: 16 },
}
const specialKeys: Record<string, { key: string; code: string; virtual: number; text?: string }> = {
  Enter: { key: 'Enter', code: 'Enter', virtual: 13, text: '\r' },
  Return: { key: 'Enter', code: 'Enter', virtual: 13, text: '\r' },
  Tab: { key: 'Tab', code: 'Tab', virtual: 9 },
  Escape: { key: 'Escape', code: 'Escape', virtual: 27 },
  Esc: { key: 'Escape', code: 'Escape', virtual: 27 },
  Backspace: { key: 'Backspace', code: 'Backspace', virtual: 8 },
  Delete: { key: 'Delete', code: 'Delete', virtual: 46 },
  ArrowLeft: { key: 'ArrowLeft', code: 'ArrowLeft', virtual: 37 },
  ArrowUp: { key: 'ArrowUp', code: 'ArrowUp', virtual: 38 },
  ArrowRight: { key: 'ArrowRight', code: 'ArrowRight', virtual: 39 },
  ArrowDown: { key: 'ArrowDown', code: 'ArrowDown', virtual: 40 },
  Home: { key: 'Home', code: 'Home', virtual: 36 },
  End: { key: 'End', code: 'End', virtual: 35 },
  PageUp: { key: 'PageUp', code: 'PageUp', virtual: 33 },
  PageDown: { key: 'PageDown', code: 'PageDown', virtual: 34 },
  Space: { key: ' ', code: 'Space', virtual: 32, text: ' ' },
}

const punctuationKeys: Record<string, { code: string; virtual: number; shifted: string }> = {
  '`': { code: 'Backquote', virtual: 192, shifted: '~' },
  '-': { code: 'Minus', virtual: 189, shifted: '_' },
  '=': { code: 'Equal', virtual: 187, shifted: '+' },
  '[': { code: 'BracketLeft', virtual: 219, shifted: '{' },
  ']': { code: 'BracketRight', virtual: 221, shifted: '}' },
  '\\': { code: 'Backslash', virtual: 220, shifted: '|' },
  ';': { code: 'Semicolon', virtual: 186, shifted: ':' },
  "'": { code: 'Quote', virtual: 222, shifted: '"' },
  ',': { code: 'Comma', virtual: 188, shifted: '<' },
  '.': { code: 'Period', virtual: 190, shifted: '>' },
  '/': { code: 'Slash', virtual: 191, shifted: '?' },
}

function protocolArray(value: UntrustedInput, label: string): UntrustedInput[] {
  if (!Array.isArray(value)) throw new Error(`Invalid CDP ${label}`)
  return value as UntrustedInput[]
}

function targetInfo(input: UntrustedInput): Target {
  const info = protocolRecord(input, 'CDP target info')
  return {
    id: protocolString(info.targetId, 'CDP target id'),
    contextId: typeof info.browserContextId === 'string' ? info.browserContextId : '',
    url: protocolString(info.url, 'CDP target URL'),
    type: protocolString(info.type, 'CDP target type'),
    title: typeof info.title === 'string' ? info.title : '',
  }
}

/** Drive Chromium browsers through one multiplexed browser-level CDP connection. */
export function createCDPDriver(wire: BrowserWire): BrowserProtocolDriver {
  const targets = new Map<string, Target>()
  const contexts = new Set<string>([''])
  const destroyed = new Set<string>()
  const sessions = new Map<string, Promise<Session>>()
  const activeSessions = new Map<string, Session>()
  const removers: (() => void)[] = []
  const frameControls = createCDPFrames(wire)
  const fileControls = createCDPFileControls(wire, request, {
    pageInfo: (id) => targets.get(id),
    pageForFrame: frameControls.pageForFrame,
    preparePage: async (id) => {
      await sessionFor(id)
    },
  })
  const dialogControls = createCDPDialogControls(wire, request, {
    pageForSession: frameControls.pageForSession,
  })
  let initialization: Promise<void> | undefined
  let discovering = true
  let inputTail: Promise<void> = Promise.resolve()
  let queuedInputs = 0

  // Chromium shares focus across targets. Keep activation and each complete native
  // gesture together; document evaluation and locator waits remain independent.
  function input<T>(id: string, action: () => Promise<T>): Promise<T> {
    if (queuedInputs >= 256) return Promise.reject(new Error('Browser input queue limit reached'))
    queuedInputs++
    const work = inputTail.then(async () => {
      await request(id, 'Page.bringToFront')
      return action()
    })
    inputTail = work.then(
      () => {
        queuedInputs--
      },
      () => {
        queuedInputs--
      },
    )
    return work
  }

  function invalidate(id: string, error: Error): void {
    sessions.delete(id)
    dialogControls.closePage(id)
    frameControls.closePage(id)
    const session = activeSessions.get(id)
    activeSessions.delete(id)
    if (session) for (const reject of session.invalidate) reject(error)
  }
  function remember(input: UntrustedInput): void {
    const target = targetInfo(input)
    if (target.type === 'page') {
      targets.set(target.id, target)
      contexts.add(target.contextId)
      destroyed.delete(target.id)
    }
  }
  removers.push(wire.on('Target.targetCreated', (event) => remember(event.targetInfo)))
  removers.push(wire.on('Target.targetInfoChanged', (event) => remember(event.targetInfo)))
  removers.push(
    wire.on('Target.targetDestroyed', (event) => {
      const id = protocolString(event.targetId, 'Destroyed CDP target')
      targets.delete(id)
      if (discovering) destroyed.add(id)
      invalidate(id, new Error('Browser page closed'))
    }),
  )
  removers.push(
    wire.on('Target.detachedFromTarget', (event) => {
      if (typeof event.sessionId === 'string') dialogControls.detachSession(event.sessionId)
      for (const [id, session] of activeSessions) {
        if (session.id === event.sessionId)
          invalidate(id, new Error('Browser page session detached'))
      }
    }),
  )
  void wire.closed.then(() => {
    for (const remove of removers) remove()
    for (const id of sessions.keys()) invalidate(id, new Error('Browser connection closed'))
    dialogControls.dispose()
    void fileControls.dispose().catch(() => undefined)
    frameControls.dispose()
    targets.clear()
    contexts.clear()
    destroyed.clear()
  })

  async function sessionFor(id: string): Promise<Session> {
    if (!targets.has(id)) throw new Error('Unknown or closed browser page')
    const cached = sessions.get(id)
    if (cached) return cached
    const pending = (async (): Promise<Session> => {
      const result = await wire.request('Target.attachToTarget', { targetId: id, flatten: true })
      const session: Session = {
        id: protocolString(result.sessionId, 'CDP session id'),
        invalidate: new Set(),
      }
      if (!targets.has(id) || !wire.isConnected())
        throw new Error('Browser page closed while attaching')
      activeSessions.set(id, session)
      const frameReady = frameControls.register(id, session.id)
      await wire.request('Page.enable', {}, { sessionId: session.id })
      await wire.request(
        'Page.setLifecycleEventsEnabled',
        { enabled: true },
        { sessionId: session.id },
      )
      await frameReady
      if (activeSessions.get(id) !== session)
        throw new Error('Browser page session detached while attaching')
      return session
    })()
    sessions.set(id, pending)
    try {
      return await pending
    } catch (error) {
      if (sessions.get(id) === pending) {
        const active = activeSessions.get(id)
        invalidate(id, new Error('Browser page initialization failed'))
        if (active && wire.isConnected()) {
          await wire
            .request('Target.detachFromTarget', { sessionId: active.id })
            .catch(() => undefined)
        }
      }
      throw error
    }
  }
  async function request(
    id: string,
    method: string,
    params: Record<string, UntrustedInput> = {},
    frameId?: string,
  ): Promise<Record<string, UntrustedInput>> {
    const session = await sessionFor(id)
    if (!frameId) return wire.request(method, params, { sessionId: session.id })
    const route = await frameControls.route(id, frameId)
    return wire.request(
      method,
      {
        ...params,
        ...(method === 'Runtime.evaluate' && route.contextId !== undefined
          ? { contextId: route.contextId }
          : {}),
      },
      { sessionId: route.sessionId },
    )
  }
  async function evaluate(
    id: string,
    expression: string,
    frameId?: string,
  ): Promise<UntrustedInput> {
    const response = await request(
      id,
      'Runtime.evaluate',
      {
        expression,
        awaitPromise: true,
        returnByValue: true,
        userGesture: true,
      },
      frameId,
    )
    if (response.exceptionDetails) {
      const details = protocolRecord(response.exceptionDetails, 'CDP exception')
      const exception = details.exception
        ? protocolRecord(details.exception, 'CDP exception value')
        : undefined
      throw new Error(
        typeof exception?.description === 'string'
          ? exception.description
          : typeof details.text === 'string'
            ? details.text
            : 'Browser evaluation failed',
      )
    }
    const result = protocolRecord(response.result, 'CDP evaluation result')
    if (typeof result.unserializableValue === 'string') {
      switch (result.unserializableValue) {
        case 'NaN':
          return NaN
        case 'Infinity':
          return Infinity
        case '-Infinity':
          return -Infinity
        case '-0':
          return -0
        default:
          if (/^-?\d+n$/u.test(result.unserializableValue))
            return BigInt(result.unserializableValue.slice(0, -1))
          throw new Error('Unsupported CDP unserializable evaluation result')
      }
    }
    return result.value
  }

  const driver: BrowserProtocolDriver = {
    backend: 'cdp',
    wire,
    version: '',
    capabilities: {
      backend: 'cdp',
      isolatedContexts: true,
      viewport: true,
      screenshots: true,
      fullPageScreenshot: true,
      cookies: true,
      nativeInput: true,
      rawProtocol: true,
      nativeCapture: false,
      nativeViewer: false,
      frames: true,
      downloads: true,
      uploads: true,
      dialogs: true,
    },
    initialize() {
      initialization ??= (async () => {
        const version = await wire.request('Browser.getVersion')
        driver.version = protocolString(version.product, 'CDP browser version')
        await wire.request('Target.setDiscoverTargets', { discover: true })
        const [snapshot, knownContexts] = await Promise.all([
          wire.request('Target.getTargets'),
          wire.request('Target.getBrowserContexts'),
        ])
        for (const info of protocolArray(snapshot.targetInfos, 'targets response')) {
          const target = targetInfo(info)
          if (!destroyed.has(target.id) && !targets.has(target.id)) remember(info)
        }
        for (const id of protocolArray(knownContexts.browserContextIds, 'contexts response'))
          contexts.add(protocolString(id, 'CDP context id'))
        discovering = false
        destroyed.clear()
      })()
      return initialization
    },
    async contexts() {
      // CDP has target-destroyed events, but no browser-context-destroyed event.
      // Refresh only this explicit listing operation, never ordinary actions.
      const before = new Set(contexts)
      const response = await wire.request('Target.getBrowserContexts')
      const current = new Set<string>([''])
      for (const id of protocolArray(response.browserContextIds, 'contexts response')) {
        const contextId = protocolString(id, 'CDP context id')
        if (!before.has(contextId) || contexts.has(contextId)) current.add(contextId)
      }
      // Preserve creations and disposals completed while this snapshot was in flight.
      for (const id of contexts) if (!before.has(id)) current.add(id)
      contexts.clear()
      for (const id of current) contexts.add(id)
      for (const id of before) if (!current.has(id)) fileControls.retireContext(id)
      for (const [id, target] of targets) {
        if (!current.has(target.contextId)) {
          targets.delete(id)
          invalidate(id, new Error('Browser context closed'))
        }
      }
      return [...contexts]
    },
    pages() {
      return Promise.resolve(
        [...targets.values()].map(({ id, contextId, url }) => ({ id, contextId, url })),
      )
    },
    async newContext() {
      const response = await wire.request('Target.createBrowserContext')
      const id = protocolString(response.browserContextId, 'CDP context id')
      contexts.add(id)
      return id
    },
    async closeContext(id) {
      if (id === '') throw new Error('Cannot dispose the default browser context')
      await wire.request('Target.disposeBrowserContext', { browserContextId: id })
      fileControls.retireContext(id)
      contexts.delete(id)
      for (const [pageId, target] of targets)
        if (target.contextId === id) {
          targets.delete(pageId)
          invalidate(pageId, new Error('Browser context closed'))
        }
    },
    async newPage(contextId = '') {
      const closedDuringCreation = new Set<string>()
      const remove = wire.on('Target.targetDestroyed', (event) => {
        if (typeof event.targetId === 'string') closedDuringCreation.add(event.targetId)
      })
      try {
        const result = await wire.request('Target.createTarget', {
          url: 'about:blank',
          background: true,
          ...(contextId ? { browserContextId: contextId } : {}),
        })
        const id = protocolString(result.targetId, 'CDP target id')
        if (closedDuringCreation.has(id)) throw new Error('Browser page closed during creation')
        const page = targets.get(id) ?? {
          id,
          contextId,
          url: 'about:blank',
          title: '',
          type: 'page',
        }
        targets.set(id, page)
        return { id, contextId: page.contextId, url: page.url }
      } finally {
        remove()
      }
    },
    async closePage(id) {
      const result = await wire.request('Target.closeTarget', { targetId: id })
      if (result.success === false) throw new Error('Browser refused to close page')
      targets.delete(id)
      invalidate(id, new Error('Browser page closed'))
    },
    async navigate(id, url, wait) {
      const session = await sessionFor(id)
      if (wait === 'none') {
        const result = await wire.request('Page.navigate', { url }, { sessionId: session.id })
        if (typeof result.errorText === 'string' && result.errorText)
          throw new Error(result.errorText)
        return
      }
      const lifecycle = wait === 'interactive' ? 'DOMContentLoaded' : 'load'
      let loader: string | undefined
      const seen = new Set<string>()
      let finish: (() => void) | undefined
      let fail: ((error: Error) => void) | undefined
      const controller = new AbortController()
      const waiting = new Promise<void>((resolve, reject) => {
        finish = resolve
        fail = reject
      })
      // An event may arrive before the navigate response identifies its loader.
      const remove = wire.on('Page.lifecycleEvent', (event, sessionId) => {
        if (
          sessionId !== session.id ||
          event.name !== lifecycle ||
          typeof event.loaderId !== 'string'
        )
          return
        seen.add(event.loaderId)
        if (event.loaderId === loader) finish?.()
      })
      const reject = (error: Error): void => {
        fail?.(error)
        controller.abort(error)
      }
      session.invalidate.add(reject)
      const timeoutMs = wire.timeoutMs
      const timer = setTimeout(
        () => reject(new Error(`Browser navigation timed out after ${timeoutMs}ms`)),
        timeoutMs,
      )
      // Mark as observed immediately, including a detach during the navigate request.
      void waiting.catch(() => undefined)
      try {
        const result = await wire.request(
          'Page.navigate',
          { url },
          { sessionId: session.id, signal: controller.signal },
        )
        if (typeof result.errorText === 'string' && result.errorText)
          throw new Error(result.errorText)
        if (typeof result.loaderId === 'string') {
          loader = result.loaderId
          if (seen.has(loader)) finish?.()
          await waiting
        }
        // No loader means a same-document navigation, which is already committed.
      } finally {
        clearTimeout(timer)
        remove()
        session.invalidate.delete(reject)
      }
    },
    evaluate,
    setInputFiles: (...args) => fileControls.setInputFiles(...args),
    subscribeDownloads: (...args) => fileControls.subscribeDownloads(...args),
    subscribeDialogs: (...args) => dialogControls.subscribe(...args),
    async disposeResources() {
      dialogControls.dispose()
      frameControls.dispose()
      await fileControls.dispose()
    },
    pageInfo(id) {
      const target = targets.get(id)
      if (!target) return undefined
      return {
        id: target.id,
        contextId: target.contextId,
        url: target.url,
        ...(target.title !== undefined ? { title: target.title } : {}),
      }
    },
    async pageTitle(id) {
      if (!targets.has(id)) throw new Error('Unknown or closed browser page')
      const result = await wire.request('Target.getTargetInfo', { targetId: id })
      const info = protocolRecord(result.targetInfo, 'CDP page info')
      const title = protocolString(info.title, 'CDP page title')
      const current = targets.get(id)
      if (!current) throw new Error('Browser page closed while reading title')
      current.title = title
      return title
    },
    async preparePage(id) {
      await sessionFor(id)
    },
    async activatePage(id) {
      await input(id, () => Promise.resolve())
    },
    async frames(id) {
      await sessionFor(id)
      return frameControls.list(id)
    },
    frameInfo: frameControls.info,
    async resolveFrame(id, expression, parentFrameId) {
      await sessionFor(id)
      return frameControls.resolve(id, expression, parentFrameId)
    },
    async screenshot(id, options: BrowserScreenshotOptions) {
      return input(id, async () => {
        const format = options.type ?? 'png'
        const params: Record<string, UntrustedInput> = {
          format,
          captureBeyondViewport: options.fullPage ?? false,
        }
        if (format === 'jpeg' && options.quality !== undefined) params.quality = options.quality
        if (options.fullPage) {
          const metrics = await request(id, 'Page.getLayoutMetrics')
          const size = protocolRecord(
            metrics.cssContentSize ?? metrics.contentSize,
            'CDP page size',
          )
          if (
            typeof size.width !== 'number' ||
            typeof size.height !== 'number' ||
            !Number.isFinite(size.width) ||
            !Number.isFinite(size.height) ||
            size.width <= 0 ||
            size.height <= 0
          ) {
            throw new Error('Invalid CDP page dimensions')
          }
          params.clip = { x: 0, y: 0, width: size.width, height: size.height, scale: 1 }
        }
        const result = await request(id, 'Page.captureScreenshot', params)
        return Buffer.from(protocolString(result.data, 'CDP screenshot data'), 'base64')
      })
    },
    async setViewport(id, viewport) {
      await request(id, 'Emulation.setDeviceMetricsOverride', {
        ...viewport,
        deviceScaleFactor: 1,
        mobile: false,
      })
    },
    async click(id, x, y, button, count, frameId) {
      return input(id, async () => {
        await sessionFor(id)
        const point = await frameControls.point(id, frameId, x, y)
        x = point.x
        y = point.y
        const mouse = (
          params: Record<string, UntrustedInput>,
        ): Promise<Record<string, UntrustedInput>> =>
          wire.request('Input.dispatchMouseEvent', params, { sessionId: point.sessionId })
        const buttons = button === 'left' ? 1 : button === 'right' ? 2 : 4
        await mouse({ type: 'mouseMoved', x, y, button: 'none', buttons: 0 })
        for (let clickCount = 1; clickCount <= count; clickCount++) {
          let released = false
          try {
            await mouse({ type: 'mousePressed', x, y, button, buttons, clickCount })
            await mouse({ type: 'mouseReleased', x, y, button, buttons: 0, clickCount })
            released = true
          } finally {
            if (!released)
              await mouse({ type: 'mouseReleased', x, y, button, buttons: 0, clickCount }).catch(
                () => undefined,
              )
          }
        }
      })
    },
    async clickLocator(id, prepare, frameId, beforeDown) {
      return input(id, async () => {
        await sessionFor(id)
        let candidate = await prepare()
        if ('retry' in candidate) return candidate
        let point = await frameControls.point(id, frameId, candidate.x, candidate.y)
        for (let attempt = 0; attempt < 4; attempt++) {
          await wire.request(
            'Input.dispatchMouseEvent',
            {
              type: 'mouseMoved',
              x: point.x,
              y: point.y,
              button: 'none',
              buttons: 0,
            },
            { sessionId: point.sessionId },
          )
          // Activation and hovering can change focus, scroll and layout. Resolve again
          // inside the same gesture queue, immediately before pressing any button.
          candidate = await prepare()
          if ('retry' in candidate) return candidate
          const current = await frameControls.point(id, frameId, candidate.x, candidate.y)
          if (
            current.sessionId !== point.sessionId ||
            Math.abs(current.x - point.x) > 0.5 ||
            Math.abs(current.y - point.y) > 0.5
          ) {
            point = current
            continue
          }
          beforeDown?.()
          let released = false
          try {
            await wire.request(
              'Input.dispatchMouseEvent',
              {
                type: 'mousePressed',
                x: current.x,
                y: current.y,
                button: 'left',
                buttons: 1,
                clickCount: 1,
              },
              { sessionId: current.sessionId },
            )
            await wire.request(
              'Input.dispatchMouseEvent',
              {
                type: 'mouseReleased',
                x: current.x,
                y: current.y,
                button: 'left',
                buttons: 0,
                clickCount: 1,
              },
              { sessionId: current.sessionId },
            )
            released = true
          } finally {
            if (!released)
              await wire
                .request(
                  'Input.dispatchMouseEvent',
                  {
                    type: 'mouseReleased',
                    x: current.x,
                    y: current.y,
                    button: 'left',
                    buttons: 0,
                    clickCount: 1,
                  },
                  { sessionId: current.sessionId },
                )
                .catch(() => undefined)
          }
          return candidate
        }
        return { retry: 'Element moved during pointer preparation' }
      })
    },
    async moveMouse(id, x, y, frameId) {
      return input(id, async () => {
        await sessionFor(id)
        const point = await frameControls.point(id, frameId, x, y)
        await wire.request(
          'Input.dispatchMouseEvent',
          { type: 'mouseMoved', x: point.x, y: point.y, button: 'none', buttons: 0 },
          { sessionId: point.sessionId },
        )
      })
    },
    async dragMouse(id, from, to, steps, frameId) {
      return input(id, async () => {
        await sessionFor(id)
        const start = await frameControls.point(id, frameId, from.x, from.y)
        const end = await frameControls.point(id, frameId, to.x, to.y)
        if (start.sessionId !== end.sessionId)
          throw new Error('Browser frame changed renderer during drag')
        const mouse = (params: Record<string, UntrustedInput>) =>
          wire.request('Input.dispatchMouseEvent', params, { sessionId: start.sessionId })
        await mouse({ type: 'mouseMoved', x: start.x, y: start.y, button: 'none', buttons: 0 })
        try {
          await mouse({
            type: 'mousePressed',
            x: start.x,
            y: start.y,
            button: 'left',
            buttons: 1,
            clickCount: 1,
          })
          for (let step = 1; step <= steps; step++)
            await mouse({
              type: 'mouseMoved',
              x: start.x + ((end.x - start.x) * step) / steps,
              y: start.y + ((end.y - start.y) * step) / steps,
              button: 'left',
              buttons: 1,
            })
        } finally {
          await mouse({
            type: 'mouseReleased',
            x: end.x,
            y: end.y,
            button: 'left',
            buttons: 0,
            clickCount: 1,
          }).catch(() => undefined)
        }
      })
    },
    async insertText(id, text) {
      await input(id, async () => {
        await request(id, 'Input.insertText', { text })
      })
    },
    async press(id, key) {
      const parts = (
        key === '+'
          ? ['+']
          : key.endsWith('++')
            ? [...key.slice(0, -2).split('+'), '+']
            : key.split('+')
      ).map((part) =>
        part === 'ControlOrMeta' ? (process.platform === 'darwin' ? 'Meta' : 'Control') : part,
      )
      const last = parts.pop() ?? ''
      const modifiers = parts.map((part) => {
        const value = modifierKeys[part]
        if (!value) throw new Error(`Unsupported keyboard modifier: ${part}`)
        return value
      })
      const modifier = modifierKeys[last]
      const special = specialKeys[last]
      const functionNumber = /^F([1-9]|1\d|2[0-4])$/u.exec(last)?.[1]
      const character = [...last].length === 1 ? last : undefined
      let value =
        modifier ??
        special ??
        (functionNumber
          ? {
              key: last,
              code: last,
              virtual: 111 + Number(functionNumber),
            }
          : character
            ? {
                key: character,
                code: /^[a-z]$/iu.test(character)
                  ? `Key${character.toUpperCase()}`
                  : /^\d$/u.test(character)
                    ? `Digit${character}`
                    : '',
                virtual: /^[a-z0-9]$/iu.test(character) ? character.toUpperCase().charCodeAt(0) : 0,
                text: character,
              }
            : undefined)
      if (character && value) {
        const punctuation =
          punctuationKeys[character] ??
          Object.values(punctuationKeys).find((entry) => entry.shifted === character)
        if (punctuation) value = { ...value, code: punctuation.code, virtual: punctuation.virtual }
        if (modifiers.some((entry) => entry.bit === 8)) {
          const shiftedDigit = /^\d$/u.test(character) ? ')!@#$%^&*('[Number(character)] : undefined
          const shifted =
            punctuationKeys[character]?.shifted ?? shiftedDigit ?? character.toUpperCase()
          value = { ...value, key: shifted, text: shifted }
        }
      }
      if (!value) throw new Error(`Unsupported keyboard key: ${last}`)
      const keyValue = value
      return input(id, async () => {
        const session = await sessionFor(id)
        let mask = 0
        const held: typeof modifiers = []
        const dispatch = (
          type: string,
          entry: { key: string; code: string; virtual: number; text?: string },
          text = '',
        ): Promise<Record<string, UntrustedInput>> =>
          wire.request(
            'Input.dispatchKeyEvent',
            {
              type,
              key: entry.key,
              code: entry.code,
              windowsVirtualKeyCode: entry.virtual,
              nativeVirtualKeyCode: entry.virtual,
              modifiers: mask,
              ...(text ? { text, unmodifiedText: text } : {}),
            },
            { sessionId: session.id },
          )
        let keyHeld = false
        try {
          for (const entry of modifiers) {
            mask |= entry.bit
            held.push(entry)
            await dispatch('rawKeyDown', entry)
          }
          if (modifier) mask |= modifier.bit
          const text =
            (mask & ~8) === 0 && 'text' in keyValue && typeof keyValue.text === 'string'
              ? keyValue.text
              : ''
          keyHeld = true
          await dispatch(text ? 'keyDown' : 'rawKeyDown', keyValue, text)
          if (modifier) mask &= ~modifier.bit
          await dispatch('keyUp', keyValue)
          keyHeld = false
        } finally {
          if (keyHeld) {
            if (modifier) mask &= ~modifier.bit
            try {
              await dispatch('keyUp', keyValue)
            } catch {
              /* Release remaining modifiers below. */
            }
          }
          for (const entry of held.reverse()) {
            mask &= ~entry.bit
            await dispatch('keyUp', entry).catch(() => undefined)
          }
        }
      })
    },
    async scroll(id, x, y, deltaX, deltaY) {
      await input(id, async () => {
        await request(id, 'Input.dispatchMouseEvent', { type: 'mouseWheel', x, y, deltaX, deltaY })
      })
    },
    async cookies(contextId) {
      const result = await wire.request(
        'Storage.getCookies',
        contextId ? { browserContextId: contextId } : {},
      )
      return protocolArray(result.cookies, 'cookies response').map((input): BrowserCookie => {
        const cookie = protocolRecord(input, 'CDP cookie')
        return {
          name: protocolString(cookie.name, 'CDP cookie name'),
          value: protocolString(cookie.value, 'CDP cookie value'),
          domain: protocolString(cookie.domain, 'CDP cookie domain'),
          ...(typeof cookie.path === 'string' ? { path: cookie.path } : {}),
          ...(typeof cookie.secure === 'boolean' ? { secure: cookie.secure } : {}),
          ...(typeof cookie.httpOnly === 'boolean' ? { httpOnly: cookie.httpOnly } : {}),
          ...(cookie.sameSite === 'Strict' ||
          cookie.sameSite === 'Lax' ||
          cookie.sameSite === 'None'
            ? { sameSite: cookie.sameSite }
            : {}),
          ...(typeof cookie.expires === 'number' && cookie.expires >= 0
            ? { expires: cookie.expires }
            : {}),
        }
      })
    },
    async setCookies(contextId, cookies) {
      await wire.request('Storage.setCookies', {
        cookies,
        ...(contextId ? { browserContextId: contextId } : {}),
      })
    },
    async closeBrowser() {
      await wire.request('Browser.close')
    },
  }
  return driver
}
