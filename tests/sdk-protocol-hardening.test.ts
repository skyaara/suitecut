// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { MAX_NATIVE_RESPONSE_PAYLOAD, NativePacketDecoder } from '../src/native-protocol.js'

function packet(id: number, payload = Buffer.alloc(0)): Buffer {
  const json = Buffer.from(
    JSON.stringify({ version: 1, bytes: payload.length, type: 'response', id, ok: true }),
  )
  const length = Buffer.alloc(4)
  length.writeUInt32LE(json.length)
  return Buffer.concat([length, json, payload])
}

describe('SDK packet parser reentrancy and terminal errors', () => {
  it('preserves owned payloads across every split of adjacent packet boundaries', () => {
    const expected = [Buffer.from([0, 255, 42]), Buffer.alloc(0), Buffer.from([1, 2, 3, 4])]
    const bytes = Buffer.concat(expected.map((data, index) => packet(index + 1, data)))
    for (let split = 0; split <= bytes.length; split++) {
      const received: Buffer[] = []
      const decoder = new NativePacketDecoder((header, payload) => {
        if (header.type !== 'response') throw new Error('Wrong packet type')
        expect(header.id).toBe(received.length + 1)
        received.push(payload)
      })
      const first = Buffer.from(bytes.subarray(0, split))
      const second = Buffer.from(bytes.subarray(split))
      decoder.push(first)
      first.fill(0)
      decoder.push(second)
      second.fill(0)
      decoder.end()
      expect(received).toEqual(expected)
    }
  })

  it('delivers all bytes of the current read before receiver-fed reads and owns deferred input', () => {
    const received: number[] = []
    const nested = packet(3, Buffer.from([0, 255, 42]))
    const decoder = new NativePacketDecoder((header, payload) => {
      if (header.type !== 'response') throw new Error('Wrong packet type')
      received.push(header.id)
      if (header.id === 1) {
        decoder.push(nested)
        nested.fill(0)
      }
      if (header.id === 3) expect(payload).toEqual(Buffer.from([0, 255, 42]))
    })
    decoder.push(Buffer.concat([packet(1), packet(2)]))
    decoder.end()
    expect(received).toEqual([1, 2, 3])
  })

  it('bounds synchronous receiver-fed input and fails closed on overflow', () => {
    const decoder = new NativePacketDecoder(() => {
      for (let index = 0; index < 4097; index++) decoder.push(Buffer.from([0]))
    })
    expect(() => decoder.push(packet(1))).toThrow('queue limit')
    expect(() => decoder.push(packet(2))).toThrow('disposed')
  })

  it('drains a long receiver-fed chain in order without retaining consumed reads', () => {
    let count = 0
    const decoder = new NativePacketDecoder((header) => {
      if (header.type !== 'response') throw new Error('Wrong packet type')
      expect(header.id).toBe(++count)
      if (count < 10_000) decoder.push(packet(count + 1))
    })
    decoder.push(packet(1))
    decoder.end()
    expect(count).toBe(10_000)
  })

  it.each([0, 7])(
    'accepts synchronous receiver input after delivering %i body bytes',
    (bodyBytes) => {
      const received: number[] = []
      const decoder = new NativePacketDecoder((header) => {
        if (header.type !== 'response') throw new Error('Wrong packet type')
        received.push(header.id)
        if (header.id === 1) decoder.push(packet(2, Buffer.from([0, 255, 42])))
      })
      decoder.push(packet(1, Buffer.alloc(bodyBytes, 42)))
      decoder.end()
      expect(received).toEqual([1, 2])
    },
  )

  it.each([1, 5, 30, 63])(
    'preserves %i reentrantly supplied bytes until the next read',
    (split) => {
      const received: number[] = []
      const nested = packet(2, Buffer.alloc(7, 255))
      const decoder = new NativePacketDecoder((header) => {
        if (header.type !== 'response') throw new Error('Wrong packet type')
        received.push(header.id)
        if (header.id === 1) decoder.push(nested.subarray(0, split))
      })
      decoder.push(packet(1))
      decoder.push(nested.subarray(split))
      decoder.end()
      expect(received).toEqual([1, 2])
    },
  )

  it('fails closed on malformed framing, rather than accepting a later stream', () => {
    const decoder = new NativePacketDecoder(() => {
      throw new Error('Unexpected packet')
    })
    expect(() => decoder.push(Buffer.from([255, 255, 255, 255]))).toThrow('header length')
    expect(() => decoder.push(packet(1))).toThrow('disposed')
    expect(() => decoder.end()).not.toThrow()
  })

  it('releases an incomplete maximum response when EOF is reported', () => {
    const json = Buffer.from(
      JSON.stringify({
        version: 1,
        bytes: MAX_NATIVE_RESPONSE_PAYLOAD,
        type: 'response',
        id: 1,
        ok: true,
      }),
    )
    const length = Buffer.alloc(4)
    length.writeUInt32LE(json.length)
    const decoder = new NativePacketDecoder(() => {
      throw new Error('Unexpected packet')
    })
    decoder.push(Buffer.concat([length, json, Buffer.from([1])]))
    expect(() => decoder.end()).toThrow('Truncated')
    expect(() => decoder.push(packet(1))).toThrow('disposed')
  })

  it('preserves the receiver error while retiring the parser', () => {
    const failure = new Error('Receiver failed')
    const decoder = new NativePacketDecoder(() => {
      throw failure
    })
    expect(() => decoder.push(packet(1))).toThrow(failure)
    expect(() => decoder.push(packet(2))).toThrow('disposed')
  })
})
