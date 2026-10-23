// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { NativeContextRouteInvocations } from '../src/native-context-route-invocations.js'
import { NativeNetwork, type NativeRouteHandler } from '../src/native-network.js'
import { type NativeNetworkInfo } from '../src/native-protocol.js'
import { nativeRouteHandlerInvoked } from '../src/native-route-handler-observations.js'
import { type UntrustedInput } from '../src/untrusted.js'

function deferred<T>() {
  let resolve: (value: T) => void = () => undefined
  const promise = new Promise<T>((done) => {
    resolve = done
  })
  return { promise, resolve }
}
function event(registration = 'selected'): NativeNetworkInfo {
  return {
    event: 'route',
    sequence: registration === 'selected' ? 1 : 2,
    request: {
      id: registration,
      requestId: registration,
      session: '',
      frameId: '',
      url: 'https://native.test/request',
      method: 'GET',
      resourceType: 'Fetch',
      headers: {},
      isNavigationRequest: false,
    },
    route: {
      id: registration,
      registrationId: registration,
      exhausted: true,
      callbackScope: '1',
      callbackToken: registration === 'selected' ? '1' : '2',
    },
  }
}
function fixture(
  handler: NativeRouteHandler,
  command = vi
    .fn<(config: Record<string, UntrustedInput>) => Promise<UntrustedInput>>()
    .mockResolvedValue(undefined),
) {
  const book = new NativeContextRouteInvocations(),
    signal = new AbortController().signal
  const lease = vi.fn<(action: 'start' | 'finish') => Promise<void>>().mockResolvedValue(undefined)
  const network = new NativeNetwork(undefined, undefined, {
    signal,
    processSignal: signal,
    defaultTimeout: () => 1000,
    command,
    body: () => Promise.resolve(undefined),
    contextRoute: (registration) => book.resolve(registration, handler),
    routeCallback: lease,
  })
  return { book, network, lease, command }
}

describe('context route invocation receipts and exception policy', () => {
  it('retains the actual invocation until the real finish ACK, including failed start', async () => {
    const finish = deferred<void>(),
      entered = deferred<void>()
    const f = fixture(() => {
      entered.resolve()
    })
    f.lease.mockImplementation((action) =>
      action === 'finish' ? finish.promise : Promise.resolve(),
    )
    f.network.receiveRoute(event(), f.command)
    await entered.promise
    expect(f.book.pending()).toBe(1)
    finish.resolve()
    await vi.waitFor(() => expect(f.book.pending()).toBe(0))
    const failed = new Error('actual start receipt rejected'),
      never = vi.fn()
    const second = fixture(never)
    second.lease.mockImplementation((action) =>
      action === 'start' ? Promise.reject(failed) : Promise.resolve(),
    )
    second.network.receiveRoute(event(), second.command)
    await vi.waitFor(() => expect(second.book.pending()).toBe(0))
    expect(never).not.toHaveBeenCalled()
    expect(second.network.takeRouteErrors()).toEqual([failed])
  })

  it('waits for actual removal outcome before suppressing a selected exhausted handler error', async () => {
    const entered = deferred<void>(),
      release = deferred<void>(),
      applied = deferred<boolean>()
    const f = fixture(async () => {
      entered.resolve()
      await release.promise
      throw new Error('selected user failure')
    })
    f.network.receiveRoute(event(), f.command)
    await entered.promise
    const admit = f.book.armIgnore(applied.promise)
    release.resolve()
    await Promise.resolve()
    expect(f.book.pending()).toBe(1)
    expect(f.lease.mock.calls.map(([action]) => action)).toEqual(['start'])
    admit(new Set(['selected']))
    applied.resolve(true)
    await vi.waitFor(() => expect(f.book.pending()).toBe(0))
    expect(f.network.takeRouteErrors()).toEqual([])
    expect(f.command).toHaveBeenCalledWith(
      expect.objectContaining({ action: 'abort', internal: true }),
    )
  })

  it('preserves original exceptions when removal fails and selects by registration rather than callback identity', async () => {
    const entered = deferred<void>(),
      release = deferred<void>(),
      outcome = deferred<boolean>()
    const failure = new Error('original user exception'),
      handler = vi.fn(async () => {
        entered.resolve()
        await release.promise
        throw failure
      })
    const f = fixture(handler)
    f.network.receiveRoute(event(), f.command)
    f.network.receiveRoute(event('unrelated'), f.command)
    await entered.promise
    f.book.ignore(new Set(['selected']), outcome.promise)
    outcome.resolve(false)
    release.resolve()
    await vi.waitFor(() => expect(f.book.pending()).toBe(0))
    expect(handler).toHaveBeenCalledTimes(2)
    expect(f.network.takeRouteErrors()).toEqual([failure, failure])
  })

  it('suppresses only the actual selected registration when the same callback serves another route', async () => {
    const release = deferred<void>(),
      outcome = deferred<boolean>(),
      failure = new Error('shared user function failure')
    const handler = vi.fn(async () => {
      await release.promise
      throw failure
    })
    const f = fixture(handler)
    f.network.receiveRoute(event(), f.command)
    f.network.receiveRoute(event('unrelated'), f.command)
    f.book.ignore(new Set(['selected']), outcome.promise)
    release.resolve()
    await vi.waitFor(() => expect(f.book.pending()).toBe(1))
    expect(f.network.takeRouteErrors()).toEqual([failure])
    outcome.resolve(true)
    await vi.waitFor(() => expect(f.book.pending()).toBe(0))
    expect(f.network.takeRouteErrors()).toEqual([])
    expect(handler).toHaveBeenCalledTimes(2)
  })

  it('does not retain completed invocation edges while an actual removal admission remains pending', async () => {
    const book = new NativeContextRouteInvocations(),
      held = deferred<boolean>()
    const admit = book.armIgnore(held.promise)
    for (let index = 0; index < 1000; ++index) {
      const wrapped = book.resolve('selected', vi.fn()),
        actualReceipt = Promise.resolve()
      expect(book.observationEdges()).toBe(1)
      nativeRouteHandlerInvoked(wrapped, actualReceipt)
      await actualReceipt
      expect(book.pending()).toBe(0)
      expect(book.observationEdges()).toBe(0)
    }
    admit(new Set(['selected']))
    held.resolve(true)
    await held.promise
    expect(book.pending()).toBe(0)
    expect(book.observationEdges()).toBe(0)
  })

  it('removes canceled wait edges without adding reactions to a held actual receipt', async () => {
    const book = new NativeContextRouteInvocations(),
      held = deferred<void>()
    const wrapped = book.resolve('selected', vi.fn())
    nativeRouteHandlerInvoked(wrapped, held.promise)
    const originalThen = vi.spyOn(held.promise, 'then')
    for (let index = 0; index < 1000; ++index) {
      const wait = book.snapshot(new Set(['selected']))
      expect(book.waitObservationEdges()).toBe(1)
      wait.cancel()
      await wait.settled
      expect(book.waitObservationEdges()).toBe(0)
    }
    expect(originalThen).not.toHaveBeenCalled()
    const maximum = Array.from({ length: 64 }, () => book.snapshot(new Set(['selected'])))
    expect(() => book.snapshot(new Set(['selected']))).toThrow('capacity')
    expect(book.waitObservationEdges()).toBe(64)
    for (const waiting of maximum) waiting.cancel()
    expect(book.waitObservationEdges()).toBe(0)
    const final = book.snapshot(new Set(['selected']))
    let completed = false
    void final.settled.then(() => {
      completed = true
    })
    await Promise.resolve()
    expect(completed).toBe(false)
    held.resolve()
    await final.settled
    expect(book.pending()).toBe(0)
    expect(book.waitObservationEdges()).toBe(0)
  })

  it('keeps host wait pending through genuine finish ACK and excludes future invocations', async () => {
    const finish = deferred<void>(),
      entered = deferred<void>()
    const f = fixture(() => {
      entered.resolve()
    })
    f.lease.mockImplementation((action) =>
      action === 'finish' ? finish.promise : Promise.resolve(),
    )
    f.network.receiveRoute(event(), f.command)
    await entered.promise
    const wait = f.book.snapshot(new Set(['selected']))
    let completed = false
    void wait.settled.then(() => {
      completed = true
    })
    const later = f.book.resolve('selected', vi.fn()),
      laterReceipt = deferred<void>()
    nativeRouteHandlerInvoked(later, laterReceipt.promise)
    await Promise.resolve()
    expect(completed).toBe(false)
    finish.resolve()
    await wait.settled
    expect(f.book.pending()).toBe(1)
    laterReceipt.resolve()
    await laterReceipt.promise
    expect(f.book.pending()).toBe(0)
  })

  it('releases unrelated exceptions at real admission and preserves synchronous metadata channel errors', async () => {
    const release = deferred<void>(),
      applied = deferred<boolean>(),
      failure = new Error('unrelated user error')
    const f = fixture(async () => {
      await release.promise
      throw failure
    })
    f.network.receiveRoute(event('unrelated'), f.command)
    const admit = f.book.armIgnore(applied.promise)
    release.resolve()
    await Promise.resolve()
    admit(new Set(['selected']))
    await vi.waitFor(() => expect(f.book.pending()).toBe(0))
    expect(f.network.takeRouteErrors()).toEqual([failure])
    applied.resolve(true)

    const entered = deferred<void>(),
      run = deferred<void>(),
      decision = deferred<boolean>(),
      channel = new Error('actual synchronous channel closed')
    const command = vi.fn<(config: Record<string, UntrustedInput>) => Promise<UntrustedInput>>(
      (params) => {
        if (params.action === 'requestHeaders') throw channel
        return Promise.resolve(undefined)
      },
    )
    const second = fixture(async (_route, request) => {
      entered.resolve()
      await run.promise
      await request.allHeaders()
    }, command)
    second.network.receiveRoute(event(), command)
    await entered.promise
    second.book.ignore(new Set(['selected']), decision.promise)
    decision.resolve(true)
    run.resolve()
    await vi.waitFor(() => expect(second.book.pending()).toBe(0))
    expect(second.network.takeRouteErrors()).toEqual([channel])
  })

  it('does not suppress genuine command rejection after a successfully selected policy removal', async () => {
    const entered = deferred<void>(),
      release = deferred<void>(),
      applied = deferred<boolean>()
    const failure = new Error('actual fulfillment ACK rejected')
    const command = vi.fn<(config: Record<string, UntrustedInput>) => Promise<UntrustedInput>>(
      (params) => {
        if (params.action === 'fulfill') return Promise.reject(failure)
        return Promise.resolve(undefined)
      },
    )
    const f = fixture(async (route) => {
      entered.resolve()
      await release.promise
      await route.fulfill({ body: 'actual body' })
    }, command)
    f.network.receiveRoute(event(), command)
    await entered.promise
    f.book.ignore(new Set(['selected']), applied.promise)
    applied.resolve(true)
    release.resolve()
    await vi.waitFor(() => expect(f.book.pending()).toBe(0))
    expect(f.network.takeRouteErrors()).toEqual([failure])
  })
})
