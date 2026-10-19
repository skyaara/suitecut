// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { createCDPDriver } from '../src/browser-cdp.js'
import { type BrowserWire, protocolString } from '../src/browser-wire.js'
import { type UntrustedInput } from '../src/untrusted.js'

interface Call {
  method: string
  params: Record<string, UntrustedInput>
  sessionId?: string
}
type Handler = (
  call: Call,
) => Promise<Record<string, UntrustedInput>> | Record<string, UntrustedInput> | undefined

function fakeWire(timeoutMs = 30_000) {
  const calls: Call[] = []
  const listeners = new Map<
    string,
    Set<(params: Record<string, UntrustedInput>, sessionId?: string) => void>
  >()
  let resolveClosed: (() => void) | undefined
  let connected = true
  let handler: Handler | undefined
  const closed = new Promise<void>((resolve) => {
    resolveClosed = resolve
  })
  const wire = {
    timeoutMs,
    closed,
    isConnected: () => connected,
    on(method, listener) {
      const set = listeners.get(method) ?? new Set()
      set.add(listener)
      listeners.set(method, set)
      return () => {
        set.delete(listener)
      }
    },
    async close() {
      connected = false
      resolveClosed?.()
      await closed
    },
    request(method, params = {}, options = {}) {
      const call = {
        method,
        params,
        ...(options.sessionId ? { sessionId: options.sessionId } : {}),
      }
      calls.push(call)
      const custom = handler?.(call)
      if (custom) return Promise.resolve(custom)
      const responses: Record<string, Record<string, UntrustedInput>> = {
        'Browser.getVersion': { product: 'Chrome/150.0' },
        'Target.getTargets': {
          targetInfos: [{ targetId: 'p1', type: 'page', url: 'about:blank' }],
        },
        'Target.getBrowserContexts': { browserContextIds: [] },
        'Target.attachToTarget': { sessionId: 's1' },
        'Target.createBrowserContext': { browserContextId: 'ctx1' },
        'Target.createTarget': { targetId: 'p2' },
        'Runtime.evaluate': { result: { value: 42 } },
        'Page.captureScreenshot': { data: Buffer.from('png').toString('base64') },
        'Page.getLayoutMetrics': { cssContentSize: { width: 1234, height: 4321 } },
        'Storage.getCookies': { cookies: [] },
      }
      return Promise.resolve(responses[method] ?? {})
    },
  } satisfies Pick<BrowserWire, 'timeoutMs' | 'closed' | 'isConnected' | 'on' | 'close' | 'request'>
  return {
    wire: wire as BrowserWire,
    calls,
    listeners,
    handle(value: Handler) {
      handler = value
    },
    emit(method: string, params: Record<string, UntrustedInput>, sessionId?: string) {
      for (const listener of listeners.get(method) ?? []) listener(params, sessionId)
    },
  }
}

describe('CDP browser adapter', () => {
  it('caches one flattened session and discovers popups without per-action enumeration', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await Promise.all([driver.initialize(), driver.initialize()])
    expect(driver.version).toBe('Chrome/150.0')
    expect(
      await Promise.all(Array.from({ length: 20 }, () => driver.evaluate('p1', '21 * 2'))),
    ).toEqual(Array(20).fill(42))
    expect(mock.calls.filter((call) => call.method === 'Target.attachToTarget')).toHaveLength(1)
    expect(mock.calls.filter((call) => call.method === 'Page.enable')).toHaveLength(1)
    expect(mock.calls.filter((call) => call.method === 'Target.getTargets')).toHaveLength(1)
    mock.emit('Target.targetCreated', {
      targetInfo: {
        targetId: 'popup',
        type: 'page',
        url: 'https://popup.test',
        browserContextId: 'ctx',
      },
    })
    expect(await driver.pages()).toContainEqual({
      id: 'popup',
      contextId: 'ctx',
      url: 'https://popup.test',
    })
    mock.emit('Target.targetDestroyed', { targetId: 'p1' })
    await expect(driver.evaluate('p1', '1')).rejects.toThrow('closed')
    await mock.wire.close()
    expect([...mock.listeners.values()].every((set) => set.size === 0)).toBe(true)
  })

  it('retries failed attachments and releases a session after initialization fails', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    let failed = false
    mock.handle(({ method }) => {
      if (method === 'Page.enable' && !failed) {
        failed = true
        throw new Error('enable failed')
      }
      return method === 'Runtime.evaluate' ? { result: { value: 7 } } : undefined
    })
    await expect(driver.evaluate('p1', '7')).rejects.toThrow('enable failed')
    expect(mock.calls.some((call) => call.method === 'Target.detachFromTarget')).toBe(true)
    expect(await driver.evaluate('p1', '7')).toBe(7)
    expect(mock.calls.filter((call) => call.method === 'Target.attachToTarget')).toHaveLength(2)
    await mock.wire.close()
  })

  it('waits for the correct navigation loader even when lifecycle precedes its response', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    mock.handle(({ method }) => {
      if (method === 'Page.navigate') {
        mock.emit('Page.lifecycleEvent', { name: 'load', loaderId: 'new' }, 's1')
        return { loaderId: 'new', frameId: 'main' }
      }
      return undefined
    })
    await driver.navigate('p1', 'https://test', 'complete')
    expect(mock.listeners.get('Page.lifecycleEvent')?.size).toBe(0)
    await mock.wire.close()
  })

  it('ignores old and other-session lifecycle events, and rejects detached navigation', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    let navigateStarted: (() => void) | undefined
    const started = new Promise<void>((resolve) => {
      navigateStarted = resolve
    })
    mock.handle(({ method }) => {
      if (method === 'Page.navigate') {
        navigateStarted?.()
        return { loaderId: 'new' }
      }
      return undefined
    })
    const pending = driver.navigate('p1', 'https://test', 'complete')
    await started
    mock.emit('Page.lifecycleEvent', { name: 'load', loaderId: 'old' }, 's1')
    mock.emit('Page.lifecycleEvent', { name: 'load', loaderId: 'new' }, 'other-session')
    mock.emit('Target.detachedFromTarget', { sessionId: 's1' })
    await expect(pending).rejects.toThrow('detached')
    expect(mock.listeners.get('Page.lifecycleEvent')?.size).toBe(0)
    await mock.wire.close()
  })

  it('preserves by-value results and surfaces page exceptions', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    mock.handle(({ method, params }) =>
      method === 'Runtime.evaluate'
        ? params.expression === 'error'
          ? { exceptionDetails: { text: 'Uncaught', exception: { description: 'Error: broken' } } }
          : { result: { unserializableValue: '-123n' } }
        : undefined,
    )
    expect(await driver.evaluate('p1', '-123n')).toBe(-123n)
    await expect(driver.evaluate('p1', 'error')).rejects.toThrow('Error: broken')
    expect(mock.calls.find((call) => call.method === 'Runtime.evaluate')?.params).toMatchObject({
      awaitPromise: true,
      returnByValue: true,
      userGesture: true,
    })
    await mock.wire.close()
  })

  it('dispatches genuine pointer/key events, Unicode text and modifier release', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    await driver.click('p1', 25, 50, 'left', 2)
    expect(
      mock.calls
        .filter((call) => call.method === 'Input.dispatchMouseEvent')
        .map((call) => call.params.type),
    ).toEqual(['mouseMoved', 'mousePressed', 'mouseReleased', 'mousePressed', 'mouseReleased'])
    await driver.press('p1', 'Control+a')
    const events = mock.calls
      .filter((call) => call.method === 'Input.dispatchKeyEvent')
      .map((call) => call.params)
    expect(events.map((event) => [event.type, event.key, event.modifiers])).toEqual([
      ['rawKeyDown', 'Control', 2],
      ['rawKeyDown', 'a', 2],
      ['keyUp', 'a', 2],
      ['keyUp', 'Control', 0],
    ])
    await driver.insertText('p1', 'नमस्ते 🌏')
    expect(mock.calls.at(-1)?.params).toEqual({ text: 'नमस्ते 🌏' })
    await driver.press('p1', 'Enter')
    expect(mock.calls.at(-2)?.params).toMatchObject({ type: 'keyDown', text: '\r' })
    await mock.wire.close()
  })

  it('keeps activation and whole gestures together across pages without blocking evaluation', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    for (const id of ['p2', 'p3'])
      mock.emit('Target.targetCreated', {
        targetInfo: { targetId: id, type: 'page', url: 'about:blank' },
      })
    let started!: () => void
    let release!: () => void
    const pressed = new Promise<void>((resolve) => {
      started = resolve
    })
    const gate = new Promise<void>((resolve) => {
      release = resolve
    })
    mock.handle(({ method, params }) => {
      if (method === 'Target.attachToTarget')
        return { sessionId: `session-${protocolString(params.targetId, 'target id')}` }
      if (method === 'Input.dispatchMouseEvent' && params.type === 'mousePressed') {
        started()
        return gate.then(() => ({}))
      }
      return undefined
    })
    const clicking = driver.click('p1', 10, 20, 'left', 1)
    await pressed
    const typing = driver.insertText('p2', 'parallel')
    const screenshot = driver.screenshot('p3', {})
    expect(await driver.evaluate('p2', '42')).toBe(42)
    expect(mock.calls.filter((call) => call.method === 'Page.bringToFront')).toHaveLength(1)
    release()
    await Promise.all([clicking, typing, screenshot])
    expect(
      mock.calls
        .filter(
          (call) =>
            call.method === 'Page.bringToFront' ||
            call.method.startsWith('Input.') ||
            call.method === 'Page.captureScreenshot',
        )
        .map((call) => [call.sessionId, call.method, call.params.type]),
    ).toEqual([
      ['session-p1', 'Page.bringToFront', undefined],
      ['session-p1', 'Input.dispatchMouseEvent', 'mouseMoved'],
      ['session-p1', 'Input.dispatchMouseEvent', 'mousePressed'],
      ['session-p1', 'Input.dispatchMouseEvent', 'mouseReleased'],
      ['session-p2', 'Page.bringToFront', undefined],
      ['session-p2', 'Input.insertText', undefined],
      ['session-p3', 'Page.bringToFront', undefined],
      ['session-p3', 'Page.captureScreenshot', undefined],
    ])
    await mock.wire.close()
  })

  it('resolves locator geometry after activation and again after hover before pressing', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    let activated = false
    let moves = 0
    mock.handle(({ method, params }) => {
      if (method === 'Page.bringToFront') activated = true
      if (method === 'Input.dispatchMouseEvent' && params.type === 'mouseMoved') moves++
      return undefined
    })
    const result = await driver.clickLocator?.('p1', () => {
      expect(activated).toBe(true)
      return Promise.resolve(moves === 0 ? { x: 10, y: 20 } : { x: 80, y: 90 })
    })
    expect(result).toEqual({ x: 80, y: 90 })
    const mouse = mock.calls.filter((call) => call.method === 'Input.dispatchMouseEvent')
    expect(mouse.map((call) => [call.params.type, call.params.x, call.params.y])).toEqual([
      ['mouseMoved', 10, 20],
      ['mouseMoved', 80, 90],
      ['mousePressed', 80, 90],
      ['mouseReleased', 80, 90],
    ])
    await mock.wire.close()
  })

  it('abandons a covered locator before mouse down and releases input to another page', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    let preparations = 0
    const result = await driver.clickLocator?.('p1', () =>
      Promise.resolve(++preparations === 1 ? { x: 10, y: 20 } : { retry: 'Element is covered' }),
    )
    expect(result).toEqual({ retry: 'Element is covered' })
    expect(mock.calls.some((call) => call.params.type === 'mousePressed')).toBe(false)
    await driver.insertText('p1', 'still usable')
    expect(mock.calls.at(-1)?.method).toBe('Input.insertText')
    await mock.wire.close()
  })

  it('bounds geometry preparation without retrying any completed click', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    let preparations = 0
    const result = await driver.clickLocator?.('p1', () =>
      Promise.resolve({ x: ++preparations * 20, y: 10 }),
    )
    expect(result).toEqual({ retry: 'Element moved during pointer preparation' })
    expect(preparations).toBe(5)
    expect(mock.calls.filter((call) => call.params.type === 'mouseMoved')).toHaveLength(4)
    expect(mock.calls.some((call) => call.params.type === 'mousePressed')).toBe(false)
    await mock.wire.close()
  })

  it('releases a failed locator press once without attempting another click', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    mock.handle(({ method, params }) =>
      method === 'Input.dispatchMouseEvent' && params.type === 'mousePressed'
        ? Promise.reject(new Error('press failed'))
        : undefined,
    )
    await expect(
      driver.clickLocator?.('p1', () => Promise.resolve({ x: 10, y: 20 })),
    ).rejects.toThrow('press failed')
    expect(mock.calls.filter((call) => call.params.type === 'mousePressed')).toHaveLength(1)
    expect(mock.calls.filter((call) => call.params.type === 'mouseReleased')).toHaveLength(1)
    await mock.wire.close()
  })

  it('checks cancellation immediately before down after all pointer preparation', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    await expect(
      driver.clickLocator?.(
        'p1',
        () => Promise.resolve({ x: 10, y: 20 }),
        undefined,
        () => {
          throw new Error('page cancelled')
        },
      ),
    ).rejects.toThrow('page cancelled')
    expect(mock.calls.filter((call) => call.params.type === 'mouseMoved')).toHaveLength(1)
    expect(mock.calls.some((call) => call.params.type === 'mousePressed')).toBe(false)
    expect(mock.calls.some((call) => call.params.type === 'mouseReleased')).toBe(false)
    await mock.wire.close()
  })

  it('releases failed gestures before letting another page acquire focus', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    mock.emit('Target.targetCreated', {
      targetInfo: { targetId: 'p2', type: 'page', url: 'about:blank' },
    })
    mock.handle(({ method, params }) =>
      method === 'Input.dispatchMouseEvent' && params.type === 'mousePressed'
        ? Promise.reject(new Error('pointer failed'))
        : undefined,
    )
    const clicking = driver.click('p1', 10, 20, 'left', 1)
    const typing = driver.insertText('p2', 'still works')
    await expect(clicking).rejects.toThrow('pointer failed')
    await typing
    const gestures = mock.calls.filter((call) => call.method.startsWith('Input.'))
    expect(gestures.map((call) => call.params.type ?? call.method)).toEqual([
      'mouseMoved',
      'mousePressed',
      'mouseReleased',
      'Input.insertText',
    ])
    await mock.wire.close()
  })

  it('bounds pending native input and recovers its budget when work settles', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    let started!: () => void
    let release!: () => void
    const entered = new Promise<void>((resolve) => {
      started = resolve
    })
    const gate = new Promise<void>((resolve) => {
      release = resolve
    })
    let first = true
    mock.handle(({ method }) => {
      if (method === 'Page.bringToFront' && first) {
        first = false
        started()
        return gate.then(() => ({}))
      }
      return undefined
    })
    const active = driver.insertText('p1', 'first')
    await entered
    const queued = Array.from({ length: 255 }, () => driver.insertText('p1', 'queued'))
    await expect(driver.insertText('p1', 'overflow')).rejects.toThrow('input queue limit')
    release()
    await Promise.all([active, ...queued])
    await driver.insertText('p1', 'budget recovered')
    await mock.wire.close()
  })

  it('supports isolated contexts, full page screenshots, resizing and context cookies', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    const context = await driver.newContext()
    const page = await driver.newPage(context)
    expect(page.contextId).toBe(context)
    await driver.setViewport(page.id, { width: 1280, height: 720 })
    expect(await driver.screenshot(page.id, { fullPage: true })).toEqual(Buffer.from('png'))
    expect(
      mock.calls.find((call) => call.method === 'Page.captureScreenshot')?.params.clip,
    ).toEqual({ x: 0, y: 0, width: 1234, height: 4321, scale: 1 })
    await driver.setCookies(context, [{ name: 'a', value: 'b', domain: 'test.local' }])
    expect(mock.calls.at(-1)?.params.browserContextId).toBe(context)
    expect(await driver.cookies(context)).toEqual([])
    await driver.closeContext(context)
    expect(await driver.pages()).not.toContainEqual(page)
    await mock.wire.close()
  })

  it('bounds load waits and clears event ownership on timeout and navigation failure', async () => {
    const mock = fakeWire(5)
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    mock.handle(({ method }) =>
      method === 'Page.navigate' ? { loaderId: 'never-finishes' } : undefined,
    )
    await expect(driver.navigate('p1', 'https://test', 'interactive')).rejects.toThrow(
      'timed out after 5ms',
    )
    expect(mock.listeners.get('Page.lifecycleEvent')?.size).toBe(0)
    mock.handle(({ method }) =>
      method === 'Page.navigate' ? { errorText: 'net::ERR_NAME_NOT_RESOLVED' } : undefined,
    )
    await expect(driver.navigate('p1', 'https://test', 'complete')).rejects.toThrow(
      'ERR_NAME_NOT_RESOLVED',
    )
    expect(mock.listeners.get('Page.lifecycleEvent')?.size).toBe(0)
    await mock.wire.close()
  })

  it('releases modifiers after a failed key dispatch and supports literal plus', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    mock.handle(({ method, params }) => {
      if (method === 'Input.dispatchKeyEvent' && params.key === 'a') throw new Error('key failed')
      return undefined
    })
    await expect(driver.press('p1', 'Control+a')).rejects.toThrow('key failed')
    expect(mock.calls.at(-1)?.params).toMatchObject({ type: 'keyUp', key: 'Control', modifiers: 0 })
    await driver.press('p1', '+')
    expect(mock.calls.at(-2)?.params).toMatchObject({ type: 'keyDown', key: '+', text: '+' })
    await driver.press('p1', 'Shift+a')
    expect(mock.calls.at(-3)?.params).toMatchObject({
      type: 'keyDown',
      key: 'A',
      text: 'A',
      modifiers: 8,
    })
    await driver.press('p1', 'Shift')
    expect(mock.calls.at(-2)?.params).toMatchObject({
      type: 'rawKeyDown',
      key: 'Shift',
      modifiers: 8,
    })
    expect(mock.calls.at(-1)?.params).toMatchObject({ type: 'keyUp', key: 'Shift', modifiers: 0 })
    await mock.wire.close()
  })

  it('does not resurrect a page destroyed while its creation response is pending', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    mock.handle(({ method }) => {
      if (method !== 'Target.createTarget') return undefined
      mock.emit('Target.targetDestroyed', { targetId: 'p2' })
      return { targetId: 'p2' }
    })
    await expect(driver.newPage()).rejects.toThrow('closed during creation')
    expect((await driver.pages()).some((page) => page.id === 'p2')).toBe(false)
    await mock.wire.close()
  })

  it('refreshes explicitly listed contexts and prunes externally disposed context state', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    let sequence = 0
    mock.handle(({ method }) => {
      if (method === 'Target.createBrowserContext') return { browserContextId: `ctx${++sequence}` }
      return undefined
    })
    for (let iteration = 0; iteration < 100; iteration++) {
      const contextId = await driver.newContext()
      await driver.newPage(contextId)
      await driver.evaluate('p2', '42')
      // An external client closes the context. CDP has no corresponding context event.
      expect(await driver.contexts()).toEqual([''])
      expect((await driver.pages()).filter((page) => page.contextId !== '')).toHaveLength(0)
      await expect(driver.evaluate('p2', '42')).rejects.toThrow('closed')
    }
    expect(
      mock.calls
        .filter((call) => call.method === 'Target.createBrowserContext')
        .every((call) => call.params.disposeOnDetach !== true),
    ).toBe(true)
    await mock.wire.close()
  })

  it('preserves a context created while an explicit context snapshot is in flight', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    let resolveSnapshot: ((value: Record<string, UntrustedInput>) => void) | undefined
    mock.handle(({ method }) =>
      method === 'Target.getBrowserContexts'
        ? new Promise<Record<string, UntrustedInput>>((resolve) => {
            resolveSnapshot = resolve
          })
        : undefined,
    )
    const pending = driver.contexts()
    const newId = await driver.newContext()
    resolveSnapshot?.({ browserContextIds: [] })
    expect(await pending).toEqual(['', newId])
    await mock.wire.close()
  })

  it('serves URL/title from target events and activates before background screenshots', async () => {
    const mock = fakeWire()
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    mock.emit('Target.targetInfoChanged', {
      targetInfo: {
        targetId: 'p1',
        type: 'page',
        url: 'https://current.test/path',
        title: 'Current title',
      },
    })
    for (let iteration = 0; iteration < 100; iteration++)
      expect(driver.pageInfo?.('p1')).toMatchObject({
        url: 'https://current.test/path',
        title: 'Current title',
      })
    expect(mock.calls.some((call) => call.method === 'Runtime.evaluate')).toBe(false)
    mock.handle(({ method }) =>
      method === 'Target.getTargetInfo'
        ? { targetInfo: { title: 'Fresh browser title' } }
        : undefined,
    )
    expect(await driver.pageTitle?.('p1')).toBe('Fresh browser title')
    expect(driver.pageInfo?.('p1')?.title).toBe('Fresh browser title')
    await driver.screenshot('p1', {})
    const methods = mock.calls.map((call) => call.method)
    expect(methods.indexOf('Page.bringToFront')).toBeLessThan(
      methods.indexOf('Page.captureScreenshot'),
    )
    await mock.wire.close()
  })

  it('routes same-origin frame worlds, resolves iframe nodes and releases remote handles', async () => {
    const mock = fakeWire(5)
    mock.handle(({ method, params }) => {
      if (method === 'Page.getFrameTree')
        return {
          frameTree: {
            frame: { id: 'main', url: 'https://site.test' },
            childFrames: [
              {
                frame: {
                  id: 'child',
                  parentId: 'main',
                  url: 'https://site.test/child',
                  name: 'billing',
                },
              },
            ],
          },
        }
      if (method === 'Runtime.enable') {
        mock.emit(
          'Runtime.executionContextCreated',
          { context: { id: 11, auxData: { frameId: 'main', isDefault: true } } },
          's1',
        )
        mock.emit(
          'Runtime.executionContextCreated',
          { context: { id: 22, auxData: { frameId: 'child', isDefault: true } } },
          's1',
        )
        return {}
      }
      if (method === 'Runtime.evaluate' && params.expression === 'iframe')
        return { result: { subtype: 'node', objectId: 'iframe-object' } }
      if (
        method === 'Runtime.evaluate' &&
        params.expression === '({width:innerWidth,height:innerHeight})'
      )
        return { result: { value: { width: 500, height: 400 } } }
      if (method === 'DOM.getFrameOwner') return { backendNodeId: 100 }
      if (method === 'DOM.getBoxModel')
        return { model: { content: [100, 200, 1100, 200, 1100, 1000, 100, 1000] } }
      if (method === 'DOM.describeNode') return { node: { nodeName: 'IFRAME', frameId: 'child' } }
      return undefined
    })
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    expect(await driver.frames?.('p1')).toEqual([
      { id: 'main', pageId: 'p1', url: 'https://site.test' },
      {
        id: 'child',
        pageId: 'p1',
        parentId: 'main',
        url: 'https://site.test/child',
        name: 'billing',
      },
    ])
    expect(await driver.evaluate('p1', '42', 'child')).toBe(42)
    expect(mock.calls.at(-1)).toMatchObject({ sessionId: 's1', params: { contextId: 22 } })
    expect(await driver.resolveFrame?.('p1', 'iframe')).toBe('child')
    expect(mock.calls.at(-1)).toMatchObject({
      method: 'Runtime.releaseObject',
      params: { objectId: 'iframe-object' },
    })
    await driver.click('p1', 20, 30, 'left', 1, 'child')
    expect(mock.calls.at(-1)).toMatchObject({
      sessionId: 's1',
      method: 'Input.dispatchMouseEvent',
      params: { x: 140, y: 260 },
    })
    mock.emit('Runtime.executionContextDestroyed', { executionContextId: 22 }, 's1')
    await expect(driver.evaluate('p1', '42', 'child')).rejects.toThrow('context is unavailable')
    mock.emit(
      'Runtime.executionContextCreated',
      { context: { id: 23, auxData: { frameId: 'child', isDefault: true } } },
      's1',
    )
    expect(await driver.evaluate('p1', '42', 'child')).toBe(42)
    mock.emit('Page.frameDetached', { frameId: 'child', reason: 'remove' }, 's1')
    await expect(driver.evaluate('p1', '42', 'child')).rejects.toThrow('detached')
    await mock.wire.close()
  })

  it('attaches OOPIF renderers once and dispatches frame input through its own renderer', async () => {
    const mock = fakeWire()
    mock.handle(({ method, sessionId, params }) => {
      if (
        method === 'Runtime.evaluate' &&
        params.expression === '({width:innerWidth,height:innerHeight})'
      )
        return { result: { value: { width: 500, height: 400 } } }
      if (method === 'Page.getFrameTree')
        return {
          frameTree:
            sessionId === 'cross-session'
              ? { frame: { id: 'cross', url: 'https://cross.test/frame' } }
              : {
                  frame: { id: 'main', url: 'https://site.test' },
                  childFrames: [
                    { frame: { id: 'cross', parentId: 'main', url: 'https://cross.test/frame' } },
                  ],
                },
        }
      if (method === 'Runtime.enable') {
        mock.emit(
          'Runtime.executionContextCreated',
          {
            context: {
              id: sessionId === 'cross-session' ? 33 : 11,
              auxData: {
                frameId: sessionId === 'cross-session' ? 'cross' : 'main',
                isDefault: true,
              },
            },
          },
          sessionId,
        )
        return {}
      }
      if (method === 'DOM.getFrameOwner') return { backendNodeId: 100 }
      if (method === 'DOM.getBoxModel')
        return { model: { content: [100, 200, 600, 200, 600, 600, 100, 600] } }
      return undefined
    })
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    await driver.preparePage?.('p1')
    mock.emit(
      'Target.attachedToTarget',
      { sessionId: 'cross-session', targetInfo: { targetId: 'cross', type: 'iframe' } },
      's1',
    )
    expect(await driver.evaluate('p1', '42', 'cross')).toBe(42)
    expect(mock.calls.at(-1)).toMatchObject({
      sessionId: 'cross-session',
      params: { contextId: 33 },
    })
    // Old-renderer swap/navigation events may arrive after the new default world.
    mock.emit('Page.frameDetached', { frameId: 'cross', reason: 'swap' }, 's1')
    mock.emit(
      'Page.frameNavigated',
      { frame: { id: 'cross', parentId: 'main', url: 'https://cross.test/frame' } },
      's1',
    )
    mock.emit(
      'Runtime.executionContextCreated',
      { context: { id: 444, auxData: { frameId: 'cross', isDefault: true } } },
      's1',
    )
    expect(await driver.evaluate('p1', '42', 'cross')).toBe(42)
    expect(mock.calls.at(-1)).toMatchObject({
      sessionId: 'cross-session',
      params: { contextId: 33 },
    })
    await driver.click('p1', 20, 30, 'left', 1, 'cross')
    expect(mock.calls.at(-1)).toMatchObject({
      sessionId: 'cross-session',
      method: 'Input.dispatchMouseEvent',
      params: { x: 20, y: 30 },
    })
    mock.emit('Target.detachedFromTarget', { sessionId: 'cross-session' }, 's1')
    await expect(driver.evaluate('p1', '42', 'cross')).rejects.toThrow('detached')
    await mock.wire.close()
  })

  it('awaits late OOPIF default contexts without evaluating in the parent document', async () => {
    const mock = fakeWire(1000)
    mock.handle(({ method, sessionId }) => {
      if (method === 'Page.getFrameTree')
        return {
          frameTree:
            sessionId === 'late-session'
              ? { frame: { id: 'late', url: 'https://late.test' } }
              : {
                  frame: { id: 'main', url: 'https://main.test' },
                  childFrames: [
                    { frame: { id: 'late', parentId: 'main', url: 'https://late.test' } },
                  ],
                },
        }
      if (method === 'Runtime.enable' && sessionId === 'late-session') {
        mock.emit(
          'Runtime.executionContextCreated',
          { context: { id: 99, auxData: { frameId: 'late', isDefault: true } } },
          sessionId,
        )
        return {}
      }
      return undefined
    })
    const driver = createCDPDriver(mock.wire)
    await driver.initialize()
    await driver.preparePage?.('p1')
    let completed = false
    const pending = driver.evaluate('p1', '42', 'late').then((value) => {
      completed = true
      return value
    })
    await new Promise<void>((resolve) => setImmediate(resolve))
    expect(completed).toBe(false)
    mock.emit(
      'Target.attachedToTarget',
      { sessionId: 'late-session', targetInfo: { targetId: 'late', type: 'iframe' } },
      's1',
    )
    expect(await pending).toBe(42)
    const evaluations = mock.calls.filter((call) => call.method === 'Runtime.evaluate')
    expect(evaluations).toHaveLength(1)
    expect(evaluations[0]).toMatchObject({ sessionId: 'late-session', params: { contextId: 99 } })
    await mock.wire.close()
  })
})
