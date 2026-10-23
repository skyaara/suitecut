// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { createLiveAudioTransport, LiveAudioBuffer } from '../src/live-audio.js'

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

describe('native audio HTTP input', () => {
  it('retires held input queues on transport close and rejects new work before reading PCM', async () => {
    const transport = await createLiveAudioTransport()
    const input = transport.createInput()
    const read = vi.spyOn(transport.buffer, 'read')
    await input.write(performance.now(), 48000, new AbortController().signal)
    await transport.close()
    expect(() => transport.createInput()).toThrow('transport is closed')
    await expect(input.write(performance.now(), 960, new AbortController().signal)).rejects.toThrow(
      'input is closed',
    )
    expect(read).toHaveBeenCalledOnce()
    input.close()
  })

  it('rejects closed, pre-aborted and over-capacity writes before allocating or consuming PCM', async () => {
    const transport = await createLiveAudioTransport()
    const input = transport.createInput()
    const read = vi.spyOn(transport.buffer, 'read')
    const canceled = new AbortController()
    const reason = new Error('original audio cancellation')
    canceled.abort(reason)
    try {
      await expect(input.write(performance.now(), 960, canceled.signal)).rejects.toBe(reason)
      await expect(
        input.write(performance.now(), 48001, new AbortController().signal),
      ).rejects.toThrow('encoder did not connect')
      expect(read).not.toHaveBeenCalled()
      input.close()
      await expect(
        input.write(performance.now(), 960, new AbortController().signal),
      ).rejects.toThrow('input is closed')
      expect(read).not.toHaveBeenCalled()
    } finally {
      input.close()
      await transport.close()
    }
  })

  it('retires a connected input when the encoder disconnects', async () => {
    const transport = await createLiveAudioTransport()
    const input = transport.createInput()
    const read = vi.spyOn(transport.buffer, 'read')
    try {
      const response = await fetch(input.url)
      await response.body?.cancel()
      await vi.waitFor(async () => {
        await expect(
          input.write(performance.now(), 0, new AbortController().signal),
        ).rejects.toThrow('input is closed')
      })
      read.mockClear()
      await expect(
        input.write(performance.now(), 960, new AbortController().signal),
      ).rejects.toThrow('input is closed')
      expect(read).not.toHaveBeenCalled()
    } finally {
      input.close()
      await transport.close()
    }
  })

  it('delivers queued native PCM to the encoder and closes its input', async () => {
    const transport = await createLiveAudioTransport()
    const input = transport.createInput()
    const at = performance.now()
    transport.buffer.push(at, tone(1234))
    try {
      await input.write(at, 960, new AbortController().signal)
      const response = await fetch(input.url)
      expect(response.status).toBe(200)
      const reader = response.body?.getReader()
      if (!reader) throw new Error('Missing PCM response')
      const chunk = await reader.read()
      expect(Buffer.from(chunk.value ?? []).equals(tone(1234))).toBe(true)
      input.close()
      expect((await reader.read()).done).toBe(true)
      expect((await fetch(`${transport.url}/invalid`)).status).toBe(404)
    } finally {
      input.close()
      await transport.close()
    }
  })
})
