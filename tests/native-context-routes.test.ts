import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { type NativeContextRouteInfo } from '../src/native-context-route-protocol.js'
import { NativeContextRoutes } from '../src/native-context-routes.js'
import { NativeBrowserContexts } from '../src/native-context.js'
import {
  NativeNetwork,
  type NativeRouteHandler,
  type NativeNetworkTransport,
} from '../src/native-network.js'
import { type NativeNetworkInfo } from '../src/native-protocol.js'
import { type UntrustedInput } from '../src/untrusted.js'

const registration = (value: UntrustedInput) => {
  if (
    !value ||
    typeof value !== 'object' ||
    !('registrationId' in value) ||
    typeof value.registrationId !== 'string'
  )
    throw new Error('Missing native registration identity')
  return value.registrationId
}

describe('native context route handler ownership', () => {
  it('installs one handler before the actual ACK and consumes only genuine native exhaustion', async () => {
    let done: (value: UntrustedInput) => void = () => undefined
    const command = vi.fn(
      (params: Record<string, UntrustedInput>) =>
        new Promise<UntrustedInput>((resolve) => {
          done = resolve
          void params
        }),
    )
    const routes = new NativeContextRoutes(command),
      handler = vi.fn()
    const installing = routes.route('**/wire', handler, { times: 1 })
    const id = registration(command.mock.calls[0]![0])
    expect(command).toHaveBeenCalledExactlyOnceWith(
      expect.objectContaining({ action: 'register', times: 1, url: '**/wire' }),
    )
    expect(routes.resolve(id, false)).toBe(handler)
    expect(routes.resolve(id, false)).toBe(handler)
    expect(routes.resolve(id, true)).toBe(handler)
    expect(routes.resolve(id, false)).toBeUndefined()
    done(undefined)
    await installing
  })

  it('preserves a removed handler until genuine policy ACK and rolls failed installation back', async () => {
    const command = vi
      .fn<(params: Record<string, UntrustedInput>) => Promise<UntrustedInput>>()
      .mockResolvedValue(undefined)
    const routes = new NativeContextRoutes(command),
      first = vi.fn(),
      second = vi.fn()
    await routes.route('**/*', first)
    await routes.route('**/*', second)
    const firstId = registration(command.mock.calls[0]![0]),
      secondId = registration(command.mock.calls[1]![0])
    let acknowledge: (value: UntrustedInput) => void = () => undefined
    command.mockImplementationOnce(
      () =>
        new Promise((resolve) => {
          acknowledge = resolve
        }),
    )
    const removal = routes.unroute('**/*', first)
    expect(routes.resolve(firstId, false)).toBe(first)
    expect(command).toHaveBeenLastCalledWith({ action: 'remove', registrationIds: [firstId] })
    acknowledge(undefined)
    await removal
    expect(routes.resolve(firstId, false)).toBeUndefined()
    expect(routes.resolve(secondId, false)).toBe(second)
    command.mockRejectedValueOnce(new Error('Actual Fetch enable rejected'))
    await expect(routes.route('**/failure', first)).rejects.toThrow('Actual Fetch enable rejected')
    expect(routes.resolve(registration(command.mock.calls.at(-1)![0]), false)).toBeUndefined()
  })

  it('rejects unsupported matchers/options without issuing a native mutation', async () => {
    const command = vi.fn(() => Promise.resolve(undefined)),
      routes = new NativeContextRoutes(command)
    await expect(routes.route(42 as UntrustedInput as string, vi.fn())).rejects.toThrow(
      'URL glob, RegExp or synchronous URL predicate',
    )
    await expect(routes.route('**/*', vi.fn(), { times: 0 })).rejects.toThrow()
    await expect(
      routes.route('**/*', vi.fn(), { times: 2, behavior: 'wait' } as { times: number }),
    ).rejects.toThrow()
    expect(command).not.toHaveBeenCalled()
  })

  it('dispatches one canonical callback and sends every route operation through the genuine owner envelope', async () => {
    const controller = new AbortController(),
      contextCommand = vi
        .fn<(params: Record<string, UntrustedInput>) => Promise<UntrustedInput>>()
        .mockResolvedValue(undefined),
      routes = new NativeContextRoutes(contextCommand)
    const ownerCommand = vi
        .fn<(params: Record<string, UntrustedInput>) => Promise<UntrustedInput>>()
        .mockResolvedValue(undefined),
      ordinaryCommand = vi.fn(() => Promise.resolve(undefined)),
      lease = vi
        .fn<NonNullable<NativeNetworkTransport['routeCallback']>>()
        .mockResolvedValue(undefined)
    const handler: NativeRouteHandler = async (route) => {
      await route.fulfill({ body: Buffer.alloc(30_000, 7) })
    }
    await routes.route('**/wire', handler)
    const id = registration(contextCommand.mock.calls[0]![0])
    const network = new NativeNetwork(undefined, undefined, {
      signal: controller.signal,
      processSignal: controller.signal,
      defaultTimeout: () => 5000,
      command: ordinaryCommand,
      body: () => Promise.resolve(undefined),
      contextRoute: (key, exhausted) => routes.resolve(key, exhausted),
      routeCallback: lease,
    })
    const event: NativeNetworkInfo = {
      event: 'route',
      sequence: 8,
      request: {
        id: 'actual-request',
        requestId: 'actual-network-id',
        session: 'actual-child',
        frameId: '',
        url: 'https://native.test/wire',
        method: 'GET',
        resourceType: 'Fetch',
        headers: {},
        isNavigationRequest: false,
      },
      route: {
        id: 'actual-pause',
        registrationId: id,
        exhausted: true,
        callbackScope: '31',
        callbackToken: '48',
      },
    }
    network.receiveRoute(event, ownerCommand)
    network.receiveRoute(event, ownerCommand)
    await vi.waitFor(() => expect(lease).toHaveBeenCalledTimes(2))
    expect(ordinaryCommand).not.toHaveBeenCalled()
    expect(ownerCommand).toHaveBeenCalledWith(
      expect.objectContaining({ action: 'resetBody', routeId: 'actual-pause' }),
    )
    expect(ownerCommand).toHaveBeenCalledWith(
      expect.objectContaining({ action: 'bodyChunk', routeId: 'actual-pause' }),
    )
    expect(ownerCommand).toHaveBeenCalledWith(
      expect.objectContaining({ action: 'fulfill', routeId: 'actual-pause', bodyUpload: true }),
    )
    expect(lease.mock.calls.map((args) => args[0])).toEqual(['start', 'finish'])
  })
})

it('routes an actual early Page envelope before the SDK Page exists, without a Page READY command dependency', async () => {
  const signal = new AbortController().signal
  const routeListeners = new Set<(value: NativeContextRouteInfo) => void>()
  const runContext = vi
    .fn<
      (
        action: string,
        config?: Record<string, UntrustedInput>,
        signal?: AbortSignal,
      ) => Promise<UntrustedInput>
    >()
    .mockResolvedValue(undefined)
  const runAutomation = vi.fn(() => Promise.resolve(undefined))
  const sources = new Map<number, NativeBrowser>()
  const context = new NativeBrowserContexts({
    signal,
    runContext,
    onContextEvent: () => () => undefined,
    onContextRoute: (listener) => {
      routeListeners.add(listener)
      return () => routeListeners.delete(listener)
    },
    pageSource: (id) => {
      let source = sources.get(id)
      if (!source) {
        source = {
          signal,
          runAutomation,
          pageId: id,
        } as UntrustedInput as NativeBrowser
        sources.set(id, source)
      }
      return source
    },
  }).defaultContext()
  const callback = vi.fn<NativeRouteHandler>(async (route, request) => {
    expect(request).toBe(route.request())
    await route.continue()
  })
  await context.route('**/first', callback)
  const id = registration(runContext.mock.calls[0]![1])
  expect(context.pageById(7)).toBeNull()
  const record: NativeContextRouteInfo = {
    contextId: context.id,
    pageId: 7,
    owner: 'actual-owner-namespace',
    network: {
      event: 'route',
      sequence: 9,
      request: {
        id: 'first',
        requestId: 'native-first',
        session: '',
        frameId: '',
        url: 'https://native.test/first',
        method: 'GET',
        resourceType: 'Document',
        headers: {},
        isNavigationRequest: true,
      },
      route: { id: 'fetch-first', registrationId: id, callbackScope: '8', callbackToken: '44' },
    },
  }
  for (const listener of routeListeners) listener(record)
  await vi.waitFor(() => expect(callback).toHaveBeenCalledOnce())
  await vi.waitFor(() =>
    expect(runContext).toHaveBeenCalledWith(
      'route',
      expect.objectContaining({
        owner: record.owner,
        callbackScope: '8',
        routeId: 'fetch-first',
        action: 'continue',
        contextId: context.id,
      }),
      expect.any(AbortSignal),
    ),
  )
  expect(context.pageById(7)).not.toBeNull()
  expect(runAutomation).toHaveBeenCalledWith(
    'routeCallback',
    expect.objectContaining({ action: 'start', scope: '8', token: '44' }),
    signal,
  )
})
