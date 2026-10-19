import { Buffer } from 'node:buffer'

import { describe, expect, it, vi } from 'vitest'

import { NativeNetworkBodyReceiver, NativeNetworkBodyArchive } from '../src/native-network-body.js'

describe('native request-body byte publications', () => {
  it('keeps exact binary bytes until consumption and acknowledges each stage once', () => {
    const acknowledge = vi.fn(() => Promise.resolve())
    const receiver = new NativeNetworkBodyReceiver(acknowledge, () => undefined)
    receiver.receive({ bodyId: '1', offset: 0, totalBytes: 4 }, Buffer.from([0, 255]))
    expect(() => receiver.take('1')).toThrow('incomplete')
    receiver.receive({ bodyId: '1', offset: 2, totalBytes: 4 }, Buffer.from([13, 10]))
    expect(acknowledge.mock.calls).toEqual([['1', 'received']])
    expect(receiver.diagnostics()).toEqual({ owners: 1, retainedBytes: 4 })
    const body = receiver.take('1')
    expect(body).toEqual(Buffer.from([0, 255, 13, 10]))
    expect(receiver.take('1')).toBe(body)
    expect(acknowledge.mock.calls).toEqual([
      ['1', 'received'],
      ['1', 'taken'],
    ])
    expect(receiver.diagnostics()).toEqual({ owners: 0, retainedBytes: 0 })
  })
  it('bounds bytes received but not yet consumed even after receipt acknowledgment', () => {
    const receiver = new NativeNetworkBodyReceiver(
      () => Promise.resolve(),
      () => undefined,
    )
    const chunk = Buffer.alloc(24 * 1024, 255)
    for (let id = 1; id <= 8; id++) {
      for (let offset = 0; offset < 2 * 1024 * 1024; offset += chunk.length) {
        receiver.receive(
          { bodyId: String(id), offset, totalBytes: 2 * 1024 * 1024 },
          chunk.subarray(0, Math.min(chunk.length, 2 * 1024 * 1024 - offset)),
        )
      }
    }
    expect(receiver.diagnostics()).toEqual({ owners: 8, retainedBytes: 16 * 1024 * 1024 })
    expect(() =>
      receiver.receive({ bodyId: '9', offset: 0, totalBytes: 1 }, Buffer.from([1])),
    ).toThrow('capacity')
    receiver.take('1')
    receiver.receive({ bodyId: '9', offset: 0, totalBytes: 1 }, Buffer.from([0]))
    receiver.close()
    expect(receiver.diagnostics()).toEqual({ owners: 0, retainedBytes: 0 })
  })
  it('cancels partial publications and rejects duplicate, reordered, or oversized bytes', () => {
    const receiver = new NativeNetworkBodyReceiver(
      () => Promise.resolve(),
      () => undefined,
    )
    receiver.receive({ bodyId: '1', offset: 0, totalBytes: 3 }, Buffer.from([1]))
    expect(() =>
      receiver.receive({ bodyId: '1', offset: 0, totalBytes: 3 }, Buffer.from([2])),
    ).toThrow('mismatch')
    receiver.receive({ bodyId: '1', offset: 0, totalBytes: 0, canceled: true }, Buffer.alloc(0))
    expect(receiver.diagnostics()).toEqual({ owners: 0, retainedBytes: 0 })
    expect(() => receiver.take('1')).toThrow('unavailable')
    expect(() =>
      receiver.receive({ bodyId: '2', offset: 1, totalBytes: 3 }, Buffer.from([1])),
    ).toThrow('offset')
    expect(() =>
      receiver.receive({ bodyId: '3', offset: 0, totalBytes: 25000 }, Buffer.alloc(25000)),
    ).toThrow('24 KiB')
  })
  it('bounds replay bytes independently from pending delivery and preserves held byte identity', () => {
    const receiver = new NativeNetworkBodyReceiver(
      () => Promise.resolve(),
      () => undefined,
    )
    const chunk = Buffer.alloc(24 * 1024, 255)
    let held: Buffer | undefined
    for (let id = 1; id <= 10; id++) {
      for (let offset = 0; offset < 2 * 1024 * 1024; offset += chunk.length)
        receiver.receive(
          { bodyId: String(id), offset, totalBytes: 2 * 1024 * 1024 },
          chunk.subarray(0, Math.min(chunk.length, 2 * 1024 * 1024 - offset)),
        )
      const actual = receiver.take(String(id))
      if (id === 1) held = actual
    }
    expect(receiver.archiveDiagnostics()).toEqual({ bodies: 8, retainedBytes: 16 * 1024 * 1024 })
    expect(receiver.take('1')).toBe(held)
    receiver.close()
    expect(receiver.archiveDiagnostics()).toEqual({ bodies: 0, retainedBytes: 0 })
  })
  it('shares byte quotas across pages and releases a closed page reservation', () => {
    const archive = new NativeNetworkBodyArchive()
    const first = new NativeNetworkBodyReceiver(
      () => Promise.resolve(),
      () => undefined,
      archive,
    )
    const second = new NativeNetworkBodyReceiver(
      () => Promise.resolve(),
      () => undefined,
      archive,
    )
    for (let id = 1; id <= 8; ++id) {
      const receiver = id % 2 ? first : second
      receiver.receive(
        { bodyId: String(id), offset: 0, totalBytes: 2 * 1024 * 1024 },
        Buffer.from([id]),
      )
    }
    expect(() =>
      first.receive({ bodyId: '9', offset: 0, totalBytes: 1 }, Buffer.from([9])),
    ).toThrow('process network body receiver capacity')
    second.close()
    first.receive({ bodyId: '9', offset: 0, totalBytes: 1 }, Buffer.from([9]))
    expect(first.take('9')).toEqual(Buffer.from([9]))
    first.close()
  })
  it('rejects invalid first chunks without reserving shared pending bytes or owners', () => {
    const archive = new NativeNetworkBodyArchive()
    const acknowledge = vi.fn(() => Promise.resolve())
    const first = new NativeNetworkBodyReceiver(acknowledge, () => undefined, archive)
    const second = new NativeNetworkBodyReceiver(acknowledge, () => undefined, archive)
    for (let id = 1; id <= 129; id++) {
      expect(() =>
        first.receive(
          { bodyId: String(id), offset: 0, totalBytes: 2 * 1024 * 1024 },
          Buffer.alloc(0),
        ),
      ).toThrow('mismatch')
      expect(() =>
        first.receive({ bodyId: String(id), offset: 0, totalBytes: 1 }, Buffer.from([1, 2])),
      ).toThrow('mismatch')
    }
    expect(first.diagnostics()).toEqual({ owners: 0, retainedBytes: 0 })
    expect(first.archiveDiagnostics()).toEqual({ bodies: 0, retainedBytes: 0 })
    expect(acknowledge).not.toHaveBeenCalled()
    for (let id = 1; id <= 8; id++)
      second.receive(
        { bodyId: String(id), offset: 0, totalBytes: 2 * 1024 * 1024 },
        Buffer.from([id]),
      )
    expect(second.diagnostics()).toEqual({ owners: 8, retainedBytes: 16 * 1024 * 1024 })
    first.close()
    second.close()
  })
  it('releases the receiver when a real acknowledgment fails after completion', async () => {
    const failure = vi.fn()
    const receiver = new NativeNetworkBodyReceiver(
      () => Promise.reject(new Error('native closed')),
      failure,
    )
    receiver.receive({ bodyId: '1', offset: 0, totalBytes: 0 }, Buffer.alloc(0))
    await Promise.resolve()
    expect(failure).toHaveBeenCalledOnce()
    expect(receiver.diagnostics()).toEqual({ owners: 0, retainedBytes: 0 })
    expect(() => receiver.take('1')).toThrow('closed')
  })
})
