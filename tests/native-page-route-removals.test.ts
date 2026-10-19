import { describe, expect, it, vi } from 'vitest'
import * as z from 'zod'

import { NativeContextRouteInvocations } from '../src/native-context-route-invocations.js'
import { NativeNetwork, type NativeRouteHandler } from '../src/native-network.js'
import {
  nativePageRouteCallbackIdentity,
  NativePageRouteMutationSchema,
  type NativePageRouteMutation,
} from '../src/native-page-route-removal-protocol.js'
import { NativePageRouteRemovals } from '../src/native-page-route-removals.js'
import { type NativeNetworkInfo } from '../src/native-protocol.js'
import { nativeRouteCallbackPhysicalCount } from '../src/native-route-callback.js'
import { nativeRouteHandlerInvoked } from '../src/native-route-handler-observations.js'
import { type UntrustedInput } from '../src/untrusted.js'

function deferred<T>() {
  let resolve: (value: T) => void = () => undefined
  let reject: (error: Error) => void = () => undefined
  const promise = new Promise<T>((done, fail) => {
    resolve = done
    reject = fail
  })
  return { promise, resolve, reject }
}
function fixture() {
  const book = new NativeContextRouteInvocations(),
    command = vi
      .fn<(params: Record<string, UntrustedInput>) => Promise<UntrustedInput>>()
      .mockResolvedValue(undefined),
    removed = vi.fn()
  const page = new NativePageRouteRemovals(book, command, removed)
  const publish = (
    phase: NativePageRouteMutation['phase'],
    tokens = ['2'],
    scope = '1',
    registrations = ['actual-page-policy'],
  ) => {
    const mutationId = z
      .object({ mutationId: z.string() })
      .parse(command.mock.calls.at(-1)?.[0]).mutationId
    page.receive({
      event: 'routemutation',
      sequence: 1,
      mutationId,
      phase,
      registrationIds: registrations,
      callbackScope: scope,
      callbackTokens: tokens,
    })
  }
  const observe = (scope: string, token: string, receipt: Promise<void>) => {
    const handler = book.resolve(nativePageRouteCallbackIdentity(scope, token), vi.fn())
    nativeRouteHandlerInvoked(handler, receipt)
  }
  return { book, page, command, removed, publish, observe }
}

describe('Page removal original callback identities', () => {
  it('waits through its actual host receipt without selecting a different Page scope or a future callback', async () => {
    const f = fixture(),
      old = deferred<void>(),
      other = deferred<void>(),
      future = deferred<void>(),
      control = deferred<UntrustedInput>()
    f.observe('1', '2', old.promise)
    f.observe('99', '2', other.promise)
    f.command.mockReturnValueOnce(control.promise)
    let completed = false
    const removal = f.page.remove('wait').then(() => {
      completed = true
    })
    f.publish('admitted')
    expect(f.removed).toHaveBeenCalledWith(new Set(['actual-page-policy']))
    f.observe('1', '3', future.promise)
    f.publish('applied')
    control.resolve(undefined)
    await Promise.resolve()
    expect(completed).toBe(false)
    old.resolve()
    await removal
    expect(completed).toBe(true)
    expect(f.book.pending()).toBe(2)
    other.resolve()
    future.resolve()
    await Promise.all([other.promise, future.promise])
    expect(f.book.pending()).toBe(0)
  })

  it('requires matching native scope, callback tokens and actual policy ACK before command success', async () => {
    const f = fixture(),
      control = deferred<UntrustedInput>()
    f.command.mockReturnValueOnce(control.promise)
    const removal = f.page.remove('wait')
    expect(() => f.publish('applied')).toThrow('actual admission')
    f.publish('admitted')
    expect(() => f.publish('applied', ['2'], '99')).toThrow('actual admission')
    expect(() => f.publish('applied', ['3'])).toThrow('actual admission')
    expect(() => f.publish('applied', ['2'], '1', ['future-policy'])).toThrow('actual admission')
    f.publish('applied')
    control.resolve(undefined)
    await removal
    await expect(f.page.remove('wait')).rejects.toThrow('without its actual admission')
  })

  it('preserves a real control error and actual admission state separately', async () => {
    const f = fixture(),
      before = new Error('actual pre-admission rejected')
    f.command.mockRejectedValueOnce(before)
    await expect(f.page.remove('ignoreErrors')).rejects.toBe(before)
    expect(f.removed).not.toHaveBeenCalled()
    const control = deferred<UntrustedInput>(),
      after = new Error('actual late control receipt failed')
    f.command.mockReturnValueOnce(control.promise)
    const removal = f.page.remove('ignoreErrors'),
      failed = expect(removal).rejects.toBe(after)
    f.publish('admitted')
    control.reject(after)
    await failed
    expect(f.removed).toHaveBeenCalledOnce()
  })

  it('removes canceled wait edges while the same original host receipt remains genuinely pending', async () => {
    const f = fixture(),
      held = deferred<void>()
    f.observe('1', '2', held.promise)
    const originalThen = vi.spyOn(held.promise, 'then')
    for (let index = 0; index < 1000; ++index) {
      const control = deferred<UntrustedInput>()
      f.command.mockReturnValueOnce(control.promise)
      const removal = f.page.remove('wait'),
        failed = expect(removal).rejects.toThrow('actual original deadline')
      f.publish('admitted')
      expect(f.book.waitObservationEdges()).toBe(1)
      control.reject(new Error('actual original deadline'))
      await failed
      expect(f.book.waitObservationEdges()).toBe(0)
    }
    expect(originalThen).not.toHaveBeenCalled()
    expect(f.book.pending()).toBe(1)
    held.resolve()
    await held.promise
    expect(f.book.pending()).toBe(0)
  })

  it('rejects delivery on logical Page close without settling a held author closure', async () => {
    const f = fixture(),
      held = deferred<void>(),
      control = deferred<UntrustedInput>()
    f.observe('1', '2', held.promise)
    f.command.mockReturnValueOnce(control.promise)
    const removal = f.page.remove('wait'),
      failed = expect(removal).rejects.toThrow('Page closed before removal observer')
    f.publish('admitted')
    f.publish('applied')
    control.resolve(undefined)
    await Promise.resolve()
    f.page.close()
    await failed
    expect(f.book.pending()).toBe(1)
    expect(f.book.waitObservationEdges()).toBe(0)
    await expect(f.page.remove('wait')).rejects.toThrow('owner is closed')
    held.resolve()
    await held.promise
    expect(f.book.pending()).toBe(0)
  })

  it('reserves host observer capacity before any native command or policy mutation', async () => {
    const f = fixture(),
      reservations = Array.from({ length: 64 }, () => f.book.reserveWait())
    await expect(f.page.remove('wait')).rejects.toThrow('capacity')
    expect(f.command).not.toHaveBeenCalled()
    expect(f.removed).not.toHaveBeenCalled()
    for (const reservation of reservations) reservation.cancel()
    const held = deferred<UntrustedInput>()
    f.command.mockReturnValueOnce(held.promise)
    const removal = f.page.remove('wait')
    f.publish('admitted', [])
    f.publish('applied', [])
    held.resolve(undefined)
    await removal
  })

  it('rejects rounded/overflow/duplicate identities without interpreting IDs as another owner', async () => {
    const f = fixture(),
      control = deferred<UntrustedInput>()
    f.command.mockReturnValueOnce(control.promise)
    const removal = f.page.remove('wait')
    expect(() => f.publish('admitted', ['2', '2'])).toThrow('duplicate')
    expect(() => f.publish('admitted', ['18446744073709551616'])).toThrow('uint64')
    expect(() => f.publish('admitted', ['0'])).toThrow()
    expect(NativePageRouteMutationSchema.safeParse({ callbackScope: 1 }).success).toBe(false)
    f.publish('admitted', [])
    f.publish('applied', [])
    control.resolve(undefined)
    await removal
  })
})

describe('actual Page network dispatch into removal observers', () => {
  it('keeps Context invocations outside the private Page wait book and retains host finish cleanup', async () => {
    const pageEntered = deferred<void>(),
      pageRelease = deferred<void>(),
      contextEntered = deferred<void>(),
      contextRelease = deferred<void>(),
      finish = deferred<void>(),
      control = deferred<UntrustedInput>()
    const signal = new AbortController().signal,
      contextBook = new NativeContextRouteInvocations()
    const command = vi.fn<(params: Record<string, UntrustedInput>) => Promise<UntrustedInput>>(
      (params) =>
        params.action === 'removeRoute' && params.behavior === 'wait'
          ? control.promise
          : Promise.resolve(undefined),
    )
    let receive: (event: NativeNetworkInfo) => void = () => {
      throw new Error('Actual native subscription unavailable')
    }
    const contextHandler: NativeRouteHandler = async (route) => {
      contextEntered.resolve()
      await contextRelease.promise
      await route.fulfill({ body: 'actual-context' })
    }
    const network = new NativeNetwork(undefined, undefined, {
      signal,
      processSignal: signal,
      defaultTimeout: () => 1000,
      command,
      body: () => Promise.resolve(undefined),
      subscribe: (listener) => {
        receive = listener
        return () => undefined
      },
      contextRoute: () => contextBook.resolve('actual-context-policy', contextHandler),
      routeCallback: (action, lease) =>
        action === 'finish' && lease.token === '1' ? finish.promise : Promise.resolve(),
    })
    await network.route('**/*', async (route) => {
      pageEntered.resolve()
      await pageRelease.promise
      await route.fulfill({ body: 'actual-page' })
    })
    const registration = z
      .object({ registrationId: z.string() })
      .parse(command.mock.calls[0]?.[0]).registrationId
    const routeEvent = (id: string, token: string, sequence: number): NativeNetworkInfo => ({
      event: 'route',
      sequence,
      request: {
        id: token,
        requestId: 'wire-' + token,
        session: '',
        frameId: '',
        url: 'https://native.test/actual',
        method: 'GET',
        resourceType: 'Fetch',
        headers: {},
        isNavigationRequest: false,
      },
      route: {
        id: 'pause-' + token,
        registrationId: id,
        callbackScope: '10',
        callbackToken: token,
      },
    })
    receive(routeEvent(registration, '1', 1))
    receive(routeEvent('actual-context-policy', '2', 2))
    await Promise.all([pageEntered.promise, contextEntered.promise])
    let completed = false
    const removal = network.unrouteAll({ behavior: 'wait' }).then(() => {
      completed = true
    })
    const mutationId = z
      .object({ mutationId: z.string() })
      .parse(command.mock.calls.at(-1)?.[0]).mutationId
    for (const phase of ['admitted', 'applied'] as const)
      receive({
        event: 'routemutation',
        sequence: 3,
        mutationId,
        phase,
        registrationIds: [registration],
        callbackScope: '10',
        callbackTokens: ['1'],
      })
    control.resolve(undefined)
    pageRelease.resolve()
    await Promise.resolve()
    expect(completed).toBe(false)
    expect(nativeRouteCallbackPhysicalCount(signal)).toBe(2)
    finish.resolve()
    await removal
    expect(completed).toBe(true)
    expect(nativeRouteCallbackPhysicalCount(signal)).toBe(1)
    expect(contextBook.pending()).toBe(1)
    contextRelease.resolve()
    await vi.waitFor(() => expect(contextBook.pending()).toBe(0))
    expect(nativeRouteCallbackPhysicalCount(signal)).toBe(0)
    expect(network.takeRouteErrors()).toEqual([])
  })
  it('authorizes only selected Page user errors and preserves a real automatic abort failure', async () => {
    for (const abortFails of [false, true]) {
      const entered = deferred<void>(),
        release = deferred<void>(),
        signal = new AbortController().signal,
        actualFailure = new Error('actual primary abort receipt failed'),
        recoveryFailure = new Error('actual recovery abort receipt failed')
      let abortAttempt = 0
      let receive: (event: NativeNetworkInfo) => void = () => undefined
      const command = vi.fn<(params: Record<string, UntrustedInput>) => Promise<UntrustedInput>>(
        (params) => {
          if (params.action !== 'abort' || !abortFails) return Promise.resolve(undefined)
          return Promise.reject(++abortAttempt === 1 ? actualFailure : recoveryFailure)
        },
      )
      const network = new NativeNetwork(undefined, undefined, {
        signal,
        processSignal: signal,
        defaultTimeout: () => 1000,
        command,
        body: () => Promise.resolve(undefined),
        subscribe: (listener) => {
          receive = listener
          return () => undefined
        },
        routeCallback: () => Promise.resolve(),
      })
      await network.route('**/*', async () => {
        entered.resolve()
        await release.promise
        throw new Error('selected Page author exception')
      })
      const registration = z
        .object({ registrationId: z.string() })
        .parse(command.mock.calls[0]?.[0]).registrationId
      receive({
        event: 'route',
        sequence: 1,
        request: {
          id: 'actual-request',
          requestId: 'wire',
          session: '',
          frameId: '',
          url: 'https://native.test/actual',
          method: 'GET',
          resourceType: 'Fetch',
          headers: {},
          isNavigationRequest: false,
        },
        route: {
          id: 'actual-pause',
          registrationId: registration,
          callbackScope: '10',
          callbackToken: '1',
        },
      })
      await entered.promise
      const removal = network.unrouteAll({ behavior: 'ignoreErrors' })
      const mutationId = z
        .object({ mutationId: z.string() })
        .parse(command.mock.calls.at(-1)?.[0]).mutationId
      for (const phase of ['admitted', 'applied'] as const)
        receive({
          event: 'routemutation',
          sequence: 2,
          mutationId,
          phase,
          registrationIds: [registration],
          callbackScope: '10',
          callbackTokens: ['1'],
        })
      await removal
      release.resolve()
      await vi.waitFor(() => expect(nativeRouteCallbackPhysicalCount(signal)).toBe(0))
      const aborts = command.mock.calls
        .filter(([params]) => params.action === 'abort')
        .map(([params]) => params.routeId)
      // Existing shared recovery retries the same still-unhandled pause after
      // a failed primary abort. This is not a second author callback owner.
      expect(aborts).toEqual(abortFails ? ['actual-pause', 'actual-pause'] : ['actual-pause'])
      expect(network.takeRouteErrors()).toEqual(abortFails ? [actualFailure, recoveryFailure] : [])
    }
  })
})
