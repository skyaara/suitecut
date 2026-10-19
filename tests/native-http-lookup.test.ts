import { describe, expect, it } from 'vitest'

import { type NativeHTTPLookupEvent } from '../src/native-http-lookup-protocol.js'
import { NativeHTTPLookups, type NativeHTTPLookupTransport } from '../src/native-http-lookup.js'

function fixture() {
  const process = new AbortController(),
    page = new AbortController()
  let listener: ((event: NativeHTTPLookupEvent) => void) | undefined
  const replies: {
    event: NativeHTTPLookupEvent
    addresses: readonly { address: string; family: 4 | 6 }[]
    error: string
  }[] = []
  const transport: NativeHTTPLookupTransport = {
    processSignal: process.signal,
    signal: page.signal,
    onHTTPLookupMessage: (value) => {
      listener = value
      return () => {
        listener = undefined
      }
    },
    resolveLookup: (event, addresses, error) => {
      replies.push({ event, addresses, error })
      return Promise.resolve({})
    },
  }
  const controller = NativeHTTPLookups.forTransport(transport)
  const emit = (key: string, owner: number, event: 'request' | 'canceled' = 'request') =>
    listener?.({
      event,
      owner,
      generation: owner,
      apiContextId: 9,
      lookupKey: key,
      hostname: 'actual.example',
      port: 443,
    })
  return { process, page, controller, transport, emit, replies }
}
async function drain() {
  for (let i = 0; i < 8; ++i) await Promise.resolve()
}

describe('native HTTP host lookup callback ownership', () => {
  it('uses real host input and validated answer identities after a public page closes', async () => {
    const f = fixture()
    f.page.abort()
    expect(
      NativeHTTPLookups.forTransport({ ...f.transport, signal: new AbortController().signal }),
    ).toBe(f.controller)
    await f.controller.withLookup(
      (host, options) => {
        expect(host).toBe('actual.example')
        expect(options.signal.aborted).toBe(false)
        return [{ address: '127.0.0.2', family: 4 }]
      },
      async (key) => {
        f.emit(key, 1)
        await drain()
      },
    )
    expect(f.replies).toHaveLength(1)
    expect(f.replies[0]).toMatchObject({
      event: { owner: 1, generation: 1, apiContextId: 9 },
      addresses: [{ address: '127.0.0.2', family: 4 }],
      error: '',
    })
    expect(f.controller.physicalCount()).toBe(0)
    f.process.abort()
  })
  it('retains 128 physically unsettled callbacks after logical cancellation', async () => {
    const f = fixture()
    const settle: (() => void)[] = []
    let invoked = 0
    for (let owner = 1; owner <= 128; ++owner)
      await f.controller.withLookup(
        (_host, options) => {
          ++invoked
          return new Promise((resolve) => {
            settle.push(() => {
              expect(options.signal.aborted).toBe(true)
              resolve([{ address: '::1', family: 6 }])
            })
          })
        },
        (key) => {
          f.emit(key, owner)
          f.emit(key, owner, 'canceled')
          return Promise.resolve()
        },
      )
    expect(invoked).toBe(128)
    expect(f.controller.physicalCount()).toBe(128)
    expect(f.replies).toHaveLength(0)
    await f.controller.withLookup(
      () => {
        ++invoked
        return []
      },
      async (key) => {
        f.emit(key, 129)
        await drain()
      },
    )
    expect(invoked).toBe(128)
    expect(f.replies).toHaveLength(1)
    expect(f.replies[0]?.error).toContain('capacity')
    f.process.abort()
    expect(f.controller.physicalCount()).toBe(128)
    const restarted = fixture()
    await restarted.controller.withLookup(
      () => {
        ++invoked
        return []
      },
      async (key) => {
        restarted.emit(key, 1)
        await drain()
      },
    )
    expect(invoked).toBe(128)
    expect(restarted.replies[0]?.error).toContain('capacity')
    restarted.process.abort()
    for (const release of settle) release()
    await drain()
    expect(f.controller.physicalCount()).toBe(0)
    expect(f.replies).toHaveLength(1)
  })
  it('reports an actual callback rejection without inventing a network response', async () => {
    const f = fixture()
    await f.controller.withLookup(
      () => {
        throw new Error('actual callback rejected hostname')
      },
      async (key) => {
        f.emit(key, 1)
        await drain()
      },
    )
    expect(f.replies[0]?.addresses).toEqual([])
    expect(f.replies[0]?.error).toContain('actual callback rejected hostname')
    expect(f.controller.physicalCount()).toBe(0)
    f.process.abort()
  })
  it('releases a callback that throws an unprintable value', async () => {
    const f = fixture()
    await f.controller.withLookup(
      () => {
        throw Object.create(null)
      },
      async (key) => {
        f.emit(key, 1)
        await drain()
      },
    )
    expect(f.replies[0]?.error).toContain('unprintable')
    expect(f.controller.physicalCount()).toBe(0)
    f.process.abort()
  })
})
