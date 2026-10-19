// SPDX-License-Identifier: MIT
import { afterEach, describe, expect, it, vi } from 'vitest'

import { BrowserProtocolError, BrowserWire } from '../src/browser-wire.js'
import { type UntrustedInput } from '../src/untrusted.js'

class FakeSocket extends EventTarget {
  static readonly OPEN = 1
  static readonly CLOSED = 3
  static instances: FakeSocket[] = []
  readyState = 0
  bufferedAmount = 0
  sent: Record<string, UntrustedInput>[] = []
  failSend = false
  holdClose = false
  constructor(readonly url: string) {
    super()
    FakeSocket.instances.push(this)
    queueMicrotask(() => {
      this.readyState = 1
      this.dispatchEvent(new Event('open'))
    })
  }
  send(text: string): void {
    if (this.failSend) throw new Error('send failed')
    this.sent.push(JSON.parse(text) as Record<string, UntrustedInput>)
  }
  receive(packet: UntrustedInput): void {
    this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify(packet) }))
  }
  close(): void {
    this.readyState = 2
    if (!this.holdClose) queueMicrotask(() => this.finishClose())
  }
  finishClose(): void {
    this.readyState = 3
    this.dispatchEvent(new Event('close'))
  }
}

afterEach(() => {
  vi.unstubAllGlobals()
  vi.useRealTimers()
  FakeSocket.instances = []
})

async function connection(): Promise<{ wire: BrowserWire; socket: FakeSocket }> {
  vi.stubGlobal('WebSocket', FakeSocket)
  const wire = await BrowserWire.connect('ws://localhost:1234/session', { timeoutMs: 1000 })
  const socket = FakeSocket.instances[0]
  if (!socket) throw new Error('Socket missing')
  return { wire, socket }
}

describe('multiplexed browser protocol wire', () => {
  it('matches out-of-order responses on one connection and carries session ids', async () => {
    const { wire, socket } = await connection()
    const first = wire.request('first', { input: 3 }, { sessionId: 'page-1' })
    const second = wire.request('second')
    expect(socket.sent).toEqual([
      { id: 1, method: 'first', params: { input: 3 }, sessionId: 'page-1' },
      { id: 2, method: 'second', params: {} },
    ])
    socket.receive({ id: 2, result: { order: 2 } })
    socket.receive({ id: 1, result: { order: 1 } })
    expect(await first).toEqual({ order: 1 })
    expect(await second).toEqual({ order: 2 })
    expect(wire.stats()).toMatchObject({ pending: 0, pendingPeak: 2, sent: 2, received: 2 })
    await wire.close()
  })

  it('decodes both CDP and BiDi errors and settles malformed error payloads', async () => {
    const { wire, socket } = await connection()
    for (const [payload, message] of [
      [{ error: { code: -32000, message: 'CDP rejected' } }, 'CDP rejected'],
      [{ type: 'error', error: 'invalid argument', message: 'BiDi rejected' }, 'BiDi rejected'],
      [{ error: null }, 'Invalid browser protocol'],
    ] as const) {
      const work = wire.request('bad')
      const rejected = expect(work).rejects.toThrow(message)
      socket.receive({ id: socket.sent.at(-1)?.id, ...payload })
      await rejected
    }
    expect(wire.stats().pending).toBe(0)
    await wire.close()
  })

  it('exposes protocol error method and code', async () => {
    const { wire, socket } = await connection()
    const work = wire.request('Page.unknown').catch((error: UntrustedInput) => error)
    socket.receive({ id: 1, error: { code: -32601, message: 'missing method' } })
    const error = await work
    expect(error).toBeInstanceOf(BrowserProtocolError)
    expect(error).toMatchObject({ method: 'Page.unknown', code: -32601 })
    await wire.close()
  })

  it('discards cancelled and late responses without retaining pending work', async () => {
    const { wire, socket } = await connection()
    const controller = new AbortController()
    const work = wire.request('cancel', {}, { signal: controller.signal })
    const rejected = expect(work).rejects.toThrow('cancelled')
    controller.abort(new Error('cancelled'))
    await rejected
    socket.receive({ id: 1, result: { large: 'ignored' } })
    expect(wire.stats().pending).toBe(0)
    const before = socket.sent.length
    await expect(wire.request('never', {}, { signal: controller.signal })).rejects.toThrow(
      'cancelled',
    )
    expect(socket.sent.length).toBe(before)
    await wire.close()
  })

  it('clears timeout ownership and accepts subsequent commands', async () => {
    const { wire, socket } = await connection()
    const work = wire.request('slow', {}, { timeoutMs: 5 })
    await expect(work).rejects.toThrow('timed out')
    expect(wire.stats().pending).toBe(0)
    const next = wire.request('next')
    socket.receive({ id: socket.sent.at(-1)?.id, result: {} })
    await next
    await wire.close()
  })

  it('enforces byte and pending-command budgets before sending more work', async () => {
    const { wire, socket } = await connection()
    socket.bufferedAmount = 16 * 1024 * 1024
    await expect(wire.request('full')).rejects.toThrow('byte budget')
    expect(wire.stats().pending).toBe(0)
    socket.bufferedAmount = 0
    const pending = Array.from({ length: 2048 }, () => wire.request('wait').catch(() => undefined))
    await expect(wire.request('overflow')).rejects.toThrow('pending command limit')
    await wire.close()
    await Promise.all(pending)
    expect(wire.stats().pending).toBe(0)
  })

  it('cleans serialization failures and socket send failures', async () => {
    const { wire, socket } = await connection()
    const cyclic: Record<string, UntrustedInput> = {}
    cyclic.self = cyclic
    await expect(wire.request('cyclic', cyclic)).rejects.toThrow()
    socket.failSend = true
    await expect(wire.request('failed')).rejects.toThrow('send failed')
    expect(wire.stats().pending).toBe(0)
    await wire.close()
  })

  it('routes scoped events and releases listeners on unsubscribe and close', async () => {
    const { wire, socket } = await connection()
    const seen: UntrustedInput[] = []
    const stop = wire.on('changed', (params, session) => seen.push([params, session]))
    socket.receive({ method: 'changed', params: { value: 1 }, sessionId: 'one' })
    stop()
    socket.receive({ method: 'changed', params: { value: 2 } })
    expect(seen).toEqual([[{ value: 1 }, 'one']])
    wire.on('other', () => undefined)
    await wire.close()
    expect(wire.stats().eventListeners).toBe(0)
  })

  it('rejects all work and closes on malformed packets', async () => {
    const { wire, socket } = await connection()
    const work = wire.request('active')
    const rejected = expect(work).rejects.toThrow('Invalid browser protocol packet kind')
    socket.receive({ nonsense: true })
    await rejected
    await wire.closed
    expect(wire.isConnected()).toBe(false)
    expect(wire.stats().pending).toBe(0)
  })

  it('closed waits for physical socket close, and close reports a failed handshake', async () => {
    const { wire, socket } = await connection()
    socket.holdClose = true
    vi.useFakeTimers()
    let settled = false
    void wire.closed.then(() => {
      settled = true
    })
    const closing = wire.close()
    const rejected = expect(closing).rejects.toThrow('close handshake did not finish')
    await vi.advanceTimersByTimeAsync(2001)
    await rejected
    expect(settled).toBe(false)
    expect(wire.isConnected()).toBe(false)
    socket.finishClose()
    await wire.closed
    expect(settled).toBe(true)
  })
})
