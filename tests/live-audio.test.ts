import { once } from 'node:events'
import { runInNewContext } from 'node:vm'

import { describe, expect, it } from 'vitest'
import { WebSocket } from 'ws'

import { createLiveAudioTransport, LiveAudioBuffer } from '../src/live-audio.js'
import { tabAudioWorklet } from '../src/tab-audio-extension.js'

function tone(value: number): Buffer {
  const pcm = Buffer.alloc(3840)
  for (let i = 0; i < pcm.length; i += 2) pcm.writeInt16LE(value, i)
  return pcm
}

describe('live audio timeline', () => {
  it('places timestamped samples and fills missing time with silence', () => {
    const audio = new LiveAudioBuffer()
    const now = performance.now()
    audio.push(now + 10, tone(1000))
    const output = audio.read(now, 1920)
    expect(output.subarray(0, 480 * 4).equals(Buffer.alloc(480 * 4))).toBe(true)
    expect(output.subarray(480 * 4, 1440 * 4).equals(tone(1000))).toBe(true)
    expect(output.subarray(1440 * 4).equals(Buffer.alloc(480 * 4))).toBe(true)
  })

  it('keeps the tail of a chunk across video frames', () => {
    const audio = new LiveAudioBuffer()
    const now = performance.now()
    audio.push(now, tone(2000))
    expect(audio.read(now, 480).readInt16LE(0)).toBe(2000)
    expect(audio.read(now + 10, 480).readInt16LE(0)).toBe(2000)
    expect(audio.read(now + 20, 480).readInt16LE(0)).toBe(0)
  })

  it('drops audio outside the bounded live window and malformed packets', () => {
    const audio = new LiveAudioBuffer()
    const now = performance.now()
    audio.push(now - 11_000, tone(2000))
    audio.push(now + 1000, tone(2000))
    audio.push(Number.NaN, tone(2000))
    audio.push(now, Buffer.alloc(10))
    expect(audio.read(now, 960).equals(Buffer.alloc(3840))).toBe(true)
  })

  it('retains audio during catch-up and discards it after a common timeline skip', () => {
    const audio = new LiveAudioBuffer()
    const now = performance.now()
    audio.push(now - 6000, tone(1234))
    audio.push(now, tone(2345))
    expect(audio.read(now - 6000, 960).readInt16LE(0)).toBe(1234)
    expect(audio.read(now, 960).readInt16LE(0)).toBe(2345)
    expect(audio.read(now - 6000, 960).readInt16LE(0)).toBe(0)
  })

  it('clears queued sound on selection, pause, or reconnect', () => {
    const audio = new LiveAudioBuffer()
    const now = performance.now()
    audio.push(now, tone(2000))
    audio.clear()
    expect(audio.read(now, 960).equals(Buffer.alloc(3840))).toBe(true)
  })
})

describe('audio WebSocket transport', () => {
  it('accepts binary PCM, rejects stale connections, and closes active sockets on shutdown', async () => {
    const packets: number[] = []
    const transport = await createLiveAudioTransport(
      (generation, timestamp, pcm) => {
        expect(timestamp).toBe(1234)
        expect(pcm.length).toBe(3840)
        packets.push(generation)
      },
      () => undefined,
    )
    const activate = async (generation: number) => {
      const ready = transport.activate(generation)
      await fetch(`${transport.url}/status/${generation}/ok`, { method: 'POST' })
      await ready
    }
    const connect = (generation: number) =>
      new WebSocket(`${transport.url.replace('http:', 'ws:')}/pcm/${generation}`)
    try {
      await activate(1)
      const first = connect(1)
      await once(first, 'open')
      const packet = Buffer.alloc(3848)
      packet.writeDoubleLE(1234)
      const ack = once(first, 'message')
      first.send(packet)
      await ack
      expect(packets).toEqual([1])
      const closed = once(first, 'close')
      await activate(2)
      await closed
      const stale = connect(1)
      const rejected = new Promise<void>((resolve) => stale.once('error', () => resolve()))
      await rejected
      const second = connect(2)
      await once(second, 'open')
      const malformedClosed = once(second, 'close')
      second.send(Buffer.alloc(3849))
      await malformedClosed
      expect(packets).toEqual([1])
      const third = connect(2)
      await once(third, 'open')
      const shutdown = once(third, 'close')
      await transport.close()
      await shutdown
    } finally {
      await transport.close().catch(() => undefined)
    }
  })
})

it('bounds the worklet queue during a stalled consumer and resumes with current audio', () => {
  const messages: { time: number }[] = []
  let Processor: new () => {
    process(inputs: Float32Array[][]): boolean
    port: { onmessage: () => void }
  }
  const sandbox = {
    AudioWorkletProcessor: class {
      port = {
        postMessage: (value: { time: number }) => messages.push(value),
        onmessage: () => undefined,
      }
    },
    registerProcessor: (_name: string, value: typeof Processor) => {
      Processor = value
    },
    currentFrame: 0,
    sampleRate: 48000,
  }
  runInNewContext(tabAudioWorklet, sandbox)
  const processor = new Processor!()
  const input = [[new Float32Array(128)]]
  for (let i = 0; i < 10000; i++) {
    sandbox.currentFrame += 128
    processor.process(input)
  }
  expect(messages).toHaveLength(4)
  processor.port.onmessage()
  for (let i = 0; i < 8; i++) {
    sandbox.currentFrame += 128
    processor.process(input)
  }
  expect(messages).toHaveLength(5)
  expect(messages[4]!.time).toBeGreaterThan(26)
})
