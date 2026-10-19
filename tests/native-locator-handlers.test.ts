// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeFrame, NativeLocator, NativePage } from '../src/native-page.js'
import { NativePacketSchema, type NativeLocatorHandlerStage } from '../src/native-protocol.js'

function deferred() {
  let resolve!: () => void
  const promise = new Promise<void>((accept) => {
    resolve = accept
  })
  return { promise, resolve }
}
function fixture() {
  const processAbort = new AbortController(),
    pageAbort = new AbortController()
  const processClosed = deferred(),
    pageClosed = deferred()
  const listeners = new Set<(stage: NativeLocatorHandlerStage) => void>()
  const runAutomation = vi
    .fn<NativeBrowser['runAutomation']>()
    .mockImplementation((command) =>
      Promise.resolve(
        command === 'locatorHandlerRegister'
          ? { handlerId: 7 }
          : command === 'locatorHandlerOperationBegin'
            ? { operationToken: 31 }
            : undefined,
      ),
    )
  const source: NativeBrowser = {
    pageId: 1,
    state: 'running',
    url: 'about:blank',
    signal: processAbort.signal,
    pageSignal: pageAbort.signal,
    processSignal: processAbort.signal,
    processClosed: processClosed.promise,
    closed: pageClosed.promise,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'controlled',
    failure: new Promise<never>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    onLocatorHandlerStage: (listener) => {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },
  }
  const page = new NativePage(source)
  const emit = (stage: NativeLocatorHandlerStage['stage'], removed?: boolean): void => {
    const clientToken = runAutomation.mock.calls.find(
      (call) => call[0] === 'locatorHandlerRegister',
    )?.[1]?.clientToken
    if (typeof clientToken !== 'string') throw new Error('No actual registration command')
    for (const listener of [...listeners])
      listener({
        stage,
        clientToken,
        id: 8,
        generation: 3,
        token: 5,
        handlerId: 7,
        ...(removed === undefined ? {} : { removed }),
      })
  }
  return { page, source, runAutomation, listeners, emit, processClosed, pageAbort, processAbort }
}
const turn = () =>
  new Promise<void>((resolve) => {
    setImmediate(resolve)
  })
describe('native locator handler thin callback transport', () => {
  it('captures exact lazy child-frame query/strictness and forwards native-only options', async () => {
    const f = fixture()
    const frame = new NativeFrame(f.page, {
      id: 'actual-child',
      parentId: 'root',
      name: '',
      url: 'about:blank',
    })
    const locator = frame.locator('button')
    await f.page.addLocatorHandler(locator, () => undefined, { times: 2, noWaitAfter: true })
    expect(f.runAutomation).toHaveBeenCalledExactlyOnceWith(
      'locatorHandlerRegister',
      expect.objectContaining({
        times: 2,
        noWaitAfter: true,
        target: expect.objectContaining({
          selector: 'button',
          query: [{ engine: 'css', selector: 'button' }],
          framePath: [],
          frame: 'actual-child',
          strict: true,
        }),
      }),
    )
    await expect(f.page.addLocatorHandler(locator, () => undefined)).rejects.toThrow('registered')
    await f.page.removeLocatorHandler(locator)
    expect(f.listeners.size).toBe(0)
    const calls = f.runAutomation.mock.calls.length
    await f.page.removeLocatorHandler(locator)
    expect(f.runAutomation.mock.calls.length).toBe(calls)
    f.processClosed.resolve()
    await turn()
  })
  it('reports the actual callback return and never treats native abort as RAW settlement', async () => {
    const f = fixture(),
      raw = deferred(),
      locator = f.page.locator('overlay')
    const callback = vi.fn(async (value: NativeLocator) => {
      expect(value).toBe(locator)
      await raw.promise
    })
    await f.page.addLocatorHandler(locator, callback)
    f.emit('locatorHandlerRun')
    await turn()
    expect(callback).toHaveBeenCalledOnce()
    f.emit('locatorHandlerAbort')
    await turn()
    expect(
      f.runAutomation.mock.calls.filter((call) => call[0] === 'locatorHandlerRaw'),
    ).toHaveLength(0)
    raw.resolve()
    await turn()
    expect(
      f.runAutomation.mock.calls.find((call) => call[0] === 'locatorHandlerRaw')?.[1],
    ).toMatchObject({
      target: 8,
      generation: 3,
      token: 5,
      handlerId: 7,
      ok: true,
      operationsPending: false,
    })
    f.emit('locatorHandlerRetired', true)
    expect(f.listeners.size).toBe(0)
    f.processClosed.resolve()
    await turn()
  })
  it('retains a real callback across page close until actual RAW and native retirement', async () => {
    const f = fixture(),
      raw = deferred(),
      locator = f.page.locator('overlay')
    await f.page.addLocatorHandler(locator, async () => {
      await raw.promise
    })
    f.emit('locatorHandlerRun')
    await turn()
    f.pageAbort.abort(new Error('actual page closed'))
    expect(f.listeners.size).toBe(1)
    expect(
      f.runAutomation.mock.calls.filter((call) => call[0] === 'locatorHandlerRaw'),
    ).toHaveLength(0)
    raw.resolve()
    await turn()
    f.emit('locatorHandlerRetired', true)
    expect(f.listeners.size).toBe(0)
    f.processClosed.resolve()
    await turn()
  })
  it('does not turn native child death into a still-pending Node callback return', async () => {
    const f = fixture(),
      raw = deferred(),
      locator = f.page.locator('overlay')
    let returned = false
    await f.page.addLocatorHandler(locator, async () => {
      await raw.promise
      returned = true
    })
    f.emit('locatorHandlerRun')
    await turn()
    f.processClosed.resolve()
    await turn()
    expect(returned).toBe(false)
    expect(
      f.runAutomation.mock.calls.filter((call) => call[0] === 'locatorHandlerRaw'),
    ).toHaveLength(0)
    raw.resolve()
    await turn()
    expect(returned).toBe(true)
    expect(f.listeners.size).toBe(0)
  })
  it('does not reuse an external remover wait for the same callback self-removal', async () => {
    const f = fixture(),
      enterSelf = deferred(),
      externalACK = deferred(),
      locator = f.page.locator('overlay')
    const original = new Error('native self-removal cannot await its own callback')
    f.runAutomation.mockImplementation(async (command, params) => {
      if (command === 'locatorHandlerRegister') return { handlerId: 7 }
      if (command === 'locatorHandlerRemove') {
        if (params?.invocationToken === 5) throw original
        await externalACK.promise
      }
      return undefined
    })
    let selfRejected = false
    await f.page.addLocatorHandler(locator, async () => {
      await enterSelf.promise
      await expect(f.page.removeLocatorHandler(locator)).rejects.toBe(original)
      selfRejected = true
    })
    f.emit('locatorHandlerRun')
    await turn()
    const external = f.page.removeLocatorHandler(locator)
    enterSelf.resolve()
    await turn()
    expect(selfRejected).toBe(true)
    expect(
      f.runAutomation.mock.calls
        .filter((call) => call[0] === 'locatorHandlerRemove')
        .map((call) => call[1]?.invocationToken),
    ).toEqual([undefined, 5])
    f.emit('locatorHandlerRetired', true)
    externalACK.resolve()
    await external
    f.processClosed.resolve()
    await turn()
  })

  it('rejects invalid options and foreign targets before native registration', async () => {
    const f = fixture(),
      locator = f.page.locator('overlay')
    await expect(f.page.addLocatorHandler(locator, () => undefined, { times: 0 })).rejects.toThrow()
    expect(f.listeners.size).toBe(0)
    const other = fixture()
    expect(() => f.page.addLocatorHandler(other.page.locator('overlay'), () => undefined)).toThrow(
      'another page',
    )
    expect(f.runAutomation).not.toHaveBeenCalled()
    f.processClosed.resolve()
    other.processClosed.resolve()
    await turn()
  })
  it('preserves the retained node identity when registering a handle-backed locator', async () => {
    const f = fixture(),
      locator = new NativeLocator(f.page, 'fixed', [], [], 17)
    await f.page.addLocatorHandler(locator, () => undefined)
    expect(f.runAutomation).toHaveBeenCalledExactlyOnceWith(
      'locatorHandlerRegister',
      expect.objectContaining({
        target: expect.objectContaining({ nativeHandle: 17, selector: 'fixed' }),
      }),
    )
    await f.page.removeLocatorHandler(locator)
    expect(f.listeners.size).toBe(0)
    f.processClosed.resolve()
    await turn()
  })
  it('retains bounded UTF16 diagnostics and rejects authored coercion descendants after RAW', async () => {
    const f = fixture(),
      locator = f.page.locator('overlay')
    const original = new Error('actual')
    let lateRejected = false
    Object.defineProperty(original, 'message', {
      get() {
        queueMicrotask(() => {
          try {
            void f.page.removeLocatorHandler(locator)
          } catch {
            lateRejected = true
          }
        })
        return 'actual \ud800'
      },
    })
    await f.page.addLocatorHandler(locator, () => {
      throw original
    })
    f.emit('locatorHandlerRun')
    await turn()
    const raw = f.runAutomation.mock.calls.find((call) => call[0] === 'locatorHandlerRaw')?.[1]
    expect(raw).toMatchObject({ ok: false, errorJSON: JSON.stringify('actual \ud800') })
    expect(raw).not.toHaveProperty('error')
    expect(lateRejected).toBe(true)
    expect(
      f.runAutomation.mock.calls.filter((call) => call[0] === 'locatorHandlerRemove'),
    ).toHaveLength(0)
    f.emit('locatorHandlerRetired', true)
    f.processClosed.resolve()
    await turn()
  })

  it('validates handler identity and removed flags without weakening ordinary action stages', () => {
    const header = {
      version: 1,
      bytes: 0,
      pageId: 1,
      type: 'actionstage',
      clientToken: 'native',
      id: 8,
      generation: 3,
      token: 5,
    }
    expect(NativePacketSchema.parse({ ...header, stage: 'beforeStable' }).type).toBe('actionstage')
    expect(() => NativePacketSchema.parse({ ...header, stage: 'locatorHandlerRun' })).toThrow()
    expect(() =>
      NativePacketSchema.parse({
        ...header,
        stage: 'locatorHandlerRun',
        handlerId: 7,
        removed: true,
      }),
    ).toThrow()
    expect(
      NativePacketSchema.parse({
        ...header,
        stage: 'locatorHandlerRetired',
        handlerId: 7,
        removed: true,
      }).type,
    ).toBe('actionstage')
  })
})
