// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import * as z from 'zod'

import { runNativeWebSocketPayload } from './native-websocket-payload.js'
import { type UntrustedInput } from './untrusted.js'

interface RouteTransport {
  run(params: Record<string, UntrustedInput>, signal?: AbortSignal): Promise<UntrustedInput>
  cleanup?(params: Record<string, UntrustedInput>): Promise<UntrustedInput>
  timeout(): number
}
const Identity = z.string().min(1).max(128)
const Token = z.string().regex(/^[1-9][0-9]{0,15}$/u)
const Receipt = z.strictObject({ routingToken: Token, revision: z.literal(1) })
const Event = z.strictObject({
  routingToken: Token,
  requestId: Identity,
  stage: z.enum(['created', 'message', 'closed']),
  url: z.string().max(8192),
  messageId: z.string().max(32),
  outgoing: z.boolean(),
  opcode: z.number().int(),
  payloadBase64: z.string().max(2_796_204),
})
const WaitOptions = z.strictObject({
  timeout: z.number().int().min(0).max(2_147_483_647).optional(),
  signal: z.instanceof(AbortSignal).optional(),
})
export interface NativeWebSocketRouteWaitOptions {
  timeout?: number
  signal?: AbortSignal
}
export type NativeWebSocketRoutePayload = string | Buffer
export type NativeWebSocketRoutedEvent =
  | { event: 'created'; socket: NativeRoutedWebSocket }
  | { event: 'message'; socket: NativeRoutedWebSocket; message: NativeWebSocketRouteMessage }
  | { event: 'closed'; socket: NativeRoutedWebSocket }

function encode(payload: NativeWebSocketRoutePayload): { opcode: 1 | 2; payloadBase64: string } {
  const bytes =
    typeof payload === 'string'
      ? Buffer.from(payload)
      : Buffer.isBuffer(payload)
        ? payload
        : undefined
  if (!bytes) throw new Error('Native WebSocket payload must be a string or Buffer')
  if (bytes.length > 2_097_152) throw new Error('Native WebSocket route payload exceeds 2 MiB')
  return { opcode: typeof payload === 'string' ? 1 : 2, payloadBase64: bytes.toString('base64') }
}

/** One actual held native message, identified by its target, channel and sequence. */
export class NativeWebSocketRouteMessage {
  private decided = false
  private deciding = false
  private readonly bytes: Buffer
  constructor(
    private readonly socket: NativeRoutedWebSocket,
    readonly id: string,
    readonly outgoing: boolean,
    readonly opcode: 1 | 2,
    bytes: Buffer,
  ) {
    this.bytes = bytes
  }
  isHandled(): boolean {
    return this.decided || this.deciding
  }
  payload(): NativeWebSocketRoutePayload {
    return this.opcode === 1 ? this.bytes.toString('utf8') : Buffer.from(this.bytes)
  }
  forward(): Promise<void> {
    return this.decide('forward')
  }
  drop(): Promise<void> {
    return this.decide('drop')
  }
  replace(payload: NativeWebSocketRoutePayload): Promise<void> {
    return this.decide('replace', encode(payload))
  }
  private async decide(
    operation: string,
    fields: Record<string, UntrustedInput> = {},
  ): Promise<void> {
    if (this.decided || this.deciding)
      throw new Error('Native WebSocket message decision is already consumed or pending')
    this.deciding = true
    try {
      await this.socket.command(operation, { messageId: this.id, ...fields })
      this.decided = true
    } finally {
      this.decided = true
      this.deciding = false
    }
  }
}

export type NativeWebSocketMessageHandler = (
  message: NativeWebSocketRouteMessage,
) => void | Promise<void>
export type NativeWebSocketCloseHandler = (socket: NativeRoutedWebSocket) => void | Promise<void>
interface NativeWebSocketCommandOwner {
  command(
    requestId: string,
    operation: string,
    fields: Record<string, UntrustedInput>,
  ): Promise<void>
}

/** Real Blink channel control. ACKs mean native acceptance, not remote delivery. */
export class NativeRoutedWebSocket {
  private closed = false
  private detached = false
  private initial: 'pending' | 'deciding' | 'connected' | 'mocked' | 'continued' | 'uncertain' =
    'pending'
  private initialWork: Promise<void> | undefined
  private messageHandler: NativeWebSocketMessageHandler | undefined
  private closeHandler: NativeWebSocketCloseHandler | undefined
  constructor(
    private readonly owner: NativeWebSocketCommandOwner,
    readonly id: string,
    private readonly socketURL: string,
    private readonly automaticMock = false,
  ) {}
  /** Installing a handler makes message decisions explicit; an undecided message is dropped. */
  onMessage(handler: NativeWebSocketMessageHandler): void {
    if (this.closed || this.detached)
      throw new Error('Native WebSocket callback handle is closed or detached')
    if (typeof handler !== 'function')
      throw new Error('Native WebSocket message handler must be a function')
    this.messageHandler = handler
  }
  onClose(handler: NativeWebSocketCloseHandler): void {
    if (this.closed || this.detached)
      throw new Error('Native WebSocket callback handle is closed or detached')
    if (typeof handler !== 'function')
      throw new Error('Native WebSocket close handler must be a function')
    this.closeHandler = handler
  }
  /**
   * Default registered routes mock through the actual native channel.
   * @internal
   */
  async completeInitial(): Promise<void> {
    if (this.initialWork) await this.initialWork
    else if (this.initial === 'pending') await this.mock()
  }
  /**
   * Native event callback ownership is held by the registration adapter.
   * @internal
   */
  async receiveMessage(message: NativeWebSocketRouteMessage): Promise<void> {
    if (this.messageHandler) {
      await this.messageHandler(message)
      if (!message.isHandled()) await message.drop()
    } else if (this.initial === 'mocked') await message.drop()
    else await message.forward()
  }
  /**
   * Actual closed event only; removal merely detaches handles.
   * @internal
   */
  async receiveClosed(): Promise<void> {
    try {
      await this.closeHandler?.(this)
    } finally {
      this.messageHandler = undefined
      this.closeHandler = undefined
    }
  }
  url(): string {
    return this.socketURL
  }
  isClosed(): boolean {
    return this.closed
  }
  isDetached(): boolean {
    return this.detached
  }
  /** Resume the real server transport with message interception. */
  connectToServer(): Promise<void> {
    return this.initialDecision('connect', 'connected')
  }
  /** Open the original native client without creating a server connection. */
  mock(): Promise<void> {
    return this.initialDecision('mock', 'mocked')
  }
  /** Resume the real transport and release this channel from message interception. */
  continue(): Promise<void> {
    return this.initialDecision('continue', 'continued')
  }
  /** Deliver a message to the page's native WebSocket client. */
  async send(payload: NativeWebSocketRoutePayload): Promise<void> {
    if (this.automaticMock) await this.completeInitial()
    await this.command('client', encode(payload))
  }
  /** Write through the channel's original connected server transport. */
  sendToServer(payload: NativeWebSocketRoutePayload): Promise<void> {
    return this.command('server', encode(payload))
  }
  async close(options: { code?: number; reason?: string } = {}): Promise<void> {
    const value = z
      .strictObject({
        code: z
          .number()
          .int()
          .refine((code) => code === 1000 || (code >= 3000 && code <= 4999))
          .default(1000),
        reason: z.string().default(''),
      })
      .parse(options)
    if (Buffer.byteLength(value.reason) > 123)
      throw new Error('Native WebSocket close reason exceeds 123 UTF-8 bytes')
    await this.command('close', value)
  }
  abort(): Promise<void> {
    return this.command('abort')
  }
  /**
   * The backend validates exact one-shot message ownership.
   * @internal
   */
  async command(operation: string, fields: Record<string, UntrustedInput> = {}): Promise<void> {
    if (this.closed) throw new Error('Native routed WebSocket is closed')
    if (this.detached) throw new Error('Native routed WebSocket control handle is detached')
    await this.owner.command(this.id, operation, fields)
  }
  /**
   * Only an actual native closed event retires the public handle.
   * @internal
   */
  retire(): void {
    this.closed = true
    this.detached = true
  }
  /**
   * Policy removal does not prove actual transport closure.
   * @internal
   */
  detach(): void {
    this.detached = true
    this.messageHandler = undefined
    this.closeHandler = undefined
  }
  private initialDecision(
    operation: string,
    next: 'connected' | 'mocked' | 'continued',
  ): Promise<void> {
    if (this.initial !== 'pending')
      return Promise.reject(
        new Error('Native WebSocket initial decision is already consumed or pending'),
      )
    this.initial = 'deciding'
    this.initialWork = this.command(operation).then(
      () => {
        this.initial = next
        if (next === 'continued') this.detach()
      },
      (error) => {
        this.initial = 'uncertain'
        throw error
      },
    )
    return this.initialWork
  }
}

/**
 * Explicit native routing for subsequent channels in this exact root page target.
 * Enable probes the actual versioned backend and rejects on stock CEF.
 * Context, OOPIF and worker policy inheritance are not provided by this adapter.
 */
export class NativeWebSocketRouting {
  private token: string | undefined
  private enabling = false
  private disabling = false
  private waiting = false
  private uncertain = false
  private readonly sockets = new Map<string, NativeRoutedWebSocket>()
  constructor(private readonly transport: RouteTransport) {}
  async enable(): Promise<void> {
    if (this.uncertain)
      throw new Error(
        'Native WebSocket routing transport state is uncertain; recreate the page before enabling again',
      )
    if (this.token || this.enabling || this.disabling)
      throw new Error('Native WebSocket routing is already enabled or configuring')
    this.enabling = true
    try {
      this.token = Receipt.parse(
        await this.transport.run({ action: 'routeEnable', timeoutMs: this.transport.timeout() }),
      ).routingToken
    } catch (error) {
      this.uncertain = true
      throw error
    } finally {
      this.enabling = false
    }
  }
  async nextEvent(
    options: NativeWebSocketRouteWaitOptions = {},
  ): Promise<NativeWebSocketRoutedEvent> {
    const config = WaitOptions.parse(options),
      token = this.requireToken()
    if (this.waiting) throw new Error('Native WebSocket routing permits one pending event consumer')
    this.waiting = true
    try {
      const event = Event.parse(
        await this.transport.run(
          {
            action: 'routeWait',
            routingToken: token,
            timeoutMs: config.timeout ?? this.transport.timeout(),
          },
          config.signal,
        ),
      )
      if (event.routingToken !== token || this.token !== token)
        throw new Error('Native WebSocket route event generation changed')
      if (event.stage === 'created') {
        if (this.sockets.has(event.requestId) || this.sockets.size >= 128)
          throw new Error('Invalid native WebSocket route socket ownership')
        const socket = new NativeRoutedWebSocket(this, event.requestId, event.url)
        this.sockets.set(event.requestId, socket)
        return { event: 'created', socket }
      }
      const socket = this.sockets.get(event.requestId)
      if (!socket) throw new Error('Native WebSocket route event has no owned socket')
      if (event.stage === 'closed') {
        socket.retire()
        this.sockets.delete(event.requestId)
        return { event: 'closed', socket }
      }
      if ((event.opcode !== 1 && event.opcode !== 2) || !event.messageId)
        throw new Error('Invalid native WebSocket route message identity')
      const bytes = Buffer.from(event.payloadBase64, 'base64')
      if (bytes.length > 2_097_152 || bytes.toString('base64') !== event.payloadBase64)
        throw new Error('Invalid native WebSocket route payload encoding')
      return {
        event: 'message',
        socket,
        message: new NativeWebSocketRouteMessage(
          socket,
          event.messageId,
          event.outgoing,
          event.opcode,
          bytes,
        ),
      }
    } finally {
      this.waiting = false
    }
  }
  async disable(): Promise<void> {
    const token = this.requireToken()
    this.disabling = true
    try {
      await this.transport.run({
        action: 'routeDisable',
        routingToken: token,
        timeoutMs: this.transport.timeout(),
      })
      // Only this actual native cleanup ACK allows another routing generation.
      this.token = undefined
    } catch (error) {
      this.uncertain = true
      throw error
    } finally {
      this.disabling = false
      for (const socket of this.sockets.values()) socket.detach()
      this.sockets.clear()
    }
  }
  /**
   * Native acceptance only; transport uncertainty is never reported as delivery.
   * @internal
   */
  async command(
    requestId: string,
    operation: string,
    fields: Record<string, UntrustedInput>,
  ): Promise<void> {
    await runNativeWebSocketPayload(
      (params) => this.transport.run(params),
      {
        action: 'routeCommand',
        payloadScope: 'explicit',
        routingToken: this.requireToken(),
        requestId,
        operation,
        messageId: '',
        opcode: 0,
        payloadBase64: '',
        code: 0,
        reason: '',
        ...fields,
        timeoutMs: this.transport.timeout(),
      },
      this.transport.cleanup?.bind(this.transport),
    )
    if (operation === 'continue') {
      this.sockets.get(requestId)?.detach()
      this.sockets.delete(requestId)
    }
  }
  private requireToken(): string {
    if (this.uncertain)
      throw new Error(
        'Native WebSocket routing transport state is uncertain; recreate the page before using this controller',
      )
    if (!this.token || this.disabling) throw new Error('Native WebSocket routing is disabled')
    return this.token
  }
}
