import { describe, expect, it } from 'vitest'

import { NativeAudioAssembler } from '../src/native-audio.js'
import { NativePacketDecoder, type NativePacket } from '../src/native-protocol.js'

function packet(header: object, data = Buffer.alloc(0)): Buffer {
  const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
  const length = Buffer.alloc(4)
  length.writeUInt32LE(json.length)
  return Buffer.concat([length, json, data])
}

describe('native IPC boundary', () => {
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

  it('rejects oversized headers before allocating them', () => {
    const size = Buffer.alloc(4)
    size.writeUInt32LE(0xffffffff)
    expect(() => new NativePacketDecoder(() => undefined).push(size)).toThrow('header length')
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
    { type: 'response', id: 1, ok: true, bytes: 2_000_000 },
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
