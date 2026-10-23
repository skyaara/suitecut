// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it, vi } from 'vitest'
import * as z from 'zod'

import { type NativeBrowser } from '../src/native-browser.js'
import {
  nativeEventPredicatePhysicalCount,
  type NativeEventPredicateSnapshot,
} from '../src/native-event-predicate.js'
import { type NativeWebSocketMessage } from '../src/native-websocket-protocol.js'
import { NativeWebSockets, type NativeWebSocket } from '../src/native-websocket.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  let callback: ((message: NativeWebSocketMessage) => void) | undefined
  const lifecycle = new AbortController()
  const predicates = new Set<(value: NativeEventPredicateSnapshot) => void>()
  const source: NativeBrowser = {
    state: 'running',
    url: 'https://native.test',
    signal: lifecycle.signal,
    pixelFormat: 'bgra',
    width: 2,
    height: 2,
    framesPerSecond: 60,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation: () => Promise.resolve({}),
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    close: () => Promise.resolve(),
    onEventPredicate: (listener) => {
      predicates.add(listener)
      return () => {
        predicates.delete(listener)
      }
    },
    webSocketSequence: () => 7,
    onWebSocketMessage: (listener) => {
      callback = listener
      return () => {
        callback = undefined
      }
    },
  }
  const controller = NativeWebSockets.forSource(source, () => 30000)
  let socket: NativeWebSocket | undefined
  controller.on((actual) => {
    socket = actual
  })
  let sequence = 0
  const emit = (event: NativeWebSocketMessage['info']['event']) =>
    callback?.({
      info: {
        socketId: '1',
        sessionId: '',
        requestId: 'actual-test-request',
        url: 'ws://native.test/actual',
        event,
        sequence: ++sequence,
        ...(event === 'framesent' ? { opcode: 1 as const } : {}),
      },
      payload: event === 'framesent' ? Buffer.from('actual payload') : Buffer.alloc(0),
    })
  emit('created')
  if (!socket) throw new Error('Native socket creation fixture failed')
  return { socket, emit, lifecycle, source, controller, predicates }
}

describe('native socket EventEmitter contract', () => {
  it('removes only the last duplicate callback registration', () => {
    const { socket, emit } = fixture(),
      order: string[] = []
    const repeated = () => order.push('repeat')
    socket
      .on('framesent', repeated)
      .on('framesent', () => order.push('middle'))
      .on('framesent', repeated)
    socket.off('framesent', repeated)
    emit('framesent')
    expect(order).toEqual(['repeat', 'middle'])
  })
  it('removes one once callback using its original identity and consumes the remaining once before reentrant delivery', () => {
    const { socket, emit } = fixture()
    let calls = 0
    const repeated = () => {
      ++calls
      emit('framesent')
    }
    socket.once('framesent', repeated).once('framesent', repeated)
    socket.removeListener('framesent', repeated)
    emit('framesent')
    expect(calls).toBe(1)
  })
  it('fires each once registration once when the first callback reenters before the second outer snapshot callback', () => {
    const { socket, emit } = fixture(),
      order: string[] = []
    socket.once('framesent', () => {
      order.push('first')
      emit('framesent')
    })
    socket.once('framesent', () => order.push('second'))
    emit('framesent')
    expect(order).toEqual(['first', 'second'])
  })
  it('preserves prepend ordering and the current dispatch snapshot during removeAllListeners', () => {
    const { socket, emit } = fixture(),
      order: string[] = []
    socket.on('framesent', () => order.push('normal'))
    socket.prependOnceListener('framesent', () => {
      order.push('once')
      socket.removeAllListeners('framesent')
    })
    socket.prependListener('framesent', () => order.push('prepend'))
    emit('framesent')
    emit('framesent')
    expect(order).toEqual(['prepend', 'once', 'normal'])
  })
})

function predicateFixture() {
  const actual = fixture()
  let owner: Record<string, UntrustedInput> = {},
    token = 0
  let complete: ((value: UntrustedInput) => void) | undefined
  let fail: ((error: Error) => void) | undefined
  const run = vi.fn(
    (
      command: string,
      params: Record<string, UntrustedInput> = {},
      signal?: AbortSignal,
    ): Promise<UntrustedInput> => {
      if (command === 'websocket') {
        owner = params
        return new Promise((resolve, reject) => {
          complete = resolve
          fail = reject
          signal?.addEventListener(
            'abort',
            () => reject(signal.reason instanceof Error ? signal.reason : new Error('Aborted')),
            { once: true },
          )
        })
      }
      if (command === 'continueEventPredicate' && params.matches) complete?.({})
      return Promise.resolve({})
    },
  )
  actual.source.runAutomation = run
  const candidate = (payload: string, binary = false, socketId = '1') => {
    const event = z.string().parse(owner.event)
    for (const listener of actual.predicates)
      listener({
        id: 17,
        generation: 4,
        token: ++token,
        clientToken: z.string().parse(owner.clientToken),
        event,
        sequence: 7 + token,
        value: {
          info: {
            socketId,
            sessionId: '',
            requestId: 'actual-test-request',
            url: 'ws://native.test/actual',
            event: event === 'websocket' ? 'created' : event,
            sequence: 7 + token,
            ...(event === 'framereceived' ? { opcode: binary ? 2 : 1 } : {}),
          },
          payloadBase64: Buffer.from(payload).toString('base64'),
        },
      })
  }
  return { ...actual, run, candidate, rejectNative: (error: Error) => fail?.(error) }
}

it('uses the socket native candidate command and one deadline across rejected binary/text frames', async () => {
  const { socket, run, candidate } = predicateFixture()
  const values: { payload: string | Buffer }[] = []
  const wait = socket.waitForEvent('framereceived', {
    timeout: 137,
    predicate: (value) => {
      values.push(value)
      return Buffer.isBuffer(value.payload)
    },
  })
  candidate('first')
  await vi.waitFor(() =>
    expect(run).toHaveBeenCalledWith(
      'continueEventPredicate',
      expect.objectContaining({ matches: false }),
      expect.any(AbortSignal),
    ),
  )
  candidate('accepted', true)
  const result = await wait
  expect(result).toBe(values[1])
  expect(result.payload).toEqual(Buffer.from('accepted'))
  expect(run.mock.calls.filter(([command]) => command === 'websocket')).toHaveLength(1)
  expect(run).toHaveBeenNthCalledWith(
    1,
    'websocket',
    expect.objectContaining({
      action: 'waitPredicate',
      socketId: '1',
      timeoutMs: 137,
      afterSequence: 7,
    }),
    expect.any(AbortSignal),
  )
})

it('accepts the creation function overload and returns the existing public socket identity', async () => {
  const { socket, controller, candidate, run } = predicateFixture()
  const wait = controller.waitForEvent((value) => value === socket)
  candidate('')
  expect(await wait).toBe(socket)
  expect(run).toHaveBeenNthCalledWith(
    1,
    'websocket',
    expect.objectContaining({ action: 'waitPredicate', socketId: '', event: 'websocket' }),
    expect.any(AbortSignal),
  )
})

it('keeps an async socket predicate physically live after genuine native close rejection and suppresses its late ACK', async () => {
  const { socket, source, candidate, rejectNative, run } = predicateFixture()
  let settle: ((value: boolean) => void) | undefined
  const wait = socket.waitForEvent(
    'framereceived',
    () =>
      new Promise<boolean>((resolve) => {
        settle = resolve
      }),
  )
  candidate('held')
  await vi.waitFor(() => expect(settle).toBeTypeOf('function'))
  expect(nativeEventPredicatePhysicalCount(source)).toBe(1)
  const closed = new Error('WebSocket closed')
  rejectNative(closed)
  await expect(wait).rejects.toBe(closed)
  expect(nativeEventPredicatePhysicalCount(source)).toBe(1)
  if (!settle) throw new Error('Missing genuine pending callback')
  settle(true)
  await vi.waitFor(() => expect(nativeEventPredicatePhysicalCount(source)).toBe(0))
  expect(run.mock.calls.filter(([command]) => command === 'continueEventPredicate')).toHaveLength(0)
})

it('rejects a foreign socket candidate instead of invoking an authored predicate', async () => {
  const { socket, candidate } = predicateFixture(),
    predicate = vi.fn(() => true)
  const wait = socket.waitForEvent('framereceived', predicate)
  candidate('foreign', false, '2')
  await expect(wait).rejects.toThrow('identity differs')
  expect(predicate).not.toHaveBeenCalled()
})
