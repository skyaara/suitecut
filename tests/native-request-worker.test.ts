// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { NativeNetwork, NativeRequest } from '../src/native-network.js'
import { NativeConsoleMessage } from '../src/native-page.js'
import {
  NativeWorkerControlSchema,
  type NativeWorkerEvent,
  type NativeWorkerInfo,
} from '../src/native-worker-protocol.js'
import {
  NativeWorkerEvents,
  NativeWorkerHandle,
  type NativeWorkerOwner,
} from '../src/native-worker.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture(initial: NativeWorkerInfo[] = [], alreadyAborted = false) {
  const lifetime = new AbortController()
  if (alreadyAborted) lifetime.abort()
  let physicalClose: () => void = () => {
    throw new Error('Physical receipt resolver is not initialized')
  }
  const processClosed = new Promise<void>((resolve) => {
    physicalClose = resolve
  })
  let inventory = initial
  const listeners = new Set<(event: NativeWorkerEvent) => void>()
  const runWorker = vi.fn<NativeWorkerOwner['runWorker']>(() => Promise.resolve(undefined))
  const owner: NativeWorkerOwner = {
    source: {
      signal: lifetime.signal,
      processClosed,
      runAutomation: () => Promise.resolve(undefined),
      workerInfos: () => inventory,
      onWorkerMessage: (listener) => {
        listeners.add(listener)
        return () => listeners.delete(listener)
      },
    },
    runWorker,
    defaultTimeout: () => 100,
    isPageHandle: () => false,
    createConsole: () => {
      throw new Error('No console event in this fixture')
    },
  }
  const workers = NativeWorkerEvents.forOwner(owner)
  const discovered = vi.fn()
  if (!alreadyAborted) workers.on(discovered)
  const resolve = vi.fn((info: NativeWorkerInfo) => workers.fromObservedInfo(info))
  const network = new NativeNetwork(undefined, undefined, {
    signal: lifetime.signal,
    defaultTimeout: () => 100,
    command: () => Promise.resolve(undefined),
    body: () => Promise.resolve(undefined),
    worker: resolve,
  })
  return {
    network,
    source: owner.source,
    workers,
    discovered,
    resolve,
    runWorker,
    lifecycleSubscriptions: () => listeners.size,
    inventory: (value: NativeWorkerInfo[]) => {
      inventory = value
    },
    emit: (event: NativeWorkerEvent) => {
      for (const listener of listeners) listener(event)
    },
    logicalAbort: () => lifetime.abort(),
    physicalClose: () => physicalClose(),
    close: () => {
      lifetime.abort()
      physicalClose()
    },
  }
}
const actualWorker: NativeWorkerInfo = {
  id: 'actual-service-target',
  url: 'https://native.test/sw.js',
  type: 'service_worker',
}
function request(
  f: ReturnType<typeof fixture>,
  id = 'request:1',
  worker = actualWorker,
  serviceWorker = true,
) {
  return new NativeRequest(f.network, {
    id,
    requestId: id,
    session: 'actual-session',
    frameId: '',
    url: 'https://native.test/resource',
    method: 'GET',
    resourceType: 'Fetch',
    headers: {},
    isNavigationRequest: false,
    serviceWorker,
    workerInfo: worker,
  })
}

describe('actual request service-worker target identity', () => {
  it('shares the current canonical worker and retains it after actual close', () => {
    const f = fixture([actualWorker])
    const first = request(f),
      second = request(f, 'request:2')
    const canonical = f.workers.allTargets()[0]
    expect(first.serviceWorker()).toBe(canonical)
    expect(second.serviceWorker()).toBe(canonical)
    f.inventory([])
    f.emit({ event: 'close', worker: actualWorker, sequence: 1 })
    expect(first.serviceWorker()).toBe(canonical)
    expect(request(f, 'request:3').serviceWorker()).toBe(canonical)
    expect(() => canonical?.assertUsable()).toThrow('native worker closed')
    expect(f.discovered).not.toHaveBeenCalled()
    f.close()
  })
  it('keeps unpublished target observations weak-canonical until a genuine close', () => {
    const f = fixture()
    const first = request(f),
      second = request(f, 'request:2')
    expect(first.serviceWorker()).not.toBeNull()
    expect(second.serviceWorker()).toBe(first.serviceWorker())
    expect(() => first.serviceWorker()?.assertUsable()).not.toThrow()
    expect(f.workers.allTargets()).toEqual([])
    f.emit({ event: 'close', worker: actualWorker, sequence: 1 })
    expect(() => first.serviceWorker()?.assertUsable()).toThrow('native worker closed')
    expect(f.discovered).not.toHaveBeenCalled()
    f.close()
  })
  it('retains published and observed identity after logical abort until actual process closure', async () => {
    const f = fixture([actualWorker])
    const published = request(f).serviceWorker()
    const pending = request(f, 'pending', {
      ...actualWorker,
      id: 'actual-unpublished-target',
    }).serviceWorker()
    const publishedClosed = vi.fn(),
      pendingClosed = vi.fn()
    published?.on('close', publishedClosed)
    pending?.on('close', pendingClosed)
    f.logicalAbort()
    expect(published?.isClosed()).toBe(false)
    expect(pending?.isClosed()).toBe(false)
    expect(publishedClosed).not.toHaveBeenCalled()
    expect(pendingClosed).not.toHaveBeenCalled()
    expect(f.workers.allTargets()).toEqual([published])
    f.physicalClose()
    await f.source.processClosed
    expect(published?.isClosed()).toBe(true)
    expect(pending?.isClosed()).toBe(true)
    expect(publishedClosed).toHaveBeenCalledExactlyOnceWith(published)
    expect(pendingClosed).toHaveBeenCalledExactlyOnceWith(pending)
    expect(f.workers.allTargets()).toEqual([])
  })
  it('keeps real target lifecycle delivery after a context-only abort and channel loss', async () => {
    const f = fixture()
    const observed = request(f).serviceWorker()
    const closed = vi.fn()
    observed?.on('close', closed)
    f.logicalAbort()
    f.emit({
      event: 'channelclosed',
      worker: actualWorker,
      sequence: 1,
      reason: 'Actual session closed',
    })
    expect(observed?.isClosed()).toBe(false)
    expect(closed).not.toHaveBeenCalled()
    f.emit({ event: 'close', worker: actualWorker, sequence: 2 })
    expect(observed?.isClosed()).toBe(true)
    expect(closed).toHaveBeenCalledExactlyOnceWith(observed)
    f.physicalClose()
    await f.source.processClosed
    expect(closed).toHaveBeenCalledTimes(1)
  })
  it('handles an already logically aborted adapter without losing later genuine closure', () => {
    const f = fixture([], true)
    expect(f.lifecycleSubscriptions()).toBe(0)
    const observed = request(f).serviceWorker()
    expect(observed?.isClosed()).toBe(false)
    expect(f.lifecycleSubscriptions()).toBe(1)
    f.emit({ event: 'close', worker: actualWorker, sequence: 1 })
    expect(observed?.isClosed()).toBe(true)
    expect(f.lifecycleSubscriptions()).toBe(0)
    const later = { ...actualWorker, id: 'actual-later-observation' }
    const retained = request(f, 'later', later).serviceWorker()
    expect(retained?.isClosed()).toBe(false)
    expect(f.lifecycleSubscriptions()).toBe(1)
    f.emit({ event: 'close', worker: later, sequence: 2 })
    expect(retained?.isClosed()).toBe(true)
    expect(f.lifecycleSubscriptions()).toBe(0)
    f.close()
  })
  it('recovers only on actual newer readiness and keeps old handles and console arguments invalid', async () => {
    const f = fixture([actualWorker])
    f.emit({ event: 'worker', worker: actualWorker, sequence: 10 })
    const worker = request(f).serviceWorker()
    expect(worker).not.toBeNull()
    if (!worker) throw new Error('Actual worker unavailable')
    const handle = worker.adoptHandle({
      handle: 1,
      guid: 'actual-old',
      preview: 'old',
      node: false,
      worker: actualWorker.id,
    })
    const info = {
      type: 'log',
      text: 'old console',
      worker: actualWorker.id,
      workerInfo: actualWorker,
      location: { url: actualWorker.url, lineNumber: 1, columnNumber: 1 },
      arguments: [
        {
          handle: 2,
          guid: 'actual-old-console',
          preview: 'old',
          node: false as const,
          worker: actualWorker.id,
        },
      ],
    }
    const owner = {
      source: f.source,
      workerFromConsole: (actual: NativeWorkerInfo) => f.workers.fromConsole(actual),
    }
    const cached = new NativeConsoleMessage(owner, info),
      delayed = new NativeConsoleMessage(owner, info)
    cached.args()
    f.inventory([])
    f.emit({
      event: 'channelclosed',
      worker: actualWorker,
      sequence: 11,
      reason: 'Actual old session detached',
    })
    const epoch = worker.captureChannelEpoch()
    f.emit({
      event: 'channelclosed',
      worker: actualWorker,
      sequence: 11,
      reason: 'Actual old session detached',
    })
    expect(worker.captureChannelEpoch()).toBe(epoch)
    expect(() => worker.assertUsable()).toThrow('Actual old session detached')
    const closedInterval = new NativeConsoleMessage(owner, info)
    f.inventory([actualWorker])
    f.emit({ event: 'worker', worker: actualWorker, sequence: 12 })
    expect(request(f, 'new').serviceWorker()).toBe(worker)
    expect(() => worker.assertUsable()).not.toThrow()
    expect(() => handle.assertUsable(worker)).toThrow('earlier Inspector control channel')
    expect(() => cached.args()).toThrow('earlier Inspector control channel')
    expect(() => delayed.args()).toThrow('earlier Inspector control channel')
    expect(cached.worker()).toBe(worker)
    expect(closedInterval.worker()).toBe(worker)
    expect(() => closedInterval.args()).toThrow('Actual old session detached')
    await handle.dispose()
    expect(f.runWorker).not.toHaveBeenCalled()
    const fresh = worker.adoptHandle({
      handle: 3,
      guid: 'actual-new',
      preview: 'new',
      node: false,
      worker: actualWorker.id,
    })
    expect(() => fresh.assertUsable(worker)).not.toThrow()
    await fresh.dispose()
    expect(f.runWorker).toHaveBeenCalledTimes(1)
    f.close()
  })
  it('does not apply older historical close or channel records after actual newer readiness', () => {
    const f = fixture([actualWorker])
    f.emit({ event: 'worker', worker: actualWorker, sequence: 10 })
    const worker = request(f).serviceWorker()
    f.emit({ event: 'channelclosed', worker: actualWorker, sequence: 11 })
    f.emit({ event: 'worker', worker: actualWorker, sequence: 12 })
    expect(
      f.workers.fromRecord({ event: 'channelclosed', worker: actualWorker, sequence: 11 }),
    ).toBe(worker)
    expect(f.workers.fromRecord({ event: 'close', worker: actualWorker, sequence: 10 })).toBe(
      worker,
    )
    expect(worker?.isClosed()).toBe(false)
    expect(() => worker?.assertUsable()).not.toThrow()
    f.close()
  })
  it('never resurrects actual target death and closes a recovered channel at real process exit', async () => {
    const f = fixture([actualWorker])
    f.emit({ event: 'worker', worker: actualWorker, sequence: 1 })
    const worker = request(f).serviceWorker()
    f.emit({ event: 'close', worker: actualWorker, sequence: 2 })
    f.emit({ event: 'worker', worker: actualWorker, sequence: 3 })
    expect(worker?.isClosed()).toBe(true)
    expect(f.workers.allTargets()).toEqual([])
    const alive = { ...actualWorker, id: 'actual-recovered' }
    const recovered = request(f, 'alive', alive).serviceWorker()
    f.emit({ event: 'worker', worker: alive, sequence: 4 })
    f.emit({ event: 'channelclosed', worker: alive, sequence: 5 })
    f.emit({ event: 'worker', worker: alive, sequence: 6 })
    expect(recovered?.isClosed()).toBe(false)
    f.physicalClose()
    await f.source.processClosed
    expect(recovered?.isClosed()).toBe(true)
    f.close()
  })
  it('does not resurrect a genuinely retired target from a late native ready journal response', () => {
    const f = fixture()
    f.emit({ event: 'close', worker: actualWorker, sequence: 2 })
    const historical = f.workers.fromRecord({ event: 'worker', worker: actualWorker, sequence: 1 })
    expect(historical.isClosed()).toBe(true)
    expect(f.workers.allTargets()).toEqual([])
    expect(request(f).serviceWorker()).toBe(historical)
    f.close()
  })
  it('preserves channel-only history and pending updates when projecting native journal records', () => {
    const f = fixture()
    f.emit({
      event: 'channelclosed',
      worker: actualWorker,
      sequence: 2,
      reason: 'Actual channel receipt',
    })
    const historical = f.workers.fromRecord({ event: 'worker', worker: actualWorker, sequence: 1 })
    expect(historical.isClosed()).toBe(false)
    expect(() => historical.assertUsable()).toThrow('Actual channel receipt')
    const pending = f.workers.fromRecord({
      event: 'updated',
      worker: { ...actualWorker, id: 'actual-updated-unpublished' },
      sequence: 3,
    })
    expect(pending.isClosed()).toBe(false)
    expect(f.workers.allTargets()).toEqual([])
    f.close()
  })
  it('rejects an unidentifiable old native ready record after retirement history loss', () => {
    const f = fixture()
    for (let index = 0; index < 1025; ++index)
      f.emit({
        event: 'close',
        worker: { ...actualWorker, id: `retired:${index}` },
        sequence: index + 1,
      })
    expect(() =>
      f.workers.fromRecord({ event: 'worker', worker: actualWorker, sequence: 1 }),
    ).toThrow('retirement history capacity exceeded')
    f.close()
  })
  it('uses genuine close and channel receipts before the first observed object exists', () => {
    const f = fixture()
    f.emit({ event: 'close', worker: actualWorker, sequence: 1 })
    const closed = request(f).serviceWorker()
    expect(closed?.isClosed()).toBe(true)
    const channel = { ...actualWorker, id: 'actual-channel-only' }
    f.emit({
      event: 'channelclosed',
      worker: channel,
      sequence: 2,
      reason: 'Real control channel lost',
    })
    const unavailable = request(f, 'channel', channel).serviceWorker()
    expect(unavailable?.isClosed()).toBe(false)
    expect(() => unavailable?.assertUsable()).toThrow('Real control channel lost')
    f.emit({ event: 'close', worker: channel, sequence: 3 })
    expect(unavailable?.isClosed()).toBe(true)
    expect(f.discovered).not.toHaveBeenCalled()
    f.close()
  })
  it('rejects late unknown identity after bounded real receipt history is lost', () => {
    const f = fixture()
    const retained = request(f).serviceWorker()
    f.emit({ event: 'channelclosed', worker: actualWorker, sequence: 1 })
    for (let index = 0; index < 1025; ++index) {
      const actual = { ...actualWorker, id: `actually-closed:${index}` }
      f.emit({ event: 'close', worker: actual, sequence: index + 2 })
      expect(request(f, actual.id, actual).serviceWorker()?.isClosed()).toBe(true)
    }
    expect(request(f, 'retained').serviceWorker()).toBe(retained)
    expect(() => request(f, 'late', { ...actualWorker, id: 'unavailable-old-target' })).toThrow(
      'retirement history capacity exceeded',
    )
    f.emit({ event: 'close', worker: actualWorker, sequence: 1027 })
    expect(retained?.isClosed()).toBe(true)
    f.close()
  })
  it('keeps later observations closed after the actual process receipt', async () => {
    const f = fixture()
    f.physicalClose()
    await f.source.processClosed
    const observed = request(f).serviceWorker()
    expect(observed?.isClosed()).toBe(true)
    expect(request(f, 'again').serviceWorker()).toBe(observed)
    expect(f.workers.allTargets()).toEqual([])
    f.close()
  })
  it('refreshes an unannounced identity from genuine observed snapshots and update events', () => {
    const f = fixture()
    const first = request(f, 'initial', { ...actualWorker, url: '' })
    const observed = first.serviceWorker()
    expect(observed?.url()).toBe('')
    expect(request(f, 'snapshot', actualWorker).serviceWorker()).toBe(observed)
    expect(observed?.url()).toBe(actualWorker.url)
    f.emit({
      event: 'updated',
      worker: { ...actualWorker, url: 'https://native.test/actual-updated.js' },
      sequence: 1,
    })
    expect(observed?.url()).toBe('https://native.test/actual-updated.js')
    expect(f.workers.allTargets()).toEqual([])
    expect(f.workers.list()).toEqual([])
    expect(f.discovered).not.toHaveBeenCalled()
    f.close()
  })
  it('preserves request-before-ready identity when real readiness is published later', () => {
    const f = fixture()
    const first = request(f)
    const observed = first.serviceWorker()
    expect(f.workers.allTargets()).toEqual([])
    f.inventory([{ ...actualWorker, url: 'https://native.test/published-sw.js' }])
    f.emit({
      event: 'worker',
      worker: { ...actualWorker, url: 'https://native.test/published-sw.js' },
      sequence: 1,
    })
    expect(first.serviceWorker()).toBe(observed)
    expect(f.workers.allTargets()[0]).toBe(observed)
    expect(request(f, 'request:2').serviceWorker()).toBe(observed)
    f.close()
  })
  it('keeps console metadata on the same unannounced and then genuinely closed target', () => {
    const f = fixture()
    const first = request(f)
    expect(f.workers.fromConsole(actualWorker)).toBe(first.serviceWorker())
    expect(f.workers.allTargets()).toEqual([])
    f.emit({ event: 'close', worker: actualWorker, sequence: 1 })
    expect(f.workers.fromConsole(actualWorker)).toBe(first.serviceWorker())
    expect(() => f.workers.fromConsole(actualWorker).assertUsable()).toThrow('native worker closed')
    expect(f.workers.allTargets()).toEqual([])
    f.close()
  })
  it('applies genuine channel-close to an unpublished retained observation', () => {
    const f = fixture()
    const first = request(f)
    f.emit({
      event: 'channelclosed',
      worker: actualWorker,
      sequence: 1,
      reason: 'Actual pending channel closed',
    })
    expect(() => first.serviceWorker()?.assertUsable()).toThrow('Actual pending channel closed')
    expect(request(f, 'request:2').serviceWorker()).toBe(first.serviceWorker())
    expect(f.workers.allTargets()).toEqual([])
    f.close()
  })
  it('retains the actual channel-closed object and reason', () => {
    const f = fixture([actualWorker])
    const first = request(f)
    f.emit({
      event: 'channelclosed',
      worker: actualWorker,
      sequence: 1,
      reason: 'Actual Inspector channel detached',
    })
    const second = request(f, 'request:2')
    expect(second.serviceWorker()).toBe(first.serviceWorker())
    expect(() => second.serviceWorker()?.assertUsable()).toThrow(
      'Actual Inspector channel detached',
    )
    expect(f.discovered).not.toHaveBeenCalled()
    f.close()
  })
  it('uses the genuine current target inventory when observation is older', () => {
    const f = fixture()
    f.inventory([{ ...actualWorker, url: 'https://native.test/current-sw.js' }])
    const observed = request(f)
    expect(observed.serviceWorker()?.url()).toBe('https://native.test/current-sw.js')
    expect(() => observed.serviceWorker()?.assertUsable()).not.toThrow()
    expect(f.discovered).not.toHaveBeenCalled()
    f.close()
  })
  it('returns null for ordinary or dedicated-worker requests', () => {
    const f = fixture()
    expect(request(f, 'ordinary', actualWorker, false).serviceWorker()).toBeNull()
    expect(
      request(
        f,
        'dedicated',
        {
          ...actualWorker,
          type: 'worker',
        },
        false,
      ).serviceWorker(),
    ).toBeNull()
    expect(f.resolve).not.toHaveBeenCalled()
    f.close()
  })
  it('bounds unannounced identities without evicting retained lifecycle observers', () => {
    const f = fixture()
    const retained = Array.from({ length: 1024 }, (_, index) =>
      request(f, `request:${index}`, { ...actualWorker, id: `observed:${index}` }),
    )
    expect(() => request(f, 'overflow', { ...actualWorker, id: 'observed:overflow' })).toThrow(
      'identity capacity exceeded',
    )
    const first = retained[0]
    expect(first).toBeDefined()
    f.emit({ event: 'close', worker: { ...actualWorker, id: 'observed:0' }, sequence: 1 })
    expect(() => first?.serviceWorker()?.assertUsable()).toThrow('native worker closed')
    expect(
      request(f, 'replacement', { ...actualWorker, id: 'observed:replacement' }).serviceWorker(),
    ).not.toBeNull()
    f.close()
  })
  it('does not turn unavailable genuine worker ownership into null or a fabricated worker', () => {
    const lifetime = new AbortController()
    const network = new NativeNetwork(undefined, undefined, {
      signal: lifetime.signal,
      defaultTimeout: () => 100,
      command: () => Promise.resolve(undefined),
      body: () => Promise.resolve(undefined),
    })
    const observed = new NativeRequest(network, {
      id: 'request:1',
      requestId: 'actual-request',
      session: 'actual-session',
      frameId: '',
      url: 'https://native.test/resource',
      method: 'GET',
      resourceType: 'Fetch',
      headers: {},
      isNavigationRequest: false,
      serviceWorker: true,
      workerInfo: actualWorker,
    })
    expect(() => observed.serviceWorker()).toThrow(
      'target identity or owner channel is unavailable',
    )
    lifetime.abort()
  })
  it('adopts only a later genuine attribution update while preserving request identity', () => {
    const f = fixture([actualWorker])
    const observed = new NativeRequest(
      f.network,
      {
        id: 'request:1',
        requestId: 'actual-request',
        session: 'actual-session',
        frameId: '',
        url: 'https://native.test/resource',
        method: 'GET',
        resourceType: 'Fetch',
        headers: {},
        isNavigationRequest: false,
      },
      1,
    )
    expect(observed.serviceWorker()).toBeNull()
    observed.observe({ ...observed.info, workerInfo: actualWorker, serviceWorker: true }, 2)
    expect(observed.serviceWorker()).toBe(f.workers.allTargets()[0])
    expect(f.discovered).not.toHaveBeenCalled()
    f.close()
  })
})

describe('pending worker control replies retain their original channel', () => {
  it('rejects a late old-channel handle result after actual replacement readiness', async () => {
    const f = fixture()
    f.emit({ event: 'worker', worker: actualWorker, sequence: 1 })
    const worker = request(f).serviceWorker()
    if (!worker) throw new Error('Actual worker unavailable')
    let receipt: (value: UntrustedInput) => void = () => {
      throw new Error('No pending receipt')
    }
    f.runWorker.mockImplementationOnce(
      () =>
        new Promise<UntrustedInput>((resolve) => {
          receipt = resolve
        }),
    )
    const pending = worker.evaluateHandle('({ actual: true })')
    const rejected = expect(pending).rejects.toThrow('earlier Inspector control channel')
    f.emit({ event: 'channelclosed', worker: actualWorker, sequence: 2 })
    f.emit({ event: 'worker', worker: actualWorker, sequence: 3 })
    receipt({
      worker: actualWorker.id,
      handle: 1,
      guid: 'old-real-control-reply',
      preview: 'object',
    })
    await rejected
    expect(f.runWorker).toHaveBeenCalledTimes(1)
    expect(() => worker.assertUsable()).not.toThrow()
    f.close()
  })
})

describe('worker channel error provenance', () => {
  it('keeps cached console identity and reports proved replacement control before RPC', () => {
    const first = { ...actualWorker, control: { owner: 'observed-source', generation: '1' } }
    const next = { ...actualWorker, control: { owner: 'observed-source', generation: '2' } }
    const f = fixture([first])
    f.emit({ event: 'worker', worker: first, sequence: 10 })
    const worker = f.workers.fromObservedInfo(first)
    const owner = {
      source: f.source,
      workerFromConsole: (info: NativeWorkerInfo) => f.workers.fromConsole(info),
    }
    const info = {
      sequence: 41,
      type: 'log',
      text: 'observed old console',
      worker: first.id,
      workerInfo: first,
      location: { url: first.url, lineNumber: 1, columnNumber: 1 },
      arguments: [
        {
          handle: 1,
          guid: 'observed-control-console',
          preview: 'object',
          node: false as const,
          worker: first.id,
          workerControl: first.control,
        },
      ],
    }
    const cached = new NativeConsoleMessage(owner, info)
    const handle = cached.args()[0]
    if (!(handle instanceof NativeWorkerHandle))
      throw new Error('Controlled console worker handle missing')
    f.inventory([])
    f.emit({ event: 'channelclosed', worker: first, sequence: 11 })
    expect(() => cached.args()).toThrow('earlier Inspector control channel')
    expect(() => cached.args()).not.toThrow('another Inspector owner or attachment generation')
    f.inventory([next])
    f.emit({ event: 'worker', worker: next, sequence: 12 })
    const delayed = new NativeConsoleMessage(owner, info)
    expect(delayed).toBe(cached)
    expect(() => delayed.args()).toThrow('another Inspector owner or attachment generation')
    expect(() => cached.args()).toThrow('earlier Inspector control channel')
    expect(() => handle.assertUsable(worker)).toThrow(
      'another Inspector owner or attachment generation',
    )
    expect(() => handle.assertUsable(worker)).toThrow('earlier Inspector control channel')
    expect(() => worker.assertChannelEpoch(0)).toThrow('earlier Inspector control channel')
    expect(() => worker.assertChannelEpoch(0)).not.toThrow(
      'another Inspector owner or attachment generation',
    )
    const firstMaterialized = new NativeConsoleMessage(owner, { ...info, sequence: 42 })
    expect(() => firstMaterialized.args()).toThrow(
      'another Inspector owner or attachment generation',
    )
    expect(f.runWorker).not.toHaveBeenCalled()
    f.close()
  })
  it('rejects a different owner without inventing earlier-channel chronology', () => {
    const actual = { ...actualWorker, control: { owner: 'current-source', generation: '2' } }
    const f = fixture([actual])
    f.emit({ event: 'worker', worker: actual, sequence: 1 })
    const worker = f.workers.fromObservedInfo(actual)
    const foreign = {
      handle: 2,
      guid: 'foreign-owner-control',
      node: false as const,
      preview: 'object',
      worker: actual.id,
      workerControl: { owner: 'other-source', generation: '2' },
    }
    expect(() => worker.adoptHandle(foreign)).toThrow(
      'another Inspector owner or attachment generation',
    )
    expect(() => worker.adoptHandle(foreign)).not.toThrow('earlier Inspector control channel')
    expect(f.runWorker).not.toHaveBeenCalled()
    f.close()
  })
  it('gives an actual later author-close receipt precedence over earlier channel loss', () => {
    const actual = { ...actualWorker, control: { owner: 'observed-source', generation: '1' } }
    const f = fixture([actual])
    f.emit({ event: 'worker', worker: actual, sequence: 14 })
    const worker = f.workers.fromObservedInfo(actual)
    const channelOnly =
      'Native worker Inspector control channel detached (control channel unavailable); author execution termination was not proven'
    f.inventory([])
    f.emit({ event: 'channelclosed', worker: actual, sequence: 17, reason: channelOnly })
    expect(worker.isClosed()).toBe(false)
    expect(() => worker.assertUsable()).toThrow(channelOnly)
    f.emit({ event: 'close', worker: actual, sequence: 18 })
    expect(worker.isClosed()).toBe(true)
    expect(() => worker.assertUsable()).toThrow('native worker closed')
    expect(() => worker.assertUsable()).not.toThrow('termination was not proven')
    expect(f.runWorker).not.toHaveBeenCalled()
    f.logicalAbort()
    expect(() => worker.assertUsable()).toThrow('This operation was aborted')
    f.close()
  })
  it('retains replacement-control detail on a physically late old-channel result', async () => {
    const first = { ...actualWorker, control: { owner: 'observed-source', generation: '1' } }
    const next = { ...actualWorker, control: { owner: 'observed-source', generation: '2' } }
    const f = fixture([first])
    f.emit({ event: 'worker', worker: first, sequence: 1 })
    const worker = f.workers.fromObservedInfo(first)
    let receipt: (value: UntrustedInput) => void = () => {
      throw new Error('No pending receipt')
    }
    f.runWorker.mockImplementationOnce(
      () =>
        new Promise<UntrustedInput>((resolve) => {
          receipt = resolve
        }),
    )
    const pending = worker.evaluateHandle('({ actual: true })')
    const rejected = expect(pending).rejects.toThrow(
      'another Inspector owner or attachment generation',
    )
    f.inventory([])
    f.emit({ event: 'channelclosed', worker: first, sequence: 2 })
    f.inventory([next])
    f.emit({ event: 'worker', worker: next, sequence: 3 })
    receipt({
      worker: first.id,
      handle: 3,
      guid: 'old-control-reply',
      preview: 'object',
      workerControl: first.control,
    })
    await rejected
    expect(f.runWorker).toHaveBeenCalledTimes(1)
    expect(() => worker.assertUsable()).not.toThrow()
    f.close()
  })
})

describe('retained canonical worker lifecycle ordering', () => {
  it('keeps retained receipt ordering after the bounded shared journal rolls over', () => {
    const f = fixture()
    f.emit({ event: 'worker', worker: actualWorker, sequence: 10 })
    const retained = request(f).serviceWorker()
    expect(retained).not.toBeNull()
    f.emit({ event: 'channelclosed', worker: actualWorker, sequence: 11 })
    f.emit({ event: 'worker', worker: actualWorker, sequence: 12 })
    for (let index = 0; index < 1025; index++) {
      const worker = { ...actualWorker, id: `history:${index}` }
      f.emit({ event: 'worker', worker, sequence: 13 + index * 2 })
      f.emit({ event: 'close', worker, sequence: 14 + index * 2 })
    }
    expect(f.workers.fromRecord({ event: 'close', worker: actualWorker, sequence: 10 })).toBe(
      retained,
    )
    expect(
      f.workers.fromRecord({ event: 'channelclosed', worker: actualWorker, sequence: 11 }),
    ).toBe(retained)
    expect(retained?.isClosed()).toBe(false)
    expect(() => retained?.assertUsable()).not.toThrow()
    expect(() =>
      f.workers.fromRecord({
        event: 'worker',
        worker: { ...actualWorker, id: 'unknown:old' },
        sequence: 1,
      }),
    ).toThrow('history capacity exceeded')
    f.close()
  })
})

describe('console worker metadata uses the same observed identity', () => {
  it('keeps worker access and adopted argument handles on one canonical object', () => {
    const f = fixture([actualWorker])
    const resolve = vi.fn((info: NativeWorkerInfo) => f.workers.fromConsole(info))
    const message = new NativeConsoleMessage(
      { source: f.source, workerFromConsole: resolve },
      {
        type: 'log',
        text: 'actual worker argument',
        worker: actualWorker.id,
        workerInfo: actualWorker,
        location: { url: actualWorker.url, lineNumber: 1, columnNumber: 1 },
        arguments: [
          {
            handle: 1,
            guid: 'actual-native-handle',
            preview: 'text',
            node: false,
            worker: actualWorker.id,
          },
        ],
      },
    )
    const worker = message.worker()
    expect(worker).toBe(f.workers.allTargets()[0])
    resolve.mockImplementation(() => {
      throw new Error('A second worker lookup would change ownership')
    })
    const handle = message.args()[0]
    expect(handle).toBeDefined()
    expect(message.args()[0]).toBe(handle)
    expect(resolve).toHaveBeenCalledTimes(1)
    f.close()
    expect(message.worker()).toBe(worker)
  })
  it('returns null without resolving a worker for actual non-worker console metadata', () => {
    const f = fixture()
    const resolve = vi.fn(() => {
      throw new Error('No worker target exists')
    })
    const message = new NativeConsoleMessage(
      { source: f.source, workerFromConsole: resolve },
      {
        type: 'log',
        text: 'page console',
        arguments: [],
        location: { url: 'https://native.test/', lineNumber: 1, columnNumber: 1 },
      },
    )
    expect(message.worker()).toBeNull()
    expect(message.args()).toEqual([])
    expect(resolve).not.toHaveBeenCalled()
    f.close()
  })
  it('preserves actual argument-budget failures before any target resolution', () => {
    const f = fixture()
    const resolve = vi.fn(() => {
      throw new Error('Target resolution must not replace native argument failure')
    })
    const message = new NativeConsoleMessage(
      { source: f.source, workerFromConsole: resolve },
      {
        type: 'log',
        text: 'native argument rejection',
        worker: actualWorker.id,
        workerInfo: actualWorker,
        argumentsError: 'Actual native console argument budget exceeded',
        location: { url: actualWorker.url, lineNumber: 1, columnNumber: 1 },
      },
    )
    expect(() => message.args()).toThrow('Actual native console argument budget exceeded')
    expect(resolve).not.toHaveBeenCalled()
    f.close()
  })
  it('rejects missing or mismatched actual worker identity instead of substituting a target', () => {
    const f = fixture()
    const owner = {
      source: f.source,
      workerFromConsole: (info: NativeWorkerInfo) => f.workers.fromConsole(info),
    }
    const info = {
      type: 'log',
      text: 'worker console',
      worker: actualWorker.id,
      location: { url: actualWorker.url, lineNumber: 1, columnNumber: 1 },
    }
    expect(() => new NativeConsoleMessage(owner, info).worker()).toThrow('identity is unavailable')
    expect(() =>
      new NativeConsoleMessage(owner, {
        ...info,
        workerInfo: { ...actualWorker, id: 'other-target' },
      }).worker(),
    ).toThrow('identity is inconsistent')
    f.close()
  })
})

describe('actual native Inspector owner and attachment generation', () => {
  const oldControl = { owner: 'actual-page-owner', generation: '9007199254740993' }
  const newControl = { owner: 'actual-page-owner', generation: '9007199254740994' }
  it('rejects first-materialized historical console handles without losing canonical target identity', async () => {
    const f = fixture()
    const before = { ...actualWorker, control: oldControl }
    const after = { ...actualWorker, control: newControl }
    f.emit({ event: 'worker', worker: before, sequence: 1 })
    const canonical = request(f).serviceWorker()
    if (!canonical) throw new Error('Actual worker unavailable')
    f.emit({ event: 'channelclosed', worker: before, sequence: 2 })
    f.emit({ event: 'worker', worker: after, sequence: 3 })
    const owner = {
      source: f.source,
      workerFromConsole: (info: NativeWorkerInfo) => f.workers.fromConsole(info),
    }
    const console = (info: NativeWorkerInfo) =>
      new NativeConsoleMessage(owner, {
        type: 'log',
        text: 'actual historical console argument',
        worker: info.id,
        workerInfo: info,
        location: { url: info.url, lineNumber: 1, columnNumber: 1 },
        arguments: [
          {
            handle: 1,
            guid: 'native-opaque-handle',
            preview: 'object',
            node: false,
            worker: info.id,
            workerControl: info.control,
          },
        ],
      })
    const historical = console(before)
    expect(historical.worker()).toBe(canonical)
    expect(() => historical.args()).toThrow('another Inspector owner or attachment generation')
    expect(request(f, 'late-old-request', before).serviceWorker()).toBe(canonical)
    expect(canonical.captureNativeControl()).toEqual(newControl)
    expect(() =>
      canonical.adoptHandle({
        handle: 2,
        guid: 'wrong-owner',
        preview: 'object',
        node: false,
        worker: actualWorker.id,
        workerControl: { ...newControl, owner: 'actual-broker-owner' },
      }),
    ).toThrow('another Inspector owner')
    expect(f.runWorker).not.toHaveBeenCalled()
    const fresh = console(after).args()[0]
    expect(fresh).toBeDefined()
    await fresh?.dispose()
    expect(f.runWorker).toHaveBeenCalledTimes(1)
    expect(f.runWorker.mock.calls[0]?.[0].workerControl).toEqual(newControl)
    f.close()
  })
  it('does not adopt an old control reply when actual ready proves a new native generation', async () => {
    const f = fixture()
    const before = { ...actualWorker, control: oldControl },
      after = { ...actualWorker, control: newControl }
    f.emit({ event: 'worker', worker: before, sequence: 1 })
    const worker = request(f).serviceWorker()
    if (!worker) throw new Error('Actual worker unavailable')
    let receipt: (value: UntrustedInput) => void = () => {
      throw new Error('No pending receipt')
    }
    f.runWorker.mockImplementationOnce(
      () =>
        new Promise<UntrustedInput>((resolve) => {
          receipt = resolve
        }),
    )
    const pending = worker.evaluateHandle('({ actual: true })')
    expect(f.runWorker.mock.calls[0]?.[0].workerControl).toEqual(oldControl)
    const rejected = expect(pending).rejects.toThrow(
      'another Inspector owner or attachment generation',
    )
    // A real newer ready is itself proof of its actual native owner/generation;
    // no SDK close or target-death event is inferred here.
    f.emit({ event: 'worker', worker: after, sequence: 2 })
    receipt({
      worker: actualWorker.id,
      workerControl: oldControl,
      handle: 1,
      guid: 'old-real-control-reply',
      preview: 'object',
      node: false,
    })
    await rejected
    expect(worker.isClosed()).toBe(false)
    f.close()
  })
  it('keeps uint64 native generations exact above JavaScript safe integer range', () => {
    expect(
      NativeWorkerControlSchema.parse({ owner: 'actual-owner', generation: '18446744073709551615' })
        .generation,
    ).toBe('18446744073709551615')
    for (const generation of ['0', '01', '18446744073709551616', '1e3', '-1', '１'])
      expect(
        NativeWorkerControlSchema.safeParse({ owner: 'actual-owner', generation }).success,
      ).toBe(false)
  })
})
