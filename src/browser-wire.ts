// SPDX-License-Identifier: MIT
import { type UntrustedInput, toError } from './untrusted.js'

export function protocolRecord(
  value: UntrustedInput,
  label: string,
): Record<string, UntrustedInput> {
  if (value === null || typeof value !== 'object' || Array.isArray(value))
    throw new Error(`Invalid browser protocol ${label}`)
  return value as Record<string, UntrustedInput>
}

export function protocolString(value: UntrustedInput, label: string): string {
  if (typeof value !== 'string') throw new Error(`Invalid browser protocol ${label}`)
  return value
}

export function protocolNumber(value: UntrustedInput, label: string): number {
  if (typeof value !== 'number' || !Number.isFinite(value))
    throw new Error(`Invalid browser protocol ${label}`)
  return value
}

export class BrowserProtocolError extends Error {
  constructor(
    readonly method: string,
    readonly code: string | number,
    message: string,
  ) {
    super(`${method}: ${message}`)
    this.name = 'BrowserProtocolError'
  }
}

type EventListener = (params: Record<string, UntrustedInput>, sessionId?: string) => void
interface Pending {
  method: string
  resolve: (result: Record<string, UntrustedInput>) => void
  reject: (error: Error) => void
  cleanup: () => void
}

/** A persistent, multiplexed protocol connection. No child process per command. */
export class BrowserWire {
  private readonly pending = new Map<number, Pending>()
  private readonly listeners = new Map<string, Set<EventListener>>()
  private nextId = 0
  private ended = false
  private closePromise: Promise<void> | undefined
  private resolveClosed!: () => void
  private detachSignal: (() => void) | undefined
  readonly closed = new Promise<void>((resolve) => {
    this.resolveClosed = resolve
  })
  private readonly counters = {
    sent: 0,
    received: 0,
    sentBytes: 0,
    receivedBytes: 0,
    pendingPeak: 0,
  }

  private constructor(
    private readonly socket: WebSocket,
    readonly timeoutMs: number,
  ) {
    socket.addEventListener('message', this.message)
    socket.addEventListener('close', this.didClose)
    socket.addEventListener('error', this.didError)
  }

  static async connect(
    endpoint: string,
    options: { timeoutMs?: number; signal?: AbortSignal } = {},
  ): Promise<BrowserWire> {
    const url = new URL(endpoint)
    if (url.protocol !== 'ws:' && url.protocol !== 'wss:')
      throw new Error('Browser endpoint requires ws: or wss:')
    const timeout = options.timeoutMs ?? 30_000
    if (!Number.isFinite(timeout) || timeout <= 0 || timeout > 2_147_483_647)
      throw new Error('Browser timeoutMs must be positive and at most 2147483647')
    options.signal?.throwIfAborted()
    const socket = new WebSocket(url)
    const wire = new BrowserWire(socket, timeout)
    try {
      await new Promise<void>((resolve, reject) => {
        const finish = (error?: Error): void => {
          clearTimeout(timer)
          socket.removeEventListener('open', opened)
          socket.removeEventListener('error', failed)
          socket.removeEventListener('close', failed)
          options.signal?.removeEventListener('abort', aborted)
          if (error) reject(error)
          else resolve()
        }
        const opened = (): void => finish()
        const failed = (): void => finish(new Error('Browser protocol connection failed'))
        const aborted = (): void => finish(toError(options.signal?.reason as UntrustedInput))
        const timer = setTimeout(
          () => finish(new Error('Browser protocol connection timed out')),
          timeout,
        )
        socket.addEventListener('open', opened, { once: true })
        socket.addEventListener('error', failed, { once: true })
        socket.addEventListener('close', failed, { once: true })
        options.signal?.addEventListener('abort', aborted, { once: true })
        if (options.signal?.aborted) aborted()
      })
      const abort = (): void => {
        wire.fail(toError(options.signal?.reason as UntrustedInput))
      }
      options.signal?.addEventListener('abort', abort, { once: true })
      wire.detachSignal = () => options.signal?.removeEventListener('abort', abort)
      if (options.signal?.aborted) abort()
      if (!wire.isConnected()) throw new Error('Browser disconnected during connection')
      return wire
    } catch (error) {
      wire.fail(toError(error as UntrustedInput))
      throw error
    }
  }

  isConnected(): boolean {
    return !this.ended && this.socket.readyState === WebSocket.OPEN
  }

  stats(): Readonly<typeof this.counters & { pending: number; eventListeners: number }> {
    let eventListeners = 0
    for (const group of this.listeners.values()) eventListeners += group.size
    return { ...this.counters, pending: this.pending.size, eventListeners }
  }

  request(
    method: string,
    params: Record<string, UntrustedInput> = {},
    options: {
      sessionId?: string
      signal?: AbortSignal
      timeoutMs?: number
    } = {},
  ): Promise<Record<string, UntrustedInput>> {
    if (!this.isConnected())
      return Promise.reject(new Error('Browser protocol connection is closed'))
    if (options.signal?.aborted)
      return Promise.reject(toError(options.signal.reason as UntrustedInput))
    const timeout = options.timeoutMs ?? this.timeoutMs
    if (!Number.isFinite(timeout) || timeout <= 0 || timeout > 2_147_483_647)
      return Promise.reject(new Error('Invalid browser command timeout'))
    if (this.pending.size >= 2048)
      return Promise.reject(new Error('Browser protocol pending command limit reached'))
    const id = ++this.nextId
    let encoded: string
    try {
      encoded = JSON.stringify({
        id,
        method,
        params,
        ...(options.sessionId ? { sessionId: options.sessionId } : {}),
      })
    } catch (error) {
      return Promise.reject(toError(error as UntrustedInput))
    }
    const bytes = Buffer.byteLength(encoded)
    if (bytes > 8 * 1024 * 1024 || this.socket.bufferedAmount + bytes > 16 * 1024 * 1024)
      return Promise.reject(new Error('Browser protocol outgoing byte budget exceeded'))
    return new Promise((resolve, reject) => {
      const cancel = (error: Error): void => {
        const entry = this.pending.get(id)
        if (!entry) return
        this.pending.delete(id)
        entry.cleanup()
        reject(error)
      }
      const abort = (): void => cancel(toError(options.signal?.reason as UntrustedInput))
      const timer = setTimeout(
        () => cancel(new Error(`${method}: browser command timed out`)),
        timeout,
      )
      this.pending.set(id, {
        method,
        resolve,
        reject,
        cleanup: () => {
          clearTimeout(timer)
          options.signal?.removeEventListener('abort', abort)
        },
      })
      options.signal?.addEventListener('abort', abort, { once: true })
      this.counters.pendingPeak = Math.max(this.counters.pendingPeak, this.pending.size)
      try {
        this.socket.send(encoded)
        this.counters.sent++
        this.counters.sentBytes += bytes
      } catch (error) {
        cancel(toError(error as UntrustedInput))
      }
      if (options.signal?.aborted) abort()
    })
  }

  on(method: string, listener: EventListener): () => void {
    if (this.ended) throw new Error('Browser protocol connection is closed')
    let group = this.listeners.get(method)
    if (!group) {
      group = new Set()
      this.listeners.set(method, group)
    }
    group.add(listener)
    return () => {
      group.delete(listener)
      if (!group.size && this.listeners.get(method) === group) this.listeners.delete(method)
    }
  }

  private readonly message = (event: MessageEvent<UntrustedInput>): void => {
    if (this.ended) return
    try {
      if (typeof event.data !== 'string') throw new Error('Expected text browser protocol packet')
      const bytes = Buffer.byteLength(event.data)
      if (bytes > 64 * 1024 * 1024)
        throw new Error('Browser protocol incoming byte budget exceeded')
      this.counters.received++
      this.counters.receivedBytes += bytes
      const packet = protocolRecord(JSON.parse(event.data) as UntrustedInput, 'packet')
      if (typeof packet.id === 'number') {
        const entry = this.pending.get(packet.id)
        if (!entry) return // Timed out or cancelled; never retain late responses.
        this.pending.delete(packet.id)
        entry.cleanup()
        try {
          if (packet.error !== undefined) {
            const error =
              typeof packet.error === 'string' ? packet : protocolRecord(packet.error, 'error')
            const code =
              typeof error.code === 'number'
                ? error.code
                : typeof packet.error === 'string'
                  ? packet.error
                  : 'protocol error'
            entry.reject(
              new BrowserProtocolError(
                entry.method,
                code,
                typeof error.message === 'string' ? error.message : 'Browser command failed',
              ),
            )
          } else entry.resolve(protocolRecord(packet.result, 'result'))
        } catch (error) {
          entry.reject(toError(error as UntrustedInput))
        }
      } else if (typeof packet.method === 'string') {
        const params =
          packet.params === undefined ? {} : protocolRecord(packet.params, 'event params')
        for (const listener of this.listeners.get(packet.method) ?? [])
          listener(params, typeof packet.sessionId === 'string' ? packet.sessionId : undefined)
      } else throw new Error('Invalid browser protocol packet kind')
    } catch (error) {
      this.fail(toError(error as UntrustedInput))
    }
  }

  private readonly didClose = (): void => {
    this.fail(new Error('Browser protocol connection closed'), false)
  }
  private readonly didError = (): void => {
    this.fail(new Error('Browser protocol connection failed'))
  }

  private fail(error: Error, sendClose = true): void {
    if (this.ended) return
    this.ended = true
    this.detachSignal?.()
    this.detachSignal = undefined
    for (const entry of this.pending.values()) {
      entry.cleanup()
      entry.reject(error)
    }
    this.pending.clear()
    this.listeners.clear()
    this.socket.removeEventListener('message', this.message)
    this.socket.removeEventListener('close', this.didClose)
    const physicalClosed = (): void => {
      this.socket.removeEventListener('error', this.didError)
      this.resolveClosed()
    }
    // Keep the error handler until physical close, including failed handshakes.
    if (sendClose && this.socket.readyState !== WebSocket.CLOSED) {
      this.socket.addEventListener('close', physicalClosed, { once: true })
      this.socket.close()
    } else physicalClosed()
  }

  close(): Promise<void> {
    return (this.closePromise ??= (async () => {
      if (this.socket.readyState === WebSocket.CLOSED) return
      const physical = new Promise<void>((resolve, reject) => {
        const close = (): void => {
          clearTimeout(timer)
          resolve()
        }
        const timer = setTimeout(() => {
          this.socket.removeEventListener('close', close)
          reject(new Error('Browser WebSocket close handshake did not finish within 2000ms'))
        }, 2000)
        this.socket.addEventListener('close', close, { once: true })
      })
      this.fail(new Error('Browser protocol connection closed'))
      await physical
    })())
  }
}
