// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'

import * as z from 'zod'

import { NativeContextRouteInvocations } from './native-context-route-invocations.js'
import {
  invokeNativeRouteCallback,
  type NativeRouteCallbackLease,
} from './native-route-callback.js'
import {
  nativeRouteCommandFailed,
  nativeRouteHandlerInvoked,
} from './native-route-handler-observations.js'
import { invokeNativeRouteURLMatcher } from './native-route-url-matcher-callback.js'
import {
  NativeRouteURLMatcherPolicy,
  type NativeRouteURLMatcher,
} from './native-route-url-matcher.js'
import { runNativeWebSocketPayload } from './native-websocket-payload.js'
import {
  NativeWebSocketRouteInfoSchema,
  type NativeWebSocketRouteInfo,
} from './native-websocket-route-protocol.js'
import { NativeRoutedWebSocket, NativeWebSocketRouteMessage } from './native-websocket-routing.js'
import { toError, type UntrustedInput } from './untrusted.js'

export type NativeWebSocketRouteHandler = (socket: NativeRoutedWebSocket) => void | Promise<void>
export type NativeWebSocketRouteURLMatcher = NativeRouteURLMatcher
export interface NativeWebSocketRouteRemovalOptions {
  behavior?: 'default' | 'wait' | 'ignoreErrors'
}
interface Transport {
  signal: AbortSignal
  processSignal: AbortSignal
  baseURL?(): string | undefined
  run(params: Record<string, UntrustedInput>): Promise<UntrustedInput>
  cleanup?(params: Record<string, UntrustedInput>): Promise<UntrustedInput>
  callback(action: 'start' | 'finish', lease: NativeRouteCallbackLease): Promise<void>
  subscribe(listener: (info: NativeWebSocketRouteInfo) => void): () => void
}
interface Registration {
  policy: NativeRouteURLMatcherPolicy
  handler: NativeWebSocketRouteHandler
  active: boolean
}
interface Socket {
  socket: NativeRoutedWebSocket
  registration: string
}
const RemovalOptions = z.strictObject({
  behavior: z.enum(['default', 'wait', 'ignoreErrors']).optional(),
})

/** Native policy selects a target/candidate before any authored matcher/handler. */
export class NativeWebSocketRoutes {
  private readonly registrations = new Map<string, Registration>()
  private readonly sockets = new Map<string, Socket>()
  private readonly chains = new Map<string, Promise<void>>()
  private readonly invocations = new NativeContextRouteInvocations()
  private readonly failures: Error[] = []
  private readonly detach: () => void
  private closed = false
  constructor(private readonly transport: Transport) {
    this.detach = transport.subscribe((info) => this.receive(info))
    transport.signal.addEventListener('abort', this.close, { once: true })
    if (transport.signal.aborted) this.close()
  }
  async route(url: NativeRouteURLMatcher, handler: NativeWebSocketRouteHandler): Promise<void> {
    this.assertOpen()
    if (typeof handler !== 'function') throw new Error('Native WebSocket route requires a handler')
    if (this.registrations.size >= 128)
      throw new Error('Native WebSocket route registration capacity exceeded')
    const id = randomUUID(),
      entry = { policy: new NativeRouteURLMatcherPolicy(url), handler, active: true }
    this.registrations.set(id, entry)
    try {
      const baseURL = this.transport.baseURL?.()
      await this.command({
        action: 'routeRegister',
        registrationId: id,
        url: entry.policy.nativePattern,
        hostMatcher: entry.policy.host,
        ...(baseURL === undefined ? {} : { baseURL }),
      })
    } catch (error) {
      this.drop(id)
      throw error
    }
  }
  async unroute(url: NativeRouteURLMatcher, handler?: NativeWebSocketRouteHandler): Promise<void> {
    this.assertOpen()
    if (handler !== undefined && typeof handler !== 'function')
      throw new Error('Native WebSocket unroute requires a handler')
    const ids = [...this.registrations]
      .filter(
        ([, entry]) =>
          entry.policy.matchesRegistration(url) &&
          (handler === undefined || handler === entry.handler),
      )
      .map(([id]) => id)
    if (ids.length) await this.remove(ids, 'default')
  }
  async unrouteAll(options: NativeWebSocketRouteRemovalOptions = {}): Promise<void> {
    this.assertOpen()
    const { behavior = 'default' } = RemovalOptions.parse(options)
    await this.remove([...this.registrations.keys()], behavior)
  }
  errors(): Error[] {
    return this.failures.splice(0)
  }
  readonly close = (): void => {
    if (this.closed) return
    this.closed = true
    this.transport.signal.removeEventListener('abort', this.close)
    this.detach()
    const registrationIds = [...this.registrations.keys()]
    if (registrationIds.length && this.transport.cleanup && !this.transport.processSignal.aborted)
      void this.transport
        .cleanup({ action: 'routeUnregister', registrationIds, behavior: 'default' })
        .catch((error) => this.record(error as UntrustedInput))
    for (const id of this.registrations.keys()) this.drop(id)
    for (const entry of this.sockets.values()) entry.socket.detach()
    this.sockets.clear()
    // Actual running callback promises/leases survive scope closure.
  }
  private async remove(
    ids: string[],
    behavior: 'default' | 'wait' | 'ignoreErrors',
  ): Promise<void> {
    if (!ids.length) return
    const selected = new Set(ids),
      wait = behavior === 'wait' ? this.invocations.reserveWait() : undefined
    let settle: (applied: boolean) => void = () => undefined
    const outcome = new Promise<boolean>((resolve) => {
      settle = resolve
    })
    if (behavior === 'ignoreErrors') this.invocations.ignore(selected, outcome)
    try {
      await this.command({ action: 'routeUnregister', registrationIds: ids, behavior })
      for (const id of ids) this.drop(id)
      for (const [key, value] of this.sockets)
        if (selected.has(value.registration)) {
          value.socket.detach()
          this.sockets.delete(key)
        }
      settle(true)
      if (wait) {
        wait.capture(selected)
        await wait.settled
      }
    } catch (error) {
      settle(false)
      throw error
    } finally {
      wait?.cancel()
    }
  }
  private drop(id: string): void {
    const entry = this.registrations.get(id)
    if (entry) {
      entry.active = false
      entry.policy.dispose()
      this.registrations.delete(id)
    }
  }
  private receive(value: NativeWebSocketRouteInfo): void {
    const info = NativeWebSocketRouteInfoSchema.parse(value),
      entry = this.registrations.get(info.registrationId)
    if (!entry || this.closed) return
    const key = JSON.stringify([info.targetId, info.routingToken, info.requestId])
    const matcher =
      info.stage === 'match' && entry.policy.host ? entry.policy.callback() : undefined
    const invoke = this.invocations.resolveCallback(info.registrationId, async () => {
      const callbackTransport = {
        signal: this.transport.signal,
        processSignal: this.transport.processSignal,
        start: (lease: NativeRouteCallbackLease) => this.acknowledge('start', lease),
        finish: (lease: NativeRouteCallbackLease) => this.acknowledge('finish', lease),
      }
      if (!entry.active) {
        if (!this.transport.processSignal.aborted) await this.acknowledge('finish', info.callback)
        return
      }
      if (info.stage === 'match') {
        if (!matcher) {
          await this.acknowledge('finish', info.callback)
          throw new Error('Native WebSocket host matcher registration is unavailable')
        }
        await invokeNativeRouteURLMatcher(
          callbackTransport,
          info.callback,
          matcher,
          info.url,
          (matched) =>
            this.socketCommand(
              info,
              'match',
              matched === undefined ? { invalid: true } : { matched },
            ),
          (error) => this.record(error),
        )
        return
      }
      await invokeNativeRouteCallback(callbackTransport, info.callback, async () => {
        if (info.stage === 'created') {
          if (this.sockets.has(key) || this.sockets.size >= 1024)
            throw new Error('Native routed WebSocket socket ownership capacity exceeded')
          const socket = new NativeRoutedWebSocket(
            { command: (_id, operation, fields) => this.socketCommand(info, operation, fields) },
            info.requestId,
            info.url,
            true,
          )
          this.sockets.set(key, { socket, registration: info.registrationId })
          try {
            await entry.handler(socket)
            if (!socket.isDetached() && !socket.isClosed()) await socket.completeInitial()
            if (socket.isDetached()) this.sockets.delete(key)
          } catch (error) {
            if (!socket.isDetached() && !socket.isClosed())
              await socket.abort().catch((failure) => this.record(failure as UntrustedInput))
            throw error
          }
          return
        }
        const owned = this.sockets.get(key)
        if (owned?.registration !== info.registrationId)
          throw new Error('Native WebSocket route event has no actual owned socket')
        if (info.stage === 'closed') {
          owned.socket.retire()
          this.sockets.delete(key)
          await owned.socket.receiveClosed()
          return
        }
        if ((info.opcode !== 1 && info.opcode !== 2) || !info.messageId)
          throw new Error('Invalid native WebSocket held-message identity')
        const bytes = info.payload
        const message = new NativeWebSocketRouteMessage(
          owned.socket,
          info.messageId,
          info.outgoing,
          info.opcode,
          bytes,
        )
        try {
          await owned.socket.receiveMessage(message)
        } catch (error) {
          if (!message.isHandled())
            await message.drop().catch((failure) => this.record(failure as UntrustedInput))
          throw error
        }
      })
    })
    const physical = (this.chains.get(key) ?? Promise.resolve()).then(invoke).then(() => undefined)
    nativeRouteHandlerInvoked(invoke, physical)
    const observed = physical.catch((error) => this.record(error as UntrustedInput))
    this.chains.set(key, observed)
    void observed.then(() => {
      if (this.chains.get(key) === observed) this.chains.delete(key)
    })
  }
  private async socketCommand(
    info: NativeWebSocketRouteInfo,
    operation: string,
    fields: Record<string, UntrustedInput> = {},
  ): Promise<void> {
    await runNativeWebSocketPayload(
      (params) => this.command(params),
      {
        action: 'routePolicyCommand',
        registrationId: info.registrationId,
        targetId: info.targetId,
        routingToken: info.routingToken,
        requestId: info.requestId,
        operation,
        messageId: '',
        opcode: 0,
        payloadBase64: '',
        code: 0,
        reason: '',
        ...fields,
      },
      this.transport.cleanup?.bind(this.transport),
    )
  }
  private async acknowledge(
    action: 'start' | 'finish',
    lease: NativeRouteCallbackLease,
  ): Promise<void> {
    try {
      await this.transport.callback(action, lease)
    } catch (error) {
      throw nativeRouteCommandFailed(error as UntrustedInput)
    }
  }
  private async command(params: Record<string, UntrustedInput>): Promise<UntrustedInput> {
    try {
      return await this.transport.run(params)
    } catch (error) {
      throw nativeRouteCommandFailed(error as UntrustedInput)
    }
  }
  private record(error: UntrustedInput): void {
    if (this.failures.length >= 128) this.failures.shift()
    this.failures.push(toError(error))
  }
  private assertOpen(): void {
    if (this.closed) throw new Error('Native WebSocket route owner is closed')
    this.transport.signal.throwIfAborted()
  }
}
