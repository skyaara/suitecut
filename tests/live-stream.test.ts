import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { LiveAudioBuffer } from '../src/live-audio.js'
import {
  createLiveStreamAttempt as createLiveStream,
  createLiveStream as createReconnectingStream,
} from '../src/live-stream.js'
import { SuiteCutStreamOptionsSchema } from '../src/schemas.js'

vi.mock('node:child_process', async (importOriginal) => ({
  ...(await importOriginal<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

vi.mock('node:timers/promises', () => ({
  setTimeout: (milliseconds: number) =>
    new Promise<void>((resolve) => setTimeout(resolve, milliseconds)),
}))

describe('live stream publishing', () => {
  let child: ChildProcess
  let input: PassThrough
  let progress: PassThrough
  let frames: Buffer[]

  beforeEach(() => {
    vi.useFakeTimers()
    vi.mocked(spawn).mockReset()
    child = new ChildProcess()
    input = new PassThrough()
    progress = new PassThrough()
    child.stdin = input
    child.stdout = new PassThrough()
    child.stderr = new PassThrough()
    Object.defineProperty(child, 'stdio', {
      value: [input, child.stdout, child.stderr, progress, null],
    })
    frames = []
    input.on('data', (chunk: Buffer) => frames.push(chunk))
    const ownedChild = child
    input.on('finish', () => ownedChild.emit('close', 0))
    vi.spyOn(child, 'kill').mockReturnValue(true)
    vi.mocked(spawn).mockReturnValue(child)
  })

  afterEach(() => {
    vi.useRealTimers()
    vi.clearAllMocks()
  })

  it('encodes raw native BGRA with explicit dimensions and rejects partial frames', async () => {
    const stream = createLiveStream(
      'ffmpeg',
      { url: 'rtmp://localhost/live/key' },
      30,
      { width: 2, height: 2 },
      undefined,
      undefined,
      undefined,
      undefined,
      undefined,
      undefined,
      { width: 2, height: 2 },
      'bgra',
    )
    const args = vi.mocked(spawn).mock.calls[0]?.[1]
    expect(args).toEqual(
      expect.arrayContaining(['rawvideo', '-pixel_format', 'bgra', '-video_size', '2x2']),
    )
    expect(args).not.toContain('mjpeg')
    expect(() => stream.update(Buffer.alloc(15))).toThrow('frame length')
    stream.update(Buffer.alloc(16, 42), performance.now())
    await vi.advanceTimersByTimeAsync(100)
    expect(frames.length).toBeGreaterThan(0)
    expect(frames.every((frame) => frame.equals(Buffer.alloc(16, 42)))).toBe(true)
    await stream.stop()
  })

  it('rejects a raw stream without dimensions before spawning FFmpeg', () => {
    expect(() =>
      createLiveStream(
        'ffmpeg',
        { url: 'rtmp://localhost/live/key' },
        30,
        { width: 2, height: 2 },
        undefined,
        undefined,
        undefined,
        undefined,
        undefined,
        undefined,
        undefined,
        'bgra',
      ),
    ).toThrow('explicit input dimensions')
    expect(spawn).not.toHaveBeenCalled()
  })

  it('declares the native I420 layout and color matrix to FFmpeg', async () => {
    const stream = createLiveStream(
      'ffmpeg',
      { url: 'rtmp://localhost/live/key' },
      60,
      { width: 2, height: 2 },
      undefined,
      undefined,
      undefined,
      undefined,
      undefined,
      undefined,
      { width: 2, height: 2 },
      'i420',
    )
    expect(vi.mocked(spawn).mock.calls[0]?.[1]).toEqual(
      expect.arrayContaining(['rawvideo', 'yuv420p', '-colorspace', 'bt709', '-color_range', 'tv']),
    )
    expect(() => stream.update(Buffer.alloc(16))).toThrow('frame length')
    stream.update(Buffer.alloc(6))
    await vi.advanceTimersByTimeAsync(100)
    expect(frames.every((frame) => frame.length === 6)).toBe(true)
    await stream.stop()
  })

  it('repeats idle frames, replaces the latest image, and stops its timer', async () => {
    const stream = createLiveStream('ffmpeg', { url: 'rtmp://localhost/live/key' }, 30, {
      width: 640,
      height: 360,
    })
    stream.update(Buffer.from('first'))
    await vi.advanceTimersByTimeAsync(150)
    expect(frames.length).toBeGreaterThanOrEqual(4)
    expect(frames.every((frame) => frame.toString() === 'first')).toBe(true)
    stream.update(Buffer.from('second'))
    await vi.advanceTimersByTimeAsync(100)
    expect(frames.at(-1)?.toString()).toBe('second')
    progress.write('frame=1\nprogress=continue\n')
    await stream.ready()
    await stream.stop()
    const count = frames.length
    await vi.advanceTimersByTimeAsync(1000)
    expect(frames).toHaveLength(count)
    await stream.stop()
    expect(input.writableEnded).toBe(true)
  })

  it('resynchronizes audio time when backpressure evicts its matching raw video', async () => {
    let now = 1000
    const clock = vi.spyOn(performance, 'now').mockImplementation(() => now)
    let release = (): void => undefined
    const blocked = new Promise<void>((resolve) => {
      release = resolve
    })
    const mediaTimes: number[] = []
    const raw = Buffer.alloc(1920 * 1080 * 1.5)
    const stream = createLiveStream(
      'ffmpeg',
      { url: 'rtmp://localhost/live/key' },
      60,
      { width: 1920, height: 1080 },
      undefined,
      async (frame) => {
        await blocked
        return frame
      },
      {
        url: '',
        buffer: new LiveAudioBuffer(),
        activate: async () => undefined,
        createInput: () => {
          throw new Error('Persistent output owns the audio encoder')
        },
        close: async () => undefined,
      },
      undefined,
      undefined,
      {
        frame: (_index, at) => mediaTimes.push(at),
        data: async () => undefined,
        health: () => ({
          outputLagMs: 0,
          progressAgeMs: 0,
          networkBlockedMs: 0,
          networkQueuedBytes: 0,
        }),
      },
      { width: 1920, height: 1080 },
      'i420',
    )
    try {
      stream.update(raw, 850)
      now = 1400
      for (let index = 0; index < 25; index++) stream.update(raw, 1000 + index * (1000 / 60))
      release()
      await vi.advanceTimersByTimeAsync(1)
      expect(mediaTimes[0]).toBe(850)
      expect(mediaTimes[1]).toBeCloseTo(1250)
    } finally {
      release()
      await stream.stop()
      clock.mockRestore()
    }
  })

  it('does not report ready before FFmpeg publishes a frame', async () => {
    const stream = createLiveStream('ffmpeg', { url: 'rtmp://localhost/live/key' }, 30, {
      width: 640,
      height: 360,
    })
    let ready = false
    const pending = stream.ready().then(() => {
      ready = true
    })
    progress.write('frame=0\nprogress=continue\n')
    await vi.advanceTimersByTimeAsync(1)
    expect(ready).toBe(false)
    progress.write('frame=')
    progress.write('2\nprogress=continue\n')
    await pending
    expect(ready).toBe(true)
    await stream.stop()
  })

  it('reports a disconnect without exposing FFmpeg diagnostics or stream keys', async () => {
    const stream = createLiveStream('ffmpeg', { url: 'rtmps://localhost/live/secret-key' }, 30, {
      width: 640,
      height: 360,
    })
    child.stderr?.emit('data', Buffer.from('Could not publish secret-key'))
    child.emit('close', 1)
    await expect(stream.failure).rejects.toThrow('disconnected or failed')
    await expect(stream.ready()).rejects.not.toThrow('secret-key')
    await expect(stream.stop()).rejects.toThrow('disconnected or failed')
  })

  it('validates destinations and encoding limits before spawning FFmpeg', () => {
    for (const url of [
      'file:///tmp/key',
      'https://example.com/live/key',
      'rtmp://',
      'rtmp://localhost',
      'rtmp://localhost/live/\nkey',
    ]) {
      expect(SuiteCutStreamOptionsSchema.safeParse({ url }).success).toBe(false)
    }
    expect(
      SuiteCutStreamOptionsSchema.safeParse({
        url: 'rtmps://example.com/live/key',
        size: { width: 1280, height: 720 },
        bitrateKbps: 4500,
      }).success,
    ).toBe(true)
    expect(
      SuiteCutStreamOptionsSchema.parse({
        url: 'rtmp://localhost/live/key',
        audio: true,
      }).audio,
    ).toBe(true)
    expect(
      SuiteCutStreamOptionsSchema.parse({
        url: 'rtmp://localhost/live/key',
        audio: false,
      }).audio,
    ).toBe(false)
    expect(
      SuiteCutStreamOptionsSchema.parse({
        url: 'rtmp://localhost/live/key',
        audio: 'tab',
      }).audio,
    ).toBe(true)
    expect(
      SuiteCutStreamOptionsSchema.safeParse({
        url: 'rtmp://localhost/live/key',
        size: { width: 1279, height: 720 },
      }).success,
    ).toBe(false)
    expect(
      SuiteCutStreamOptionsSchema.safeParse({ url: 'rtmp://localhost/live/key', bitrateKbps: 0 })
        .success,
    ).toBe(false)
  })

  it('cancels retry backoff without launching another encoder', async () => {
    const stream = createReconnectingStream(
      'ffmpeg',
      {
        url: 'rtmp://localhost/live/key',
        adaptiveBitrate: false,
        reconnect: { initialDelayMs: 1000 },
      },
      30,
      { width: 640, height: 360 },
    )
    stream.update(Buffer.from('latest'))
    child.emit('close', 1)
    await vi.advanceTimersByTimeAsync(100)
    await stream.stop()
    await vi.advanceTimersByTimeAsync(5000)
    expect(spawn).toHaveBeenCalledTimes(1)
  })

  it('retries with the latest frame and fails after the configured retry budget', async () => {
    const replacement = new ChildProcess()
    const nextInput = new PassThrough()
    const nextProgress = new PassThrough()
    replacement.stdin = nextInput
    replacement.stderr = new PassThrough()
    Object.defineProperty(replacement, 'stdio', {
      value: [nextInput, null, replacement.stderr, nextProgress, null],
    })
    vi.spyOn(replacement, 'kill').mockReturnValue(true)
    nextInput.on('finish', () => replacement.emit('close', 0))
    const replacementFrames: Buffer[] = []
    nextInput.on('data', (chunk: Buffer) => replacementFrames.push(chunk))
    vi.mocked(spawn).mockReturnValueOnce(child).mockReturnValueOnce(replacement)
    const stream = createReconnectingStream(
      'ffmpeg',
      {
        url: 'rtmp://localhost/live/key',
        adaptiveBitrate: false,
        reconnect: { initialDelayMs: 100, maxDelayMs: 100, maxAttempts: 1 },
      },
      30,
      { width: 640, height: 360 },
    )
    stream.update(Buffer.from('old'))
    child.emit('close', 1)
    await vi.advanceTimersByTimeAsync(50)
    stream.update(Buffer.from('new'))
    await vi.advanceTimersByTimeAsync(100)
    expect(spawn).toHaveBeenCalledTimes(2)
    expect(replacementFrames[0]?.toString()).toBe('new')
    nextProgress.write('frame=2\n')
    await stream.ready()
    replacement.emit('close', 1)
    await expect(stream.failure).rejects.toThrow('disconnected or failed')
    await expect(stream.stop()).rejects.toThrow('disconnected or failed')
    await expect(stream.stop()).rejects.toThrow('disconnected or failed')
  })

  it('closes the live input on cancellation without turning it into an encoder failure', async () => {
    const controller = new AbortController()
    const stream = createLiveStream(
      'ffmpeg',
      { url: 'rtmp://localhost/live/key' },
      30,
      { width: 640, height: 360 },
      controller.signal,
    )
    stream.update(Buffer.from('first'))
    await vi.advanceTimersByTimeAsync(100)
    controller.abort()
    await stream.stop()
    const count = frames.length
    await vi.advanceTimersByTimeAsync(100)
    expect(frames).toHaveLength(count)
    expect(input.writableEnded).toBe(true)
  })
})
