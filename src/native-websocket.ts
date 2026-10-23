// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import * as z from 'zod'

import { type NativeBrowser } from './native-browser.js'
import {
  waitNativeEventPredicate,
  type NativeEventPredicate,
  type NativeEventPredicateOptions,
} from './native-event-predicate.js'
import {
  NativeWebSocketInfoSchema,
  type NativeWebSocketInfo,
  type NativeWebSocketMessage,
} from './native-websocket-protocol.js'

export interface NativeWebSocketFrame {
  payload: string | Buffer
}
type SocketEventValue = NativeWebSocketFrame | string | NativeWebSocket
type SocketPredicate = (value: never) => boolean | Promise<boolean>
export interface NativeWebSocketWaitOptions<T> extends NativeEventPredicateOptions {
  predicate?: NativeEventPredicate<T>
}
export type NativeWebSocketEvent = 'framesent' | 'framereceived' | 'socketerror' | 'close'
interface SocketHandler {
  callback: (value: SocketEventValue) => void
  original: (value: never) => void
  once: boolean
  fired: boolean
}
interface SocketState {
  info: NativeWebSocketInfo
  closed: boolean
  socket: WeakRef<NativeWebSocket> | undefined
}
const WaitOptions = z.strictObject({
  timeout: z.number().int().min(0).max(2_147_483_647).optional(),
  signal: z.instanceof(AbortSignal).optional(),
  predicate: z.custom<SocketPredicate>((value) => typeof value === 'function').optional(),
})
const WaitResult = z.strictObject({
  info: NativeWebSocketInfoSchema,
  payloadBase64: z.string().max(2_796_204),
})
/** Actual browser socket observation; payloads are genuine CDP bytes. */
export class NativeWebSocket {
  private readonly listeners = new Map<NativeWebSocketEvent, SocketHandler[]>()
  constructor(
    private readonly source: NativeBrowser,
    private readonly state: SocketState,
    private readonly timeout: () => number,
  ) {}
  url(): string {
    return this.state.info.url
  }
  isClosed(): boolean {
    return this.state.closed
  }
  on(event: 'framesent' | 'framereceived', listener: (frame: NativeWebSocketFrame) => void): this
  on(event: 'socketerror', listener: (error: string) => void): this
  on(event: 'close', listener: (socket: NativeWebSocket) => void): this
  on(event: NativeWebSocketEvent, listener: (value: never) => void): this {
    const name = z.enum(['framesent', 'framereceived', 'socketerror', 'close']).parse(event)
    const handlers = this.listeners.get(name) ?? []
    handlers.push({
      callback: listener as (value: SocketEventValue) => void,
      original: listener,
      once: false,
      fired: false,
    })
    this.listeners.set(name, handlers)
    return this
  }
  off(event: 'framesent' | 'framereceived', listener: (frame: NativeWebSocketFrame) => void): this
  off(event: 'socketerror', listener: (error: string) => void): this
  off(event: 'close', listener: (socket: NativeWebSocket) => void): this
  off(event: NativeWebSocketEvent, listener: (value: never) => void): this {
    this.remove(event, listener)
    return this
  }
  once(event: 'framesent' | 'framereceived', listener: (frame: NativeWebSocketFrame) => void): this
  once(event: 'socketerror', listener: (error: string) => void): this
  once(event: 'close', listener: (socket: NativeWebSocket) => void): this
  once(event: NativeWebSocketEvent, listener: (value: never) => void): this {
    const name = z.enum(['framesent', 'framereceived', 'socketerror', 'close']).parse(event)
    const handlers = this.listeners.get(name) ?? []
    handlers.push({
      callback: listener as (value: SocketEventValue) => void,
      original: listener,
      once: true,
      fired: false,
    })
    this.listeners.set(name, handlers)
    return this
  }
  removeListener(event: NativeWebSocketEvent, listener: (value: never) => void): this {
    this.remove(event, listener)
    return this
  }
  async waitForEvent(
    event: 'framesent' | 'framereceived',
    options?:
      NativeWebSocketWaitOptions<NativeWebSocketFrame> | NativeEventPredicate<NativeWebSocketFrame>,
  ): Promise<NativeWebSocketFrame>
  async waitForEvent(
    event: 'socketerror',
    options?: NativeWebSocketWaitOptions<string> | NativeEventPredicate<string>,
  ): Promise<string>
  async waitForEvent(
    event: 'close',
    options?: NativeWebSocketWaitOptions<NativeWebSocket> | NativeEventPredicate<NativeWebSocket>,
  ): Promise<NativeWebSocket>
  async waitForEvent(
    event: NativeWebSocketEvent,
    options: (NativeEventPredicateOptions & { predicate?: SocketPredicate }) | SocketPredicate = {},
  ): Promise<NativeWebSocketFrame | string | NativeWebSocket> {
    const name = z.enum(['framesent', 'framereceived', 'socketerror', 'close']).parse(event)
    const config = WaitOptions.parse(
      typeof options === 'function' ? { predicate: options } : options,
    )
    const predicate = config.predicate
    if (predicate)
      return waitNativeEventPredicate(
        this.source,
        name,
        (value) => {
          const result = WaitResult.parse(value)
          if (result.info.socketId !== this.state.info.socketId || result.info.event !== name)
            throw new Error('Native WebSocket predicate candidate identity differs from its wait')
          this.state.info = result.info
          if (result.info.event === 'close') this.state.closed = true
          return this.value({
            info: result.info,
            payload: Buffer.from(result.payloadBase64, 'base64'),
          })
        },
        async (value) => Boolean(await predicate(value as never)),
        {
          timeout: config.timeout ?? this.timeout(),
          signal: config.signal,
          afterSequence: this.source.webSocketSequence?.() ?? this.state.info.sequence,
        },
        {
          command: 'websocket',
          fields: { action: 'waitPredicate', socketId: this.state.info.socketId },
        },
      )
    const result = WaitResult.parse(
      await this.source.runAutomation(
        'websocket',
        {
          action: 'waitEvent',
          socketId: this.state.info.socketId,
          event: name,
          afterSequence: this.source.webSocketSequence?.() ?? this.state.info.sequence,
          timeoutMs: config.timeout ?? this.timeout(),
        },
        config.signal,
      ),
    )
    this.state.info = result.info
    if (result.info.event === 'close') this.state.closed = true
    return this.value({ info: result.info, payload: Buffer.from(result.payloadBase64, 'base64') })
  }
  dispatch(message: NativeWebSocketMessage): void {
    if (message.info.event === 'created') return
    this.state.info = message.info
    if (message.info.event === 'close') this.state.closed = true
    const value = this.value(message)
    for (const handler of [...(this.listeners.get(message.info.event) ?? [])]) {
      if (handler.once) {
        if (handler.fired) continue
        handler.fired = true
        const handlers = this.listeners.get(message.info.event)
        const index = handlers?.indexOf(handler) ?? -1
        if (index >= 0) handlers?.splice(index, 1)
      }
      handler.callback(value)
    }
  }
  removeAllListeners(event?: NativeWebSocketEvent): this {
    if (event === undefined) this.listeners.clear()
    else
      this.listeners.delete(
        z.enum(['framesent', 'framereceived', 'socketerror', 'close']).parse(event),
      )
    return this
  }
  prependListener(
    event: 'framesent' | 'framereceived',
    listener: (frame: NativeWebSocketFrame) => void,
  ): this
  prependListener(event: 'socketerror', listener: (error: string) => void): this
  prependListener(event: 'close', listener: (socket: NativeWebSocket) => void): this
  prependListener(event: NativeWebSocketEvent, listener: (value: never) => void): this {
    return this.prepend(event, listener, false)
  }
  prependOnceListener(
    event: 'framesent' | 'framereceived',
    listener: (frame: NativeWebSocketFrame) => void,
  ): this
  prependOnceListener(event: 'socketerror', listener: (error: string) => void): this
  prependOnceListener(event: 'close', listener: (socket: NativeWebSocket) => void): this
  prependOnceListener(event: NativeWebSocketEvent, listener: (value: never) => void): this {
    return this.prepend(event, listener, true)
  }
  private prepend(
    event: NativeWebSocketEvent,
    listener: (value: never) => void,
    once: boolean,
  ): this {
    const name = z.enum(['framesent', 'framereceived', 'socketerror', 'close']).parse(event)
    const handlers = this.listeners.get(name) ?? []
    handlers.unshift({
      callback: listener as (value: SocketEventValue) => void,
      original: listener,
      once,
      fired: false,
    })
    this.listeners.set(name, handlers)
    return this
  }
  private remove(event: NativeWebSocketEvent, listener: (value: never) => void): void {
    const handlers = this.listeners.get(event)
    const index = handlers?.findLastIndex((handler) => handler.original === listener) ?? -1
    if (index >= 0) handlers?.splice(index, 1)
  }
  private value(message: NativeWebSocketMessage): NativeWebSocketFrame | string | NativeWebSocket {
    if (message.info.event === 'close') return this
    if (message.info.event === 'socketerror') return message.info.error ?? ''
    if (message.info.opcode !== 1 && message.info.opcode !== 2)
      throw new Error('Native socket frame has no opcode')
    return {
      payload:
        message.info.opcode === 1 ? message.payload.toString('utf8') : Buffer.from(message.payload),
    }
  }
}
/** Weak public identities with bounded metadata; native owns all event waits. */
export class NativeWebSockets {
  private static readonly controllers = new WeakMap<NativeBrowser, NativeWebSockets>()
  static forSource(source: NativeBrowser, timeout: () => number): NativeWebSockets {
    let controller = this.controllers.get(source)
    if (!controller) {
      controller = new NativeWebSockets(source, timeout)
      this.controllers.set(source, controller)
    }
    return controller
  }
  private readonly states = new Map<string, SocketState>()
  private readonly created: ((socket: NativeWebSocket) => void)[] = []
  private readonly unsubscribe: (() => void) | undefined
  private closed = false
  constructor(
    private readonly source: NativeBrowser,
    private readonly timeout: () => number,
  ) {
    this.unsubscribe = source.onWebSocketMessage?.((message) => this.receive(message))
    ;(source.pageSignal ?? source.signal).addEventListener('abort', () => this.dispose(), {
      once: true,
    })
  }
  on(listener: (socket: NativeWebSocket) => void, prepend = false): () => void {
    if (prepend) this.created.unshift(listener)
    else this.created.push(listener)
    return () => {
      const index = this.created.indexOf(listener)
      if (index >= 0) this.created.splice(index, 1)
    }
  }
  async waitForEvent(
    options:
      NativeWebSocketWaitOptions<NativeWebSocket> | NativeEventPredicate<NativeWebSocket> = {},
  ): Promise<NativeWebSocket> {
    const config = WaitOptions.parse(
      typeof options === 'function' ? { predicate: options } : options,
    )
    const predicate = config.predicate
    if (predicate)
      return waitNativeEventPredicate(
        this.source,
        'websocket',
        (value) => {
          const result = WaitResult.parse(value)
          if (result.info.event !== 'created')
            throw new Error('Native WebSocket creation predicate received another event')
          return this.fromInfo(result.info)
        },
        async (value) => Boolean(await predicate(value as never)),
        {
          timeout: config.timeout ?? this.timeout(),
          signal: config.signal,
          afterSequence: this.source.webSocketSequence?.() ?? 0,
        },
        { command: 'websocket', fields: { action: 'waitPredicate', socketId: '' } },
      )
    const result = WaitResult.parse(
      await this.source.runAutomation(
        'websocket',
        {
          action: 'waitEvent',
          socketId: '',
          event: 'created',
          afterSequence: this.source.webSocketSequence?.() ?? 0,
          timeoutMs: config.timeout ?? this.timeout(),
        },
        config.signal,
      ),
    )
    return this.fromInfo(result.info)
  }
  private fromInfo(info: NativeWebSocketInfo): NativeWebSocket {
    let state = this.states.get(info.socketId)
    if (!state) {
      state = { info, closed: false, socket: undefined }
      this.states.set(info.socketId, state)
    }
    let socket = state.socket?.deref()
    socket ??= new NativeWebSocket(this.source, state, this.timeout)
    state.socket = new WeakRef(socket)
    return socket
  }
  private receive(message: NativeWebSocketMessage): void {
    if (this.closed) return
    let state = this.states.get(message.info.socketId)
    if (!state) {
      state = { info: message.info, closed: message.info.event === 'close', socket: undefined }
      this.states.set(message.info.socketId, state)
    }
    state.info = message.info
    if (message.info.event === 'close') state.closed = true
    let socket = state.socket?.deref()
    if (message.info.event === 'created') {
      socket ??= new NativeWebSocket(this.source, state, this.timeout)
      state.socket = new WeakRef(socket)
      for (const listener of [...this.created]) listener(socket)
    } else socket?.dispatch(message)
    if (this.states.size > 512) {
      for (const [id, entry] of this.states) {
        if (entry.closed) this.states.delete(id)
        if (this.states.size <= 512) break
      }
      if (this.states.size > 512) throw new Error('Native socket identity capacity exceeded')
    }
  }
  dispose(): void {
    if (this.closed) return
    this.closed = true
    this.unsubscribe?.()
    for (const state of this.states.values()) state.closed = true
    this.states.clear()
    this.created.length = 0
  }
}
