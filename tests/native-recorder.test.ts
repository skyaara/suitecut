import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough, Writable } from 'node:stream'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { createNativeVideoRecorder } from '../src/native-recorder.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

describe('native recording cadence and shutdown', () => {
  let child: ChildProcess
  let frames: Buffer[]
  const source = { width: 2, height: 2, pixelFormat: 'bgra' as const, framesPerSecond: 30 as const }
  const start = () =>
    createNativeVideoRecorder({
      source,
      ffmpegPath: '/ffmpeg',
      outputPath: '/test.webm',
      size: { width: 2, height: 2 },
    })
  beforeEach(() => {
    child = new ChildProcess()
    vi.spyOn(child, 'kill').mockReturnValue(true)
    frames = []
    child.stdin = new PassThrough()
    child.stderr = new PassThrough()
    child.stdin.on('data', (frame: Buffer) => frames.push(Buffer.from(frame)))
    child.stdin.on('finish', () => child.emit('close', 0))
    vi.mocked(spawn).mockReturnValue(child)
  })
  afterEach(() => vi.restoreAllMocks())

  it('preserves the first-frame origin and holds idle video until recording ends', async () => {
    const recorder = start()
    recorder.push(Buffer.alloc(16, 1), 100)
    await vi.waitFor(() => expect(frames).toHaveLength(1))
    recorder.push(Buffer.alloc(16, 2), 200)
    await vi.waitFor(() => expect(frames).toHaveLength(4))
    const stop = recorder.stop(300)
    expect(recorder.stop(400)).toBe(stop)
    await stop
    expect(recorder.firstAtMs).toBe(100)
    expect(frames.map((frame) => frame[0])).toEqual([1, 1, 1, 2, 2, 2, 2])
  })

  it('bounds backpressure by replacing queued frames without shortening duration', async () => {
    let release: (() => void) | undefined
    let first = true
    child.stdin = new Writable({
      highWaterMark: 1,
      write(chunk: Buffer, _encoding, callback) {
        frames.push(Buffer.from(chunk))
        if (first) {
          first = false
          release = () => callback()
        } else callback()
      },
    })
    child.stdin.on('finish', () => child.emit('close', 0))
    const recorder = start()
    recorder.push(Buffer.alloc(16, 1), 0)
    for (let i = 1; i <= 30; i++) recorder.push(Buffer.alloc(16, i), (i * 1000) / 30)
    expect(frames).toHaveLength(1)
    release?.()
    await vi.waitFor(() => expect(frames).toHaveLength(31))
    await recorder.stop(1000)
    expect(frames).toHaveLength(31)
    expect(frames.at(-1)?.[0]).toBe(30)
    expect(frames.slice(0, -1).every((frame) => frame[0] === 1)).toBe(true)
  })

  it('flushes the final hold when stopping during the very first blocked write', async () => {
    let release: (() => void) | undefined
    let first = true
    child.stdin = new Writable({
      highWaterMark: 1,
      write(chunk: Buffer, _encoding, callback) {
        frames.push(Buffer.from(chunk))
        if (first) {
          first = false
          release = () => callback()
        } else callback()
      },
    })
    child.stdin.on('finish', () => child.emit('close', 0))
    const recorder = start()
    recorder.push(Buffer.alloc(16, 7), 0)
    const stop = recorder.stop(1000)
    release?.()
    await stop
    expect(frames).toHaveLength(31)
  })

  it('rejects encoder failures instead of reporting a successful recording', async () => {
    const recorder = start()
    const rejected = expect(recorder.failure).rejects.toThrow('encoder exited')
    child.emit('close', 1)
    await rejected
    await expect(recorder.stop(0)).rejects.toThrow('encoder exited')
  })

  it('allows recording finalization beyond the streaming five-second timeout', async () => {
    vi.useFakeTimers()
    try {
      if (!child.stdin) throw new Error('Missing test stdin')
      child.stdin.removeAllListeners('finish')
      child.stdin.on('finish', () => {
        setTimeout(() => child.emit('close', 0), 10_000)
      })
      const recorder = start()
      recorder.push(Buffer.alloc(16), 0)
      await vi.advanceTimersByTimeAsync(0)
      const stop = recorder.stop(100)
      await vi.advanceTimersByTimeAsync(10_001)
      await expect(stop).resolves.toBeUndefined()
    } finally {
      vi.useRealTimers()
    }
  })
})
