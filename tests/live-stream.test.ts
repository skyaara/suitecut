import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

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
      { url: 'rtmp://localhost/live/key', reconnect: { initialDelayMs: 1000 } },
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
