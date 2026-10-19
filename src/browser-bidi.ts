// SPDX-License-Identifier: MIT
import {
  type BrowserCookie,
  type BrowserProtocolCapabilities,
  type BrowserProtocolDriver,
  type BrowserScreenshotOptions,
  type BrowserViewport,
  type BrowserWaitUntil,
  type DriverPage,
} from './browser-protocol-types.js'
import {
  BrowserProtocolError,
  type BrowserWire,
  protocolRecord,
  protocolString,
} from './browser-wire.js'
import { type UntrustedInput } from './untrusted.js'

const keys: Readonly<Record<string, string>> = {
  Backspace: '\uE003',
  Tab: '\uE004',
  Clear: '\uE005',
  Return: '\uE006',
  Enter: '\uE007',
  Shift: '\uE008',
  Control: '\uE009',
  Ctrl: '\uE009',
  Alt: '\uE00A',
  Option: '\uE00A',
  Pause: '\uE00B',
  Escape: '\uE00C',
  Esc: '\uE00C',
  Space: '\uE00D',
  PageUp: '\uE00E',
  PageDown: '\uE00F',
  End: '\uE010',
  Home: '\uE011',
  ArrowLeft: '\uE012',
  Left: '\uE012',
  ArrowUp: '\uE013',
  Up: '\uE013',
  ArrowRight: '\uE014',
  Right: '\uE014',
  ArrowDown: '\uE015',
  Down: '\uE015',
  Insert: '\uE016',
  Delete: '\uE017',
  Meta: '\uE03D',
  Command: '\uE03D',
  Cmd: '\uE03D',
  F1: '\uE031',
  F2: '\uE032',
  F3: '\uE033',
  F4: '\uE034',
  F5: '\uE035',
  F6: '\uE036',
  F7: '\uE037',
  F8: '\uE038',
  F9: '\uE039',
  F10: '\uE03A',
  F11: '\uE03B',
  F12: '\uE03C',
}

function array(value: UntrustedInput, label: string): UntrustedInput[] {
  if (!Array.isArray(value)) throw new Error(`Invalid BiDi ${label}: expected an array`)
  return value as UntrustedInput[]
}

/** Decode BiDi values without assigning untrusted property names to prototypes. */
function remoteValue(
  value: UntrustedInput,
  references = new Map<string, UntrustedInput>(),
  depth = 0,
): UntrustedInput {
  if (depth > 100) throw new Error('BiDi evaluation result exceeds maximum depth')
  const remote = protocolRecord(value, 'BiDi remote value')
  const type = protocolString(remote.type, 'BiDi remote value type')
  const id = typeof remote.internalId === 'string' ? remote.internalId : undefined
  if (id !== undefined && references.has(id)) return references.get(id)
  switch (type) {
    case 'undefined':
      return undefined
    case 'null':
      return null
    case 'boolean':
      if (typeof remote.value !== 'boolean') throw new Error('Invalid BiDi boolean value')
      return remote.value
    case 'string':
      return protocolString(remote.value, 'BiDi string value')
    case 'number': {
      if (typeof remote.value === 'number') return remote.value
      if (typeof remote.value !== 'string') throw new Error('Invalid BiDi number value')
      switch (remote.value) {
        case 'NaN':
          return NaN
        case '-0':
          return -0
        case 'Infinity':
          return Infinity
        case '-Infinity':
          return -Infinity
        default:
          throw new Error('Invalid BiDi number value')
      }
    }
    case 'bigint':
      return BigInt(protocolString(remote.value, 'BiDi bigint value'))
    case 'date':
      return new Date(protocolString(remote.value, 'BiDi date value'))
    case 'regexp': {
      const expression = protocolRecord(remote.value, 'BiDi regexp value')
      return new RegExp(
        protocolString(expression.pattern, 'BiDi regexp pattern'),
        protocolString(expression.flags, 'BiDi regexp flags'),
      )
    }
    case 'array': {
      const result: UntrustedInput[] = []
      if (id !== undefined) references.set(id, result)
      for (const entry of array(remote.value, 'array contents'))
        result.push(remoteValue(entry, references, depth + 1))
      return result
    }
    case 'object': {
      const result: Record<string, UntrustedInput> = {}
      if (id !== undefined) references.set(id, result)
      for (const entry of array(remote.value, 'object contents')) {
        const pair = array(entry, 'object entry')
        const key =
          typeof pair[0] === 'string' ? pair[0] : remoteValue(pair[0], references, depth + 1)
        if (typeof key !== 'string' || pair.length !== 2)
          throw new Error('Invalid BiDi object entry')
        Object.defineProperty(result, key, {
          value: remoteValue(pair[1], references, depth + 1),
          enumerable: true,
          configurable: true,
          writable: true,
        })
      }
      return result
    }
    case 'map': {
      const result = new Map<UntrustedInput, UntrustedInput>()
      if (id !== undefined) references.set(id, result)
      for (const entry of array(remote.value, 'map contents')) {
        const pair = array(entry, 'map entry')
        if (pair.length !== 2) throw new Error('Invalid BiDi map entry')
        result.set(
          typeof pair[0] === 'string' ? pair[0] : remoteValue(pair[0], references, depth + 1),
          remoteValue(pair[1], references, depth + 1),
        )
      }
      return result
    }
    case 'set': {
      const result = new Set<UntrustedInput>()
      if (id !== undefined) references.set(id, result)
      for (const entry of array(remote.value, 'set contents'))
        result.add(remoteValue(entry, references, depth + 1))
      return result
    }
    default:
      throw new Error(
        `BiDi evaluation cannot return ${type}; return serializable page data instead`,
      )
  }
}

function unsupported(error: UntrustedInput): boolean {
  return (
    error instanceof BrowserProtocolError &&
    (error.code === 'unknown command' || error.code === 'unsupported operation')
  )
}

class BiDiDriver implements BrowserProtocolDriver {
  readonly backend = 'bidi'
  readonly capabilities: Omit<BrowserProtocolCapabilities, 'isolatedContexts'> & {
    isolatedContexts: boolean
  } = {
    backend: 'bidi',
    isolatedContexts: true,
    viewport: true,
    screenshots: true,
    fullPageScreenshot: true,
    cookies: true,
    nativeInput: true,
    rawProtocol: true,
    nativeCapture: false,
    nativeViewer: false,
  }
  version = 'WebDriver BiDi'
  private readonly pageCache = new Map<string, DriverPage>()
  private readonly dispose: (() => void)[] = []
  private platform = ''
  private ownsSession = false
  private disposingSession: Promise<void> | undefined

  constructor(readonly wire: BrowserWire) {}

  async initialize(): Promise<void> {
    try {
      const session = await this.wire.request('session.new', { capabilities: {} })
      this.ownsSession = true
      const caps = protocolRecord(session.capabilities, 'BiDi session capabilities')
      this.version =
        [caps.browserName, caps.browserVersion]
          .filter((value): value is string => typeof value === 'string')
          .join(' ') || this.version
      this.platform = typeof caps.platformName === 'string' ? caps.platformName : ''
    } catch (error) {
      if (
        error instanceof BrowserProtocolError &&
        error.code === 'session not created' &&
        /already (?:exists|started)|maximum number of (?:active )?sessions/i.test(error.message)
      ) {
        throw new Error(
          'This BiDi browser already has an active automation session. Close its owning SDK connection cleanly before reconnecting; disconnecting a WebSocket alone does not end a Firefox session.',
          { cause: error },
        )
      }
      throw error
    }
    this.dispose.push(
      this.wire.on('browsingContext.contextCreated', (params) => {
        if (params.parent === null || params.parent === undefined) this.rememberPage(params)
      }),
    )
    this.dispose.push(
      this.wire.on('browsingContext.contextDestroyed', (params) => {
        if (typeof params.context === 'string') this.pageCache.delete(params.context)
      }),
    )
    this.dispose.push(
      this.wire.on('browsingContext.navigationStarted', (params) => {
        if (typeof params.context !== 'string' || typeof params.url !== 'string') return
        const page = this.pageCache.get(params.context)
        if (page) page.url = params.url
      }),
    )
    const cleanup = (): void => {
      for (const stop of this.dispose.splice(0)) stop()
      this.pageCache.clear()
    }
    void this.wire.closed.then(cleanup, cleanup)
    await this.wire.request('session.subscribe', {
      events: [
        'browsingContext.contextCreated',
        'browsingContext.contextDestroyed',
        'browsingContext.navigationStarted',
      ],
    })
    const tree = await this.wire.request('browsingContext.getTree', { maxDepth: 0 })
    for (const value of array(tree.contexts, 'browsing contexts'))
      this.rememberPage(protocolRecord(value, 'BiDi browsing context'))
    try {
      await this.contexts()
    } catch (error) {
      if (!(error instanceof Error) || !unsupported(error)) throw error
      this.capabilities.isolatedContexts = false
    }
  }

  private rememberPage(value: Record<string, UntrustedInput>): DriverPage {
    const page = {
      id: protocolString(value.context, 'BiDi browsing context id'),
      contextId: typeof value.userContext === 'string' ? value.userContext : 'default',
      url: protocolString(value.url, 'BiDi browsing context URL'),
    }
    this.pageCache.set(page.id, page)
    return page
  }

  async contexts(): Promise<string[]> {
    if (!this.capabilities.isolatedContexts) return ['default']
    const result = await this.wire.request('browser.getUserContexts')
    return array(result.userContexts, 'user contexts').map((value) =>
      protocolString(
        protocolRecord(value, 'BiDi user context').userContext,
        'BiDi user context id',
      ),
    )
  }

  pages(): Promise<DriverPage[]> {
    return Promise.resolve([...this.pageCache.values()].map((page) => ({ ...page })))
  }

  async newContext(): Promise<string> {
    if (!this.capabilities.isolatedContexts)
      throw new Error('This BiDi browser does not support isolated user contexts')
    const result = await this.wire.request('browser.createUserContext')
    return protocolString(result.userContext, 'BiDi new user context id')
  }

  async closeContext(id: string): Promise<void> {
    if (id === 'default') throw new Error('The default BiDi user context cannot be removed')
    await this.wire.request('browser.removeUserContext', { userContext: id })
    for (const [key, page] of this.pageCache) if (page.contextId === id) this.pageCache.delete(key)
  }

  async newPage(contextId = 'default'): Promise<DriverPage> {
    const result = await this.wire.request('browsingContext.create', {
      type: 'tab',
      userContext: contextId,
      background: false,
    })
    const id = protocolString(result.context, 'BiDi new browsing context id')
    const page = this.pageCache.get(id) ?? { id, contextId, url: 'about:blank' }
    this.pageCache.set(id, page)
    return { ...page }
  }

  async closePage(id: string): Promise<void> {
    await this.wire.request('browsingContext.close', { context: id })
    this.pageCache.delete(id)
  }

  async navigate(id: string, url: string, wait: BrowserWaitUntil): Promise<void> {
    const result = await this.wire.request('browsingContext.navigate', { context: id, url, wait })
    const page = this.pageCache.get(id)
    if (page) page.url = typeof result.url === 'string' ? result.url : url
  }

  async evaluate(id: string, expression: string): Promise<UntrustedInput> {
    const result = await this.wire.request('script.evaluate', {
      expression,
      target: { context: id },
      awaitPromise: true,
      resultOwnership: 'none',
      serializationOptions: { maxObjectDepth: 100, maxDomDepth: 0 },
    })
    if (result.type === 'exception') {
      const details = protocolRecord(result.exceptionDetails, 'BiDi script exception')
      throw new Error(
        `Browser evaluation failed: ${protocolString(details.text, 'BiDi exception text')}`,
      )
    }
    if (result.type !== 'success') throw new Error('Invalid BiDi evaluation result')
    return remoteValue(result.result)
  }

  async screenshot(id: string, options: BrowserScreenshotOptions): Promise<Buffer> {
    const result = await this.wire.request('browsingContext.captureScreenshot', {
      context: id,
      origin: options.fullPage ? 'document' : 'viewport',
      format: {
        type: options.type === 'jpeg' ? 'image/jpeg' : 'image/png',
        ...(options.quality === undefined ? {} : { quality: options.quality / 100 }),
      },
    })
    return Buffer.from(protocolString(result.data, 'BiDi screenshot data'), 'base64')
  }

  async setViewport(id: string, viewport: BrowserViewport): Promise<void> {
    await this.wire.request('browsingContext.setViewport', { context: id, viewport })
  }

  private async actions(id: string, actions: Record<string, UntrustedInput>[]): Promise<void> {
    try {
      await this.wire.request('input.performActions', { context: id, actions })
    } catch (error) {
      // A partially executed action list can leave a key or mouse button held down.
      await this.wire.request('input.releaseActions', { context: id }).catch(() => undefined)
      throw error
    }
  }

  async click(
    id: string,
    x: number,
    y: number,
    button: 'left' | 'right' | 'middle',
    count: number,
  ): Promise<void> {
    const index = button === 'left' ? 0 : button === 'middle' ? 1 : 2
    const actions: Record<string, UntrustedInput>[] = [
      { type: 'pointerMove', x: Math.round(x), y: Math.round(y), duration: 0, origin: 'viewport' },
    ]
    for (let i = 0; i < count; i++)
      actions.push({ type: 'pointerDown', button: index }, { type: 'pointerUp', button: index })
    await this.actions(id, [
      { type: 'pointer', id: 'suitecut-mouse', parameters: { pointerType: 'mouse' }, actions },
    ])
  }

  async insertText(id: string, text: string): Promise<void> {
    let actions: Record<string, UntrustedInput>[] = []
    for (const point of text) {
      const value =
        point === '\n'
          ? keys.Enter
          : point === '\r'
            ? keys.Return
            : point === '\t'
              ? keys.Tab
              : point
      actions.push({ type: 'keyDown', value }, { type: 'keyUp', value })
      if (actions.length >= 8192) {
        await this.actions(id, [{ type: 'key', id: 'suitecut-keyboard', actions }])
        actions = []
      }
    }
    if (actions.length > 0)
      await this.actions(id, [{ type: 'key', id: 'suitecut-keyboard', actions }])
  }

  async press(id: string, key: string): Promise<void> {
    const parts = key.endsWith('+')
      ? [...key.slice(0, -1).split('+').filter(Boolean), '+']
      : key.split('+')
    const values = parts.map((part) => {
      if (part === 'ControlOrMeta') return /mac|darwin/i.test(this.platform) ? '\uE03D' : '\uE009'
      const mapped = keys[part]
      if (mapped !== undefined) return mapped
      if ([...part].length !== 1) throw new Error(`Unsupported browser key: ${part}`)
      return part
    })
    const actions = [
      ...values.map((value) => ({ type: 'keyDown', value })),
      ...values
        .slice()
        .reverse()
        .map((value) => ({ type: 'keyUp', value })),
    ]
    await this.actions(id, [{ type: 'key', id: 'suitecut-keyboard', actions }])
  }

  async scroll(id: string, x: number, y: number, deltaX: number, deltaY: number): Promise<void> {
    await this.actions(id, [
      {
        type: 'wheel',
        id: 'suitecut-wheel',
        actions: [
          {
            type: 'scroll',
            origin: 'viewport',
            x: Math.round(x),
            y: Math.round(y),
            deltaX: Math.round(deltaX),
            deltaY: Math.round(deltaY),
            duration: 0,
          },
        ],
      },
    ])
  }

  async cookies(contextId: string): Promise<BrowserCookie[]> {
    const result = await this.wire.request('storage.getCookies', {
      partition: { type: 'storageKey', userContext: contextId },
    })
    return array(result.cookies, 'cookies').map((value) => {
      const cookie = protocolRecord(value, 'BiDi cookie')
      const bytes = protocolRecord(cookie.value, 'BiDi cookie value')
      const type = protocolString(bytes.type, 'BiDi cookie value type')
      const raw = protocolString(bytes.value, 'BiDi cookie bytes')
      if (type !== 'string' && type !== 'base64')
        throw new Error('Invalid BiDi cookie byte encoding')
      const sameSite =
        typeof cookie.sameSite === 'string' ? cookie.sameSite.toLowerCase() : undefined
      return {
        name: protocolString(cookie.name, 'BiDi cookie name'),
        value: type === 'base64' ? Buffer.from(raw, 'base64').toString('utf8') : raw,
        domain: protocolString(cookie.domain, 'BiDi cookie domain'),
        path: protocolString(cookie.path, 'BiDi cookie path'),
        secure: cookie.secure === true,
        httpOnly: cookie.httpOnly === true,
        ...(sameSite === 'strict'
          ? { sameSite: 'Strict' as const }
          : sameSite === 'lax'
            ? { sameSite: 'Lax' as const }
            : sameSite === 'none'
              ? { sameSite: 'None' as const }
              : {}),
        ...(typeof cookie.expiry === 'number' ? { expires: cookie.expiry } : {}),
      }
    })
  }

  async setCookies(contextId: string, cookies: BrowserCookie[]): Promise<void> {
    for (const cookie of cookies) {
      await this.wire.request('storage.setCookie', {
        partition: { type: 'storageKey', userContext: contextId },
        cookie: {
          name: cookie.name,
          value: { type: 'string', value: cookie.value },
          domain: cookie.domain,
          path: cookie.path ?? '/',
          secure: cookie.secure ?? false,
          httpOnly: cookie.httpOnly ?? false,
          sameSite: (cookie.sameSite ?? 'Lax').toLowerCase(),
          ...(cookie.expires === undefined || cookie.expires === -1
            ? {}
            : { expiry: cookie.expires }),
        },
      })
    }
  }

  disposeSession(): Promise<void> {
    if (this.disposingSession) return this.disposingSession
    if (!this.ownsSession) return Promise.resolve()
    this.disposingSession = this.wire.request('session.end').then(
      () => {
        this.ownsSession = false
      },
      (error: UntrustedInput) => {
        this.disposingSession = undefined
        throw error
      },
    )
    return this.disposingSession
  }

  async closeBrowser(): Promise<void> {
    await this.wire.request('browser.close')
  }
}

/** Create a Firefox-compatible W3C WebDriver BiDi adapter over one connection. */
export function createBiDiDriver(wire: BrowserWire): BrowserProtocolDriver {
  return new BiDiDriver(wire)
}
