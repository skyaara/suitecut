// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { NativeContextRouteInvocations } from '../src/native-context-route-invocations.js'
import { NativeNetwork } from '../src/native-network.js'
import { type NativeNetworkInfo } from '../src/native-protocol.js'
import { NativeRouteURLMatcherPolicy } from '../src/native-route-url-matcher.js'
import { type UntrustedInput } from '../src/untrusted.js'

function deferred() {
  let resolve: () => void = () => undefined
  const promise = new Promise<void>((done) => {
    resolve = done
  })
  return { promise, resolve }
}
function fixture() {
  const controller = new AbortController(),
    command = vi
      .fn<(config: Record<string, UntrustedInput>) => Promise<UntrustedInput>>()
      .mockResolvedValue({}),
    lease = vi.fn<(action: 'start' | 'finish') => Promise<void>>().mockResolvedValue(undefined),
    context = new Map<string, NativeRouteURLMatcherPolicy>(),
    invocations = new NativeContextRouteInvocations()
  let receive: ((event: NativeNetworkInfo) => void) | undefined
  const network = new NativeNetwork(undefined, undefined, {
    signal: controller.signal,
    processSignal: new AbortController().signal,
    defaultTimeout: () => 1000,
    command,
    body: () => Promise.resolve(undefined),
    routeCallback: lease,
    subscribe: (listener) => {
      receive = listener
      return () => {
        receive = undefined
      }
    },
    contextMatcher: (id) => {
      const policy = context.get(id)
      return policy
        ? {
            callback: policy.callback(),
            observe: (callback) => invocations.resolveCallback(id, callback),
          }
        : undefined
    },
  })
  return {
    network,
    command,
    lease,
    controller,
    context,
    invocations,
    emit: (event: NativeNetworkInfo) => receive?.(event),
  }
}
function match(registrationId: string, sequence = 1): NativeNetworkInfo {
  return {
    event: 'routematch',
    sequence,
    matcher: {
      id: 'match:1',
      registrationId,
      url: 'https://native.test/author',
      callbackScope: '3',
      callbackToken: String(sequence),
    },
  }
}

describe('native URL matcher owner adapter', () => {
  it('executes the actual candidate only after start and returns bool only after finish', async () => {
    const f = fixture(),
      finish = deferred(),
      predicate = vi.fn((url: URL) => url.pathname === '/author')
    await f.network.route(predicate, () => undefined)
    const registration = f.command.mock.calls[0]?.[0].registrationId
    if (typeof registration !== 'string') throw new Error('Actual registration ID absent')
    f.command.mockClear()
    f.lease.mockImplementation((action) =>
      action === 'finish' ? finish.promise : Promise.resolve(),
    )
    f.emit(match(registration))
    await vi.waitFor(() => expect(predicate).toHaveBeenCalledOnce())
    expect(f.command).not.toHaveBeenCalled()
    finish.resolve()
    await vi.waitFor(() =>
      expect(f.command.mock.calls[0]?.[0]).toMatchObject({
        action: 'matchRoute',
        matched: true,
        callbackScope: '3',
      }),
    )
    await f.network.unroute(predicate)
  })

  it('deduplicates the genuine context envelope and optional Page projection', async () => {
    const f = fixture(),
      predicate = vi.fn(() => false),
      policy = new NativeRouteURLMatcherPolicy(predicate)
    f.context.set('actual-context-registration', policy)
    const event = match('actual-context-registration')
    f.network.receiveRoute(event, f.command)
    f.emit(event)
    await vi.waitFor(() => expect(f.command).toHaveBeenCalledOnce())
    expect(predicate).toHaveBeenCalledOnce()
    expect(f.command).toHaveBeenCalledWith(expect.objectContaining({ matched: false }))
    expect(f.invocations.pending()).toBe(0)
    policy.dispose()
  })

  it('does not fabricate Request or Route events for a matcher control packet', async () => {
    const f = fixture(),
      request = vi.fn(),
      response = vi.fn()
    f.network.on('request', request)
    f.network.on('response', response)
    const policy = new NativeRouteURLMatcherPolicy(/native\.test/g)
    f.context.set('regexp', policy)
    f.emit(match('regexp'))
    await vi.waitFor(() => expect(f.command).toHaveBeenCalledOnce())
    expect(request).not.toHaveBeenCalled()
    expect(response).not.toHaveBeenCalled()
    policy.dispose()
  })

  it('reports unavailable host state as invalid, never an invented false match', async () => {
    const f = fixture()
    f.emit(match('removed-registration'))
    await vi.waitFor(() => expect(f.command).toHaveBeenCalledOnce())
    expect(f.lease.mock.calls.map(([action]) => action)).toEqual(['finish'])
    expect(f.command.mock.calls[0]?.[0]).toMatchObject({ invalid: true })
    expect(f.command.mock.calls[0]?.[0]).not.toHaveProperty('matched')
    await vi.waitFor(() => expect(f.network.takeRouteErrors()[0]?.message).toMatch(/unavailable/))
  })

  it('suppresses missing registration only when the native owner attests original generation removal', async () => {
    const f = fixture()
    f.command.mockResolvedValue({ skipped: true })
    f.emit(match('actually-removed-registration'))
    await vi.waitFor(() => expect(f.command).toHaveBeenCalledOnce())
    await vi.waitFor(() => expect(f.lease).toHaveBeenCalledOnce())
    expect(f.command.mock.calls[0]?.[0]).toMatchObject({ invalid: true })
    expect(f.network.takeRouteErrors()).toEqual([])
  })

  it('retains an invalid actual async continuation through cancellation and physical finish', async () => {
    const f = fixture(),
      continuation = deferred()
    // @ts-expect-error Actual untyped authors may return an unsupported Promise.
    const policy = new NativeRouteURLMatcherPolicy(() => continuation.promise)
    f.context.set('async-invalid', policy)
    f.emit(match('async-invalid'))
    await vi.waitFor(() => expect(f.command.mock.calls[0]?.[0]).toMatchObject({ invalid: true }))
    expect(f.invocations.pending()).toBe(1)
    f.controller.abort(new Error('actual logical owner canceled'))
    expect(f.invocations.pending()).toBe(1)
    continuation.resolve()
    await vi.waitFor(() => expect(f.invocations.pending()).toBe(0))
    expect(f.command).toHaveBeenCalledOnce()
    expect(f.lease.mock.calls.map(([action]) => action)).toEqual(['start', 'finish'])
    policy.dispose()
  })

  it('does not deliver a boolean after logical cancellation while the real finish ACK is held', async () => {
    const f = fixture(),
      finish = deferred(),
      policy = new NativeRouteURLMatcherPolicy(() => true)
    f.context.set('held', policy)
    f.lease.mockImplementation((action) =>
      action === 'finish' ? finish.promise : Promise.resolve(),
    )
    f.emit(match('held'))
    await vi.waitFor(() => expect(f.lease).toHaveBeenCalledTimes(2))
    f.controller.abort(new Error('logical cancellation'))
    expect(f.invocations.pending()).toBe(1)
    finish.resolve()
    await vi.waitFor(() => expect(f.invocations.pending()).toBe(0))
    expect(f.command).not.toHaveBeenCalled()
    policy.dispose()
  })

  it('never suppresses actual start or finish command rejection under ignoreErrors', async () => {
    for (const failedAction of ['start', 'finish']) {
      const f = fixture(),
        actual = new Error('Actual native ' + failedAction + ' receipt failed'),
        predicate = vi.fn(() => true),
        policy = new NativeRouteURLMatcherPolicy(predicate)
      f.context.set('selected', policy)
      f.invocations.ignore(new Set(['selected']), Promise.resolve(true))
      f.lease.mockImplementation((action) =>
        action === failedAction ? Promise.reject(actual) : Promise.resolve(),
      )
      f.emit(match('selected'))
      await vi.waitFor(() => expect(f.invocations.pending()).toBe(0))
      expect(f.network.takeRouteErrors()).toEqual([actual])
      expect(f.command).not.toHaveBeenCalled()
      expect(predicate).toHaveBeenCalledTimes(failedAction === 'start' ? 0 : 1)
      policy.dispose()
    }
  })
})
