// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { NativeAudioAssembler } from '../src/native-audio.js'
import {
  NativePacketDecoder,
  NativeNetworkRequestSchema,
  NativeNetworkResponseSchema,
  type NativePacket,
} from '../src/native-protocol.js'

function packet(header: object, data = Buffer.alloc(0)): Buffer {
  const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
  const length = Buffer.alloc(4)
  length.writeUInt32LE(json.length)
  return Buffer.concat([length, json, data])
}

describe('native IPC boundary', () => {
  it('preserves inline resource and long URLs whole in observed requests and responses', () => {
    const url = `data:image/svg+xml;base64,${'A'.repeat(32_768)}`
    const request = {
      id: 'inline-1',
      requestId: '1',
      session: '',
      frameId: 'frame-1',
      url,
      method: 'GET',
      resourceType: 'Image',
      headers: {},
      isNavigationRequest: false,
    }
    const response = {
      url,
      status: 200,
      statusText: 'OK',
      headers: {},
      requestId: '1',
      session: '',
      frameId: 'frame-1',
      method: 'GET',
    }
    expect(NativeNetworkRequestSchema.parse(request).url).toBe(url)
    expect(NativeNetworkResponseSchema.parse(response).url).toBe(url)
    // Long GET form submissions and large inline resources stay whole.
    const long = `https://example.test/?q=${'a'.repeat(2_097_152 + 10)}`
    expect(NativeNetworkRequestSchema.parse({ ...request, url: long }).url).toBe(long)
    expect(NativeNetworkResponseSchema.parse({ ...response, url: long }).url).toBe(long)
    const inline = `data:image/png;base64,${'A'.repeat(1_048_576 + 10)}`
    expect(NativeNetworkRequestSchema.parse({ ...request, url: inline }).url).toBe(inline)
    expect(NativeNetworkResponseSchema.parse({ ...response, url: inline }).url).toBe(inline)
  })
  it('preserves proven viewport geometry and null resize paints alongside legacy frame packets', () => {
    const headers: NativePacket[] = []
    const decoder = new NativePacketDecoder((header) => headers.push(header))
    const base = { type: 'frame', timestampMs: 1, width: 8, height: 6, dropped: 0 }
    const data = Buffer.alloc(8 * 6 * 4)
    decoder.push(packet(base, data))
    decoder.push(
      packet({ ...base, viewport: null, contentRect: { x: 2, y: 0, width: 3, height: 6 } }, data),
    )
    decoder.push(
      packet(
        {
          ...base,
          viewport: { width: 390, height: 844 },
          contentRect: { x: 2, y: 0, width: 3, height: 6 },
        },
        data,
      ),
    )
    decoder.end()
    expect(headers[0]).not.toHaveProperty('viewport')
    expect(headers[1]).toMatchObject({ viewport: null })
    expect(headers[2]).toMatchObject({
      viewport: { width: 390, height: 844 },
      contentRect: { x: 2, y: 0, width: 3, height: 6 },
    })
  })

  it.each([
    { x: 7, y: 0, width: 2, height: 6 },
    { x: 0, y: 5, width: 8, height: 2 },
    { x: -1, y: 0, width: 8, height: 6 },
  ])('rejects crop rectangles outside actual frame storage: %j', (contentRect) => {
    const decoder = new NativePacketDecoder(() => undefined)
    expect(() =>
      decoder.push(
        packet(
          { type: 'frame', timestampMs: 1, width: 8, height: 6, dropped: 0, contentRect },
          Buffer.alloc(8 * 6 * 4),
        ),
      ),
    ).toThrow()
  })
  it('requires a page identity and empty payload for a page-crash packet', () => {
    const received: NativePacket[] = []
    // The default page crashes like any other: its crash no longer ends the browser.
    for (const pageId of [1, 2])
      new NativePacketDecoder((header) => received.push(header)).push(
        packet({ type: 'pagecrash', pageId, error: 'Browser renderer terminated' }),
      )
    expect(received).toMatchObject([
      { type: 'pagecrash', pageId: 1, bytes: 0 },
      { type: 'pagecrash', pageId: 2, bytes: 0 },
    ])
    for (const pageId of [undefined, 0])
      expect(() =>
        new NativePacketDecoder(() => undefined).push(
          packet({ type: 'pagecrash', pageId, error: 'Browser renderer terminated' }),
        ),
      ).toThrow()
    expect(() =>
      new NativePacketDecoder(() => undefined).push(
        packet(
          { type: 'pagecrash', pageId: 2, error: 'Browser renderer terminated' },
          Buffer.from('x'),
        ),
      ),
    ).toThrow()
  })

  it('accepts fragmented and coalesced packets without losing frame bytes', () => {
    const received: { header: NativePacket; data: Buffer }[] = []
    const decoder = new NativePacketDecoder((header, data) => received.push({ header, data }))
    const data = Buffer.from([255, 0, 42, 255])
    const bytes = Buffer.concat([
      packet({ type: 'ready', timestampMs: 2, cefVersion: 'test' }),
      packet({ type: 'frame', timestampMs: 3, width: 1, height: 1, dropped: 0 }, data),
      packet({ type: 'response', id: 1, ok: true }),
    ])
    for (let i = 0; i < bytes.length; i += 3) decoder.push(bytes.subarray(i, i + 3))
    decoder.end()
    expect(received.map((p) => p.header.type)).toEqual(['ready', 'frame', 'response'])
    expect(received[1]?.data).toEqual(data)
  })

  it('rejects header lengths too short for a JSON object', () => {
    const size = Buffer.alloc(4)
    size.writeUInt32LE(1)
    expect(() => new NativePacketDecoder(() => undefined).push(size)).toThrow('header length')
  })

  it('allocates header and payload storage only as their bytes arrive', () => {
    const allocations = vi.spyOn(Buffer, 'allocUnsafe')
    try {
      // A corrupt length word must not reserve 4 GiB before any header bytes exist.
      const corrupt = Buffer.alloc(4)
      corrupt.writeUInt32LE(0xffffffff)
      const decoder = new NativePacketDecoder(() => undefined)
      decoder.push(Buffer.concat([corrupt, Buffer.from('{"vers')]))
      expect(Math.max(...allocations.mock.calls.map(([size]) => size))).toBeLessThan(64)
      expect(() => decoder.end()).toThrow('Truncated')
      // A large payload fragmented across irregular reads still arrives byte for byte.
      allocations.mockClear()
      const received: Buffer[] = []
      const frames = new NativePacketDecoder((_, data) => received.push(data))
      const data = Buffer.alloc(64 * 64 * 4)
      for (let i = 0; i < data.length; i++) data[i] = (i * 31) % 251
      const bytes = packet(
        { type: 'frame', timestampMs: 1, width: 64, height: 64, dropped: 0 },
        data,
      )
      const split = bytes.length - data.length + 100
      frames.push(bytes.subarray(0, split))
      expect(Math.max(...allocations.mock.calls.map(([size]) => size))).toBeLessThan(1024)
      for (let i = split; i < bytes.length; i += 977) frames.push(bytes.subarray(i, i + 977))
      frames.end()
      expect(received).toHaveLength(1)
      expect(received[0]?.equals(data)).toBe(true)
    } finally {
      allocations.mockRestore()
    }
  })

  it('validates I420 plane sizes and even dimensions', () => {
    const received: Buffer[] = []
    const decoder = new NativePacketDecoder((_, data) => received.push(data))
    decoder.push(
      packet(
        { type: 'frame', pixelFormat: 'i420', timestampMs: 1, width: 2, height: 2, dropped: 0 },
        Buffer.alloc(6),
      ),
    )
    expect(received).toHaveLength(1)
    expect(() =>
      decoder.push(
        packet(
          { type: 'frame', pixelFormat: 'i420', timestampMs: 2, width: 1, height: 2, dropped: 0 },
          Buffer.alloc(3),
        ),
      ),
    ).toThrow('even dimensions')
  })

  it.each([
    { type: 'ready', timestampMs: 1, cefVersion: 'test', version: 2 },
    { type: 'frame', timestampMs: 1, width: 3840, height: 2160, dropped: 0 },
    { type: 'audio', timestampMs: 1, frames: 960 },
    { type: 'frame', timestampMs: -1, width: 1, height: 1, dropped: 0 },
    { type: 'response', id: 0, ok: true },
  ])('rejects malformed metadata %j', (header) => {
    expect(() => new NativePacketDecoder(() => undefined).push(packet(header))).toThrow()
  })

  it('rejects truncated payloads and partial length words at EOF', () => {
    const full = packet({ type: 'audio', timestampMs: 1, frames: 1 }, Buffer.alloc(4))
    for (const bytes of [full.subarray(0, -1), Buffer.from([1, 2])]) {
      const decoder = new NativePacketDecoder(() => undefined)
      decoder.push(bytes)
      expect(() => decoder.end()).toThrow('Truncated')
    }
  })

  it('stops decoding a coalesced batch when the receiver disposes the process decoder', () => {
    const received: string[] = []
    const decoder = new NativePacketDecoder((header) => {
      received.push(header.type)
      decoder.dispose()
    })
    decoder.push(
      Buffer.concat([
        packet({ type: 'ready', timestampMs: 1, cefVersion: 'test' }),
        packet({ type: 'audio', timestampMs: 2, frames: 1 }, Buffer.alloc(4)),
      ]),
    )
    expect(received).toEqual(['ready'])
    decoder.end()
    expect(() => decoder.push(Buffer.from([1]))).toThrow('disposed')
  })

  it('keeps delivered buffers stable across subsequent packets', () => {
    const output: Buffer[] = []
    const decoder = new NativePacketDecoder((_, payload) => output.push(payload))
    for (const fill of [1, 2, 3])
      decoder.push(packet({ type: 'audio', timestampMs: 1, frames: 1 }, Buffer.alloc(4, fill)))
    expect(output.map((data) => [...data])).toEqual([
      [1, 1, 1, 1],
      [2, 2, 2, 2],
      [3, 3, 3, 3],
    ])
  })
})

describe('native PCM reblocking', () => {
  it('preserves samples and timestamps across irregular callback boundaries', () => {
    const result: { at: number; pcm: Buffer }[] = []
    const assembler = new NativeAudioAssembler((at, pcm) => result.push({ at, pcm }))
    const input = Buffer.alloc(3840 * 3)
    for (let i = 0; i < input.length; i++) input[i] = i % 251
    let offset = 0
    for (const frames of [128, 1024, 512, 1216]) {
      assembler.push({
        data: input.subarray(offset * 4, (offset + frames) * 4),
        frames,
        timestampMs: 100 + offset / 48,
      })
      offset += frames
    }
    expect(result.map((p) => p.at)).toEqual([100, 120, 140])
    expect(Buffer.concat(result.map((p) => p.pcm))).toEqual(input)
  })

  it('discards partial old audio after source switches or discontinuities', () => {
    const output: Buffer[] = []
    const assembler = new NativeAudioAssembler((_, pcm) => output.push(pcm))
    assembler.push({ data: Buffer.alloc(1920, 1), frames: 480, timestampMs: 0 })
    assembler.push({ data: Buffer.alloc(3840, 2), frames: 960, timestampMs: 1000 })
    assembler.push({ data: Buffer.alloc(1920, 3), frames: 480, timestampMs: 1020 })
    assembler.clear()
    assembler.push({ data: Buffer.alloc(3840, 4), frames: 960, timestampMs: 1030 })
    expect(output).toEqual([Buffer.alloc(3840, 2), Buffer.alloc(3840, 4)])
  })
})

it('permits whole WebSocket frames in predicate snapshots', () => {
  const header = {
    type: 'eventpredicate',
    pageId: 2,
    id: 7,
    generation: 1,
    token: 1,
    clientToken: 'socket-test',
    sequence: 1,
  }
  const bytes = Buffer.alloc(22_369_624, 32)
  const received: Buffer[] = []
  const decoder = new NativePacketDecoder((_, data) => received.push(data))
  decoder.push(packet({ ...header, event: 'framereceived' }, bytes))
  decoder.end()
  expect(received[0]?.equals(bytes)).toBe(true)
})
