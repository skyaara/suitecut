// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { createBiDiDriver } from '../src/browser-bidi.js'
import { BrowserProtocolError, BrowserWire } from '../src/browser-wire.js'
import { type UntrustedInput } from '../src/untrusted.js'

type Reply = Record<string, UntrustedInput>
function mockWire(extra: (method: string, params: Reply) => Reply | Promise<Reply> = () => ({})) {
  const listeners = new Map<string, (params: Reply) => void>()
  let close!: () => void
  const request = vi.fn(async (method: string, params: Reply = {}): Promise<Reply> => {
    if (method === 'session.new')
      return {
        sessionId: 'session',
        capabilities: { browserName: 'firefox', browserVersion: '145', platformName: 'linux' },
      }
    if (method === 'browsingContext.getTree')
      return {
        contexts: [{ context: 'page', userContext: 'default', url: 'about:blank', parent: null }],
      }
    if (method === 'browser.getUserContexts') return { userContexts: [{ userContext: 'default' }] }
    return extra(method, params)
  })
  const wire = Object.assign(Object.create(BrowserWire.prototype) as BrowserWire, {
    request,
    on: (method: string, listener: (params: Reply) => void) => {
      listeners.set(method, listener)
      return () => {
        listeners.delete(method)
      }
    },
    closed: new Promise<void>((resolve) => {
      close = resolve
    }),
  })
  return { wire, request, listeners, close }
}

async function initialized(extra?: (method: string, params: Reply) => Reply | Promise<Reply>) {
  const mock = mockWire(extra)
  const driver = createBiDiDriver(mock.wire)
  await driver.initialize()
  mock.request.mockClear()
  return { ...mock, driver }
}

describe('direct WebDriver BiDi browser driver', () => {
  it('starts a real session, tracks external tabs and navigation, and drops listeners on disconnect', async () => {
    const { driver, request, listeners, close } = await initialized()
    expect(driver.version).toBe('firefox 145')
    expect(await driver.pages()).toEqual([{ id: 'page', contextId: 'default', url: 'about:blank' }])
    listeners.get('browsingContext.contextCreated')?.({
      context: 'child',
      parent: 'page',
      userContext: 'default',
      url: 'about:blank',
    })
    listeners.get('browsingContext.contextCreated')?.({
      context: 'popup',
      parent: null,
      userContext: 'isolated',
      url: 'https://example.test',
    })
    listeners.get('browsingContext.navigationStarted')?.({
      context: 'popup',
      url: 'https://example.test/next',
    })
    expect(await driver.pages()).toHaveLength(2)
    expect((await driver.pages())[1]).toEqual({
      id: 'popup',
      contextId: 'isolated',
      url: 'https://example.test/next',
    })
    listeners.get('browsingContext.contextDestroyed')?.({ context: 'popup' })
    expect(await driver.pages()).toHaveLength(1)
    expect(request).not.toHaveBeenCalled()
    close()
    await Promise.resolve()
    expect(listeners.size).toBe(0)
  })

  it('rejects an occupied endpoint without pretending its connection is session-bound', async () => {
    const existing = mockWire()
    existing.request.mockImplementation(async (method): Promise<Reply> => {
      await Promise.resolve()
      if (method === 'session.new')
        throw new BrowserProtocolError(
          method,
          'session not created',
          'Maximum number of active sessions reached',
        )
      if (method === 'browsingContext.getTree') return { contexts: [] }
      if (method === 'browser.getUserContexts')
        return { userContexts: [{ userContext: 'default' }] }
      return {}
    })
    const occupied = createBiDiDriver(existing.wire)
    await expect(occupied.initialize()).rejects.toThrow('already has an active automation session')
    await occupied.disposeSession?.()
    expect(existing.request).toHaveBeenCalledTimes(1)
    const failed = mockWire()
    failed.request.mockRejectedValue(
      new BrowserProtocolError('session.new', 'session not created', 'profile is corrupt'),
    )
    await expect(createBiDiDriver(failed.wire).initialize()).rejects.toThrow('profile is corrupt')
  })

  it('ends its own automation session once without closing the attached browser', async () => {
    const { driver, request } = await initialized()
    const closing = driver.disposeSession?.()
    expect(driver.disposeSession?.()).toBe(closing)
    await closing
    await driver.disposeSession?.()
    expect(request.mock.calls).toEqual([['session.end']])
  })

  it('retries a transient session.end failure without losing cleanup ownership', async () => {
    const { driver, request } = await initialized()
    request.mockRejectedValueOnce(new Error('temporary session cleanup failure'))
    await expect(driver.disposeSession?.()).rejects.toThrow('temporary session cleanup failure')
    await expect(driver.disposeSession?.()).resolves.toBeUndefined()
    await driver.disposeSession?.()
    expect(request.mock.calls).toEqual([['session.end'], ['session.end']])
  })

  it('retains cleanup ownership if initialization fails after creating the session', async () => {
    const mock = mockWire((method) => {
      if (method === 'session.subscribe') throw new Error('subscription rejected')
      return {}
    })
    const driver = createBiDiDriver(mock.wire)
    await expect(driver.initialize()).rejects.toThrow('subscription rejected')
    await expect(driver.disposeSession?.()).resolves.toBeUndefined()
    expect(mock.request).toHaveBeenLastCalledWith('session.end')
    const untouched = mockWire()
    await createBiDiDriver(untouched.wire).disposeSession?.()
    expect(untouched.request).not.toHaveBeenCalled()
  })

  it('reports unsupported user contexts and does not fake isolation', async () => {
    const mock = mockWire()
    mock.request.mockImplementation(async (method): Promise<Reply> => {
      await Promise.resolve()
      if (method === 'session.new') return { capabilities: {} }
      if (method === 'browsingContext.getTree') return { contexts: [] }
      if (method === 'browser.getUserContexts')
        throw new BrowserProtocolError(method, 'unknown command', 'not available')
      return {}
    })
    const driver = createBiDiDriver(mock.wire)
    await driver.initialize()
    expect(driver.capabilities.isolatedContexts).toBe(false)
    expect(await driver.contexts()).toEqual(['default'])
    await expect(driver.newContext()).rejects.toThrow('does not support isolated')
  })

  it('creates actual user contexts, tabs, and asks the browser to await navigation readiness', async () => {
    const { driver, request } = await initialized((method) => {
      if (method === 'browser.createUserContext') return { userContext: 'private' }
      if (method === 'browsingContext.create') return { context: 'new' }
      if (method === 'browsingContext.navigate') return { url: 'https://example.test/redirected' }
      return {}
    })
    expect(await driver.newContext()).toBe('private')
    expect(await driver.newPage('private')).toEqual({
      id: 'new',
      contextId: 'private',
      url: 'about:blank',
    })
    await driver.navigate('new', 'https://example.test/', 'interactive')
    expect(request).toHaveBeenCalledWith('browsingContext.navigate', {
      context: 'new',
      url: 'https://example.test/',
      wait: 'interactive',
    })
    expect((await driver.pages()).find((page) => page.id === 'new')?.url).toBe(
      'https://example.test/redirected',
    )
    await driver.closeContext('private')
    expect(await driver.pages()).toHaveLength(1)
    await expect(driver.closeContext('default')).rejects.toThrow('cannot be removed')
  })

  it('awaits page promises and decodes cyclic structured values safely', async () => {
    const { driver, request } = await initialized(() => ({
      type: 'success',
      result: {
        type: 'object',
        internalId: 'o',
        value: [
          ['self', { type: 'object', internalId: 'o' }],
          ['__proto__', { type: 'string', value: 'safe' }],
          ['big', { type: 'bigint', value: '12345678901234567890' }],
          ['negativeZero', { type: 'number', value: '-0' }],
          [
            'items',
            { type: 'array', value: [{ type: 'undefined' }, { type: 'number', value: 'NaN' }] },
          ],
          [
            'mapping',
            {
              type: 'map',
              value: [
                [
                  { type: 'number', value: 5 },
                  { type: 'set', value: [{ type: 'boolean', value: true }] },
                ],
              ],
            },
          ],
        ],
      },
    }))
    const value = (await driver.evaluate('page', 'Promise.resolve(data)')) as Record<
      string,
      UntrustedInput
    >
    expect(value.self).toBe(value)
    expect(Object.getPrototypeOf(value)).toBe(Object.prototype)
    expect(Object.getOwnPropertyDescriptor(value, '__proto__')?.value).toBe('safe')
    expect(value.big).toBe(12345678901234567890n)
    expect(Object.is(value.negativeZero, -0)).toBe(true)
    expect(value.items).toEqual([undefined, NaN])
    expect(value.mapping).toEqual(new Map([[5, new Set([true])]]))
    expect(request).toHaveBeenCalledWith(
      'script.evaluate',
      expect.objectContaining({ awaitPromise: true, resultOwnership: 'none' }),
    )
  })

  it('surfaces page exceptions and unsupported remote objects without pretending success', async () => {
    const { driver, request } = await initialized()
    request.mockResolvedValueOnce({
      type: 'exception',
      exceptionDetails: { text: 'Error: page failed' },
    })
    await expect(driver.evaluate('page', 'throw Error()')).rejects.toThrow('page failed')
    request.mockResolvedValueOnce({ type: 'success', result: { type: 'node', sharedId: 'node' } })
    await expect(driver.evaluate('page', 'document.body')).rejects.toThrow('cannot return node')
    request.mockResolvedValueOnce({ type: 'success', result: { type: 'object' } })
    await expect(driver.evaluate('page', 'data')).rejects.toThrow('expected an array')
  })

  it('batches Unicode keyboard input into a genuine browser input command', async () => {
    const { driver, request } = await initialized()
    await driver.insertText('page', 'A😀é')
    expect(request).toHaveBeenCalledTimes(1)
    expect(request).toHaveBeenCalledWith('input.performActions', {
      context: 'page',
      actions: [
        {
          type: 'key',
          id: 'suitecut-keyboard',
          actions: [
            { type: 'keyDown', value: 'A' },
            { type: 'keyUp', value: 'A' },
            { type: 'keyDown', value: '😀' },
            { type: 'keyUp', value: '😀' },
            { type: 'keyDown', value: 'é' },
            { type: 'keyUp', value: 'é' },
          ],
        },
      ],
    })
    request.mockClear()
    await driver.insertText('page', '')
    expect(request).not.toHaveBeenCalled()
  })

  it('bounds long input command size without splitting Unicode scalars', async () => {
    const { driver, request } = await initialized()
    await driver.insertText('page', '😀'.repeat(4097))
    expect(request).toHaveBeenCalledTimes(2)
    expect(JSON.stringify(request.mock.calls[0])).not.toContain('\\ud83d')
  })

  it('uses modifier key chords and releases input state after partial failure', async () => {
    const { driver, request } = await initialized()
    await driver.press('page', 'ControlOrMeta+A')
    expect(request).toHaveBeenCalledWith(
      'input.performActions',
      expect.objectContaining({
        actions: [
          {
            type: 'key',
            id: 'suitecut-keyboard',
            actions: [
              { type: 'keyDown', value: '\uE009' },
              { type: 'keyDown', value: 'A' },
              { type: 'keyUp', value: 'A' },
              { type: 'keyUp', value: '\uE009' },
            ],
          },
        ],
      }),
    )
    request.mockRejectedValueOnce(new Error('injected pointer failure'))
    await expect(driver.click('page', 10, 20, 'left', 2)).rejects.toThrow(
      'injected pointer failure',
    )
    expect(request).toHaveBeenLastCalledWith('input.releaseActions', { context: 'page' })
    await expect(driver.press('page', 'ImaginaryKey')).rejects.toThrow('Unsupported browser key')
  })

  it('uses browser pointer and wheel events with viewport coordinates', async () => {
    const { driver, request } = await initialized()
    await driver.click('page', 10.4, 20.8, 'right', 1)
    expect(request).toHaveBeenCalledWith(
      'input.performActions',
      expect.objectContaining({
        actions: [
          {
            type: 'pointer',
            id: 'suitecut-mouse',
            parameters: { pointerType: 'mouse' },
            actions: [
              { type: 'pointerMove', x: 10, y: 21, duration: 0, origin: 'viewport' },
              { type: 'pointerDown', button: 2 },
              { type: 'pointerUp', button: 2 },
            ],
          },
        ],
      }),
    )
    await driver.scroll('page', 12, 25, 0, 300)
    expect(request).toHaveBeenLastCalledWith(
      'input.performActions',
      expect.objectContaining({
        actions: [
          {
            type: 'wheel',
            id: 'suitecut-wheel',
            actions: [
              {
                type: 'scroll',
                origin: 'viewport',
                x: 12,
                y: 25,
                deltaX: 0,
                deltaY: 300,
                duration: 0,
              },
            ],
          },
        ],
      }),
    )
  })

  it('captures screenshots at requested quality and changes the real browser viewport', async () => {
    const { driver, request } = await initialized((method) =>
      method === 'browsingContext.captureScreenshot'
        ? { data: Buffer.from('PNG').toString('base64') }
        : {},
    )
    expect(await driver.screenshot('page', { type: 'jpeg', quality: 80, fullPage: true })).toEqual(
      Buffer.from('PNG'),
    )
    expect(request).toHaveBeenCalledWith('browsingContext.captureScreenshot', {
      context: 'page',
      origin: 'document',
      format: { type: 'image/jpeg', quality: 0.8 },
    })
    await driver.setViewport('page', { width: 1280, height: 720 })
    expect(request).toHaveBeenLastCalledWith('browsingContext.setViewport', {
      context: 'page',
      viewport: { width: 1280, height: 720 },
    })
  })

  it('isolates cookies by actual user context and preserves session-cookie semantics', async () => {
    const { driver, request } = await initialized((method) =>
      method === 'storage.getCookies'
        ? {
            cookies: [
              {
                name: 'token',
                value: { type: 'base64', value: 'aGVsbG8=' },
                domain: 'example.test',
                path: '/',
                secure: true,
                httpOnly: true,
                sameSite: 'strict',
                expiry: 1234,
              },
            ],
          }
        : {},
    )
    expect(await driver.cookies('private')).toEqual([
      {
        name: 'token',
        value: 'hello',
        domain: 'example.test',
        path: '/',
        secure: true,
        httpOnly: true,
        sameSite: 'Strict',
        expires: 1234,
      },
    ])
    await driver.setCookies('private', [
      { name: 'key', value: 'value', domain: 'example.test', expires: -1 },
    ])
    expect(request).toHaveBeenLastCalledWith('storage.setCookie', {
      partition: { type: 'storageKey', userContext: 'private' },
      cookie: {
        name: 'key',
        value: { type: 'string', value: 'value' },
        domain: 'example.test',
        path: '/',
        secure: false,
        httpOnly: false,
        sameSite: 'lax',
      },
    })
  })
})
