import { describe, expect, it, vi } from 'vitest'

import { createLiveStream } from '../src/live-stream.js'
import {
  type NativeAudioPacket,
  type NativeBrowser,
  type NativeVideoFrame,
} from '../src/native-browser.js'
import { createNativeBroadcast } from '../src/native.js'

vi.mock('../src/live-stream.js', () => ({ createLiveStream: vi.fn() }))

function source(
  width = 2,
): NativeBrowser & { emit(frame: NativeVideoFrame): void; subscriberCount(): number } {
  const frames = new Set<(frame: NativeVideoFrame) => void>()
  const audio = new Set<(packet: NativeAudioPacket) => void>()
  return {
    state: 'running',
    width,
    pixelFormat: 'bgra',
    height: 2,
    framesPerSecond: 60,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve(undefined),
    onFrame: (listener) => {
      frames.add(listener)
      return () => {
        frames.delete(listener)
      }
    },
    onAudio: (listener) => {
      audio.add(listener)
      return () => {
        audio.delete(listener)
      }
    },
    close: vi.fn(() => Promise.resolve()),
    emit: (frame) => {
      for (const callback of frames) callback(frame)
    },
    subscriberCount: () => frames.size + audio.size,
  }
}

describe('native broadcast source ownership', () => {
  it('rejects the old string audio mode in the native boolean API', async () => {
    await expect(
      createNativeBroadcast({
        source: source(),
        ffmpegPath: '/ffmpeg',
        stream: { url: 'rtmp://localhost/live/test', audio: 'tab' } as never,
      }),
    ).rejects.toThrow()
    expect(createLiveStream).not.toHaveBeenCalled()
  })

  it('switches sources without replacing the publisher and preserves capture timestamps', async () => {
    const update = vi.fn()
    const stop = vi.fn(() => Promise.resolve())
    vi.mocked(createLiveStream).mockReturnValue({
      update,
      stop,
      ready: () => Promise.resolve(),
      failure: new Promise<never>(() => undefined),
      health: () => ({ captureLagMs: 0, outputLagMs: 0, progressAgeMs: 0 }),
    })
    const first = source()
    const second = source()
    const broadcast = await createNativeBroadcast({
      source: first,
      ffmpegPath: '/ffmpeg',
      stream: { url: 'rtmp://localhost/live/test' },
    })
    const frame = {
      data: Buffer.alloc(16),
      pixelFormat: 'bgra' as const,
      width: 2,
      height: 2,
      timestampMs: 123,
      droppedFrames: 0,
    }
    first.emit(frame)
    expect(update).toHaveBeenLastCalledWith(frame.data, 123)
    broadcast.selectSource(second)
    expect(first.subscriberCount()).toBe(0)
    first.emit(frame)
    expect(update).toHaveBeenCalledTimes(1)
    second.emit({ ...frame, timestampMs: 456 })
    expect(update).toHaveBeenLastCalledWith(frame.data, 456)
    expect(() => broadcast.selectSource(source(4))).toThrow('matching dimensions')
    expect(second.subscriberCount()).toBe(1)
    expect(createLiveStream).toHaveBeenCalledWith(
      '/ffmpeg',
      expect.objectContaining({ audio: false }),
      expect.anything(),
      expect.anything(),
      expect.anything(),
      undefined,
      undefined,
      expect.anything(),
      expect.anything(),
    )
    await broadcast.stop()
    await broadcast.stop()
    expect(stop).toHaveBeenCalledTimes(1)
    expect(second.subscriberCount()).toBe(0)
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting mocked method.
    expect(first.close).not.toHaveBeenCalled()
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting mocked method.
    expect(second.close).not.toHaveBeenCalled()
  })

  it('subscribes to CEF audio only when native stream audio is enabled', async () => {
    const stop = vi.fn(() => Promise.resolve())
    vi.mocked(createLiveStream).mockReturnValue({
      update: vi.fn(),
      stop,
      ready: () => Promise.resolve(),
      failure: new Promise<never>(() => undefined),
      health: () => ({ captureLagMs: 0, outputLagMs: 0, progressAgeMs: 0 }),
    })
    const browser = source()
    const broadcast = await createNativeBroadcast({
      source: browser,
      ffmpegPath: '/ffmpeg',
      stream: { url: 'rtmp://localhost/live/test', audio: true },
    })
    try {
      expect(browser.subscriberCount()).toBe(2)
      expect(createLiveStream).toHaveBeenCalledWith(
        '/ffmpeg',
        expect.objectContaining({ audio: true }),
        expect.anything(),
        expect.anything(),
        expect.anything(),
        undefined,
        expect.any(Object),
        expect.anything(),
        expect.anything(),
      )
    } finally {
      await broadcast.stop()
    }
    expect(browser.subscriberCount()).toBe(0)
  })

  it('cleans subscriptions when the caller aborts', async () => {
    const stop = vi.fn(() => Promise.resolve())
    vi.mocked(createLiveStream).mockReturnValue({
      update: vi.fn(),
      stop,
      ready: () => Promise.resolve(),
      failure: new Promise<never>(() => undefined),
      health: () => ({ captureLagMs: 0, outputLagMs: 0, progressAgeMs: 0 }),
    })
    const browser = source()
    const controller = new AbortController()
    const broadcast = await createNativeBroadcast({
      source: browser,
      ffmpegPath: '/ffmpeg',
      stream: { url: 'rtmp://localhost/live/test' },
      signal: controller.signal,
    })
    controller.abort()
    await broadcast.stop()
    expect(stop).toHaveBeenCalledTimes(1)
    expect(browser.subscriberCount()).toBe(0)
  })
})
