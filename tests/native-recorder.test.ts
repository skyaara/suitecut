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
    void recorder.push(Buffer.alloc(16, 1), 100)
    await vi.waitFor(() => expect(frames).toHaveLength(1))
    void recorder.push(Buffer.alloc(16, 2), 200)
    await vi.waitFor(() => expect(frames).toHaveLength(3))
    const stop = recorder.stop(300)
    expect(recorder.stop(400)).toBe(stop)
    await stop
    expect(recorder.firstAtMs).toBe(100)
    expect(frames.map((frame) => frame[0])).toEqual([1, 1, 1, 2, 2, 2])
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
    void recorder.push(Buffer.alloc(16, 1), 0)
    for (let i = 1; i <= 30; i++) void recorder.push(Buffer.alloc(16, i), (i * 1000) / 30)
    expect(frames).toHaveLength(1)
    release?.()
    await vi.waitFor(() => expect(frames).toHaveLength(30))
    await recorder.stop((31 * 1000) / 30)
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
    void recorder.push(Buffer.alloc(16, 7), 0)
    const stop = recorder.stop(1000)
    release?.()
    await stop
    expect(frames).toHaveLength(30)
  })

  it('samples the newest capture at or before each tick rather than borrowing future pixels', async () => {
    const recorder = createNativeVideoRecorder({
      source,
      framesPerSecond: 100,
      ffmpegPath: '/ffmpeg',
      outputPath: '/test.webm',
      size: { width: 2, height: 2 },
    })
    await recorder.push(Buffer.alloc(16, 1), 100)
    await recorder.push(Buffer.alloc(16, 2), 117)
    await recorder.push(Buffer.alloc(16, 3), 123)
    await recorder.push(Buffer.alloc(16, 4), 145)
    await recorder.push(Buffer.alloc(16, 9), 130) // Obsolete delayed callback.
    await recorder.stop(150)
    expect(frames.map((frame) => frame[0])).toEqual([1, 1, 2, 3, 3])
  })

  it.each([1, 24, 47, 59, 90, 120])(
    'holds one captured frame for exactly one second at %i FPS',
    async (fps) => {
      const recorder = createNativeVideoRecorder({
        source,
        framesPerSecond: fps,
        ffmpegPath: '/ffmpeg',
        outputPath: '/test.webm',
        size: { width: 2, height: 2 },
      })
      await recorder.push(Buffer.alloc(16, 7), 250)
      await recorder.stop(1250)
      expect(frames).toHaveLength(fps)
      expect(frames.every((frame) => frame[0] === 7)).toBe(true)
    },
  )

  it('drops excess captures when resampling 120 FPS to 24 FPS', async () => {
    const recorder = createNativeVideoRecorder({
      source: { ...source, framesPerSecond: 120 },
      framesPerSecond: 24,
      ffmpegPath: '/ffmpeg',
      outputPath: '/test.webm',
      size: { width: 2, height: 2 },
    })
    for (let index = 0; index < 120; index++)
      await recorder.push(Buffer.alloc(16, index), (index * 1000) / 120)
    await recorder.stop(1000)
    expect(frames.map((frame) => frame[0])).toEqual(
      Array.from({ length: 24 }, (_, index) => index * 5),
    )
  })

  it('preserves every 120 FPS capture when the recording origin is nonzero', async () => {
    const recorder = createNativeVideoRecorder({
      source: { ...source, framesPerSecond: 120 },
      ffmpegPath: '/ffmpeg',
      outputPath: '/test.webm',
      size: { width: 2, height: 2 },
    })
    for (let index = 0; index < 120; index++)
      await recorder.push(Buffer.alloc(16, index), 250 + (index * 1000) / 120)
    await recorder.stop(1250)
    expect(frames.map((frame) => frame[0])).toEqual(
      Array.from({ length: 120 }, (_, index) => index),
    )
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
      void recorder.push(Buffer.alloc(16), 0)
      await vi.advanceTimersByTimeAsync(0)
      const stop = recorder.stop(100)
      await vi.advanceTimersByTimeAsync(10_001)
      await expect(stop).resolves.toBeUndefined()
    } finally {
      vi.useRealTimers()
    }
  })
})
