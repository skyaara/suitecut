// SPDX-License-Identifier: MIT
import { afterEach, describe, expect, it, vi } from 'vitest'

import { createCDPDriver } from '../src/browser-cdp.js'
import { type BrowserWire, protocolString } from '../src/browser-wire.js'
import { type UntrustedInput } from '../src/untrusted.js'

type Listener = (params: Record<string, UntrustedInput>, sessionId?: string) => void

const cleanups: (() => Promise<void>)[] = []
afterEach(async () => {
  for (const cleanup of cleanups.splice(0).reverse()) await cleanup()
})

/** Exercise the real driver lifecycle, with deterministic protocol responses. */
function fixture(settings: { downloadSetup?: () => Promise<void> } = {}) {
  const listeners = new Map<string, Set<Listener>>()
  const contexts = new Set<string>()
  const targets = new Map<string, string>()
  let nextContext = 0
  let nextPage = 0
  let connected = true
  let finishClosed!: () => void
  const closed = new Promise<void>((resolve) => {
    finishClosed = resolve
  })
  const emit = (method: string, params: Record<string, UntrustedInput>, sessionId?: string) => {
    for (const listener of [...(listeners.get(method) ?? [])]) listener(params, sessionId)
  }
  const request = vi.fn<BrowserWire['request']>(async (method, params = {}, options = {}) => {
    switch (method) {
      case 'Browser.getVersion':
        return { product: 'Chrome/156.0.0.0' }
      case 'Target.getTargets':
        return { targetInfos: [] }
      case 'Target.getBrowserContexts':
        return { browserContextIds: [...contexts] }
      case 'Target.createBrowserContext': {
        const id = `context-${++nextContext}`
        contexts.add(id)
        return { browserContextId: id }
      }
      case 'Target.createTarget': {
        const id = `page-${++nextPage}`
        const context = typeof params.browserContextId === 'string' ? params.browserContextId : ''
        targets.set(id, context)
        emit('Target.targetCreated', {
          targetInfo: {
            targetId: id,
            browserContextId: context,
            url: 'about:blank',
            title: '',
            type: 'page',
          },
        })
        return { targetId: id }
      }
      case 'Target.attachToTarget':
        return { sessionId: `session-${protocolString(params.targetId, 'target id')}` }
      case 'Browser.setDownloadBehavior': {
        if (params.behavior === 'allowAndName') await settings.downloadSetup?.()
        return {}
      }
      case 'Page.getFrameTree': {
        const page = options.sessionId?.replace('session-', '') ?? ''
        return { frameTree: { frame: { id: `frame-${page}`, url: 'about:blank' } } }
      }
      case 'Target.disposeBrowserContext': {
        const context = protocolString(params.browserContextId, 'context id')
        contexts.delete(context)
        for (const [id, owner] of targets)
          if (owner === context) {
            targets.delete(id)
            emit('Target.targetDestroyed', { targetId: id })
          }
        return {}
      }
      default:
        return {}
    }
  })
  const wire = {
    request,
    timeoutMs: 100,
    closed,
    isConnected: () => connected,
    on(method: string, listener: Listener) {
      const group = listeners.get(method) ?? new Set<Listener>()
      group.add(listener)
      listeners.set(method, group)
      return () => {
        group.delete(listener)
        if (!group.size) listeners.delete(method)
      }
    },
  } as UntrustedInput as BrowserWire
  const driver = createCDPDriver(wire)
  cleanups.push(async () => {
    await driver.disposeResources?.()
    connected = false
    finishClosed()
    await closed
  })
  return { driver, request, forgetContext: (id: string) => contexts.delete(id) }
}

describe('protocol retention under sequential context churn', () => {
  it('does not exhaust the live download-context budget with already closed contexts', async () => {
    const { driver } = fixture()
    await driver.initialize()
    for (let index = 0; index < 1025; index++) {
      const context = await driver.newContext()
      const page = await driver.newPage(context)
      const unsubscribe = await driver.subscribeDownloads!(page.id, () => undefined)
      unsubscribe()
      await driver.closeContext(context)
    }
    expect(await driver.contexts()).toEqual([''])
  })

  it('does not restore download settings on contexts that have already been disposed', async () => {
    const { driver, request } = fixture()
    await driver.initialize()
    for (let index = 0; index < 24; index++) {
      const context = await driver.newContext()
      const page = await driver.newPage(context)
      const unsubscribe = await driver.subscribeDownloads!(page.id, () => undefined)
      unsubscribe()
      await driver.closeContext(context)
    }
    await driver.disposeResources?.()
    const obsoleteResets = request.mock.calls.filter(
      ([method, params]) =>
        method === 'Browser.setDownloadBehavior' && params?.behavior === 'default',
    )
    expect(obsoleteResets).toHaveLength(0)
  })

  it('retires contexts discovered closed by an external owner while preserving live settings', async () => {
    const { driver, request, forgetContext } = fixture()
    await driver.initialize()
    const closedContext = await driver.newContext()
    const liveContext = await driver.newContext()
    for (const context of [closedContext, liveContext]) {
      const page = await driver.newPage(context)
      await driver.subscribeDownloads!(page.id, () => undefined)
    }
    forgetContext(closedContext)
    expect(await driver.contexts()).toEqual(['', liveContext])
    await driver.disposeResources?.()
    const resets = request.mock.calls.filter(
      ([method, params]) =>
        method === 'Browser.setDownloadBehavior' && params?.behavior === 'default',
    )
    expect(resets.map(([, params]) => params?.browserContextId)).toEqual([liveContext])
  })

  it('fences duplicate in-flight setup when a context closes before the response', async () => {
    let signalEntered!: () => void
    let release!: () => void
    const entered = new Promise<void>((resolve) => {
      signalEntered = resolve
    })
    const gate = new Promise<void>((resolve) => {
      release = resolve
    })
    let setups = 0
    const { driver, request } = fixture({
      downloadSetup: async () => {
        if (++setups === 1) {
          signalEntered()
          await gate
        }
      },
    })
    await driver.initialize()
    const context = await driver.newContext()
    const page = await driver.newPage(context)
    const subscriptions = Promise.allSettled([
      driver.subscribeDownloads!(page.id, () => undefined),
      driver.subscribeDownloads!(page.id, () => undefined),
    ])
    await entered
    await driver.closeContext(context)
    release()
    const settled = await subscriptions
    expect(
      settled.every(
        (result) =>
          result.status === 'rejected' &&
          result.reason instanceof Error &&
          result.reason.message.includes('context closed'),
      ),
    ).toBe(true)
    expect(setups).toBe(1)
    const replacement = await driver.newContext()
    const replacementPage = await driver.newPage(replacement)
    await driver.subscribeDownloads!(replacementPage.id, () => undefined)
    await driver.disposeResources?.()
    const resets = request.mock.calls.filter(
      ([method, params]) =>
        method === 'Browser.setDownloadBehavior' && params?.behavior === 'default',
    )
    expect(resets.map(([, params]) => params?.browserContextId)).toEqual([replacement])
  })
})
