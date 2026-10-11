import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { afterEach, describe, expect, it, vi } from 'vitest'

import {
  type NativeAudioPacket,
  type NativeVideoFrame,
  type NativeBrowser,
} from '../src/native-browser.js'
import { createNativeVideoRecorder } from '../src/native-recorder.js'
import { createNativeRecordingSession } from '../src/native-session.js'

vi.mock('../src/native-recorder.js', () => ({ createNativeVideoRecorder: vi.fn() }))

function source() {
  let deliver: ((frame: NativeVideoFrame) => void) | undefined
  let deliverAudio: ((packet: NativeAudioPacket) => void) | undefined
  const close = vi.fn(() => Promise.resolve())
  const unsubscribe = vi.fn()
  const unsubscribeAudio = vi.fn()
  const unsubscribeFailure = vi.fn()
  const unsubscribeClose = vi.fn()
  const browser: NativeBrowser = {
    state: 'running',
    width: 2,
    height: 2,
    pixelFormat: 'bgra',
    framesPerSecond: 30,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    onFailure: () => unsubscribeFailure,
    onClose: () => unsubscribeClose,
    close,
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({ result: { value: 'http://fixture/' } }),
    onFrame(listener) {
      deliver = listener
      listener({
        data: Buffer.alloc(16),
        timestampMs: -1000,
        width: 2,
        height: 2,
        pixelFormat: 'bgra',
        droppedFrames: 0,
      })
      return unsubscribe
    },
    onAudio(listener) {
      deliverAudio = listener
      return unsubscribeAudio
    },
  }
  return {
    browser,
    close,
    unsubscribe,
    unsubscribeAudio,
    unsubscribeFailure,
    unsubscribeClose,
    emit: () =>
      deliver?.({
        data: Buffer.alloc(16),
        timestampMs: performance.now(),
        width: 2,
        height: 2,
        pixelFormat: 'bgra',
        droppedFrames: 0,
      }),
    emitAudio: () =>
      deliverAudio?.({
        data: Buffer.alloc(3_840, 1),
        frames: 960,
        timestampMs: performance.now(),
      }),
  }
}

describe('native recording clock and ownership', () => {
  afterEach(() => {
    vi.useRealTimers()
    vi.restoreAllMocks()
  })

  it('keeps raw media and idle-frame timers out of the SDK', async () => {
    vi.useFakeTimers()
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-idle-test-'))
    const push = vi.fn()
    vi.mocked(createNativeVideoRecorder).mockReturnValue({
      failure: new Promise<never>(() => undefined),
      start: push,
      ready: Promise.resolve(),
      pause: vi.fn(() => Promise.resolve()),
      firstAtMs: 0,
      stop: () => Promise.resolve(),
    })
    const live = source()
    const { session } = await createNativeRecordingSession({
      source: live.browser,
      ffmpegPath: '/unused',
      capture: { audio: true },
      output: { pathFor: (name) => join(directory, name), attach: () => Promise.resolve() },
    })
    try {
      for (let i = 0; i < 10; i++) {
        await vi.advanceTimersByTimeAsync(16)
        live.emit()
      }
      expect(push).toHaveBeenCalledTimes(1)
      await vi.advanceTimersByTimeAsync(200)
      expect(push).toHaveBeenCalledTimes(1)
    } finally {
      await session.endRecording()
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('excludes nested preparation pauses and anchors late sources at registration', async () => {
    vi.useFakeTimers()
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-recording-test-'))
    vi.mocked(createNativeVideoRecorder).mockImplementation(() => {
      let firstAtMs: number | undefined
      return {
        failure: new Promise<never>(() => undefined),
        ready: Promise.resolve(),
        pause: vi.fn(() => Promise.resolve()),
        start(atMs) {
          firstAtMs ??= atMs
        },
        get firstAtMs() {
          return firstAtMs
        },
        stop: () => Promise.resolve(),
      }
    })
    const first = source()
    const second = source()
    const { session, addSource, page } = await createNativeRecordingSession({
      source: first.browser,
      ffmpegPath: '/unused',
      capture: { audio: true },
      output: { pathFor: (name) => join(directory, name), attach: () => Promise.resolve() },
    })
    try {
      await vi.advanceTimersByTimeAsync(500)
      await session.withCapturePaused(async () => {
        await vi.advanceTimersByTimeAsync(1000)
        await session.withCapturePaused(async () => {
          await vi.advanceTimersByTimeAsync(1000)
        })
        expect(session.now()).toBe(500)
      })
      await vi.advanceTimersByTimeAsync(500)
      const next = await addSource(second.browser)
      session.selectPage(next)
      expect(session.now()).toBe(1000)
      await vi.advanceTimersByTimeAsync(500)
      session.selectPage(page)
      const captured = await session.seal()
      expect(captured.endedAtMs).toBe(1500)
      const late = captured.videos.find((video) => video.pageId === session.pageIdFor(next))
      expect(late?.sourceStartedAtMs).toBe(1000)
      expect(late?.firstFrameEpochMs).toBe(session.clock.originEpochMs + 3000)
      expect(first.close).not.toHaveBeenCalled()
      expect(second.close).not.toHaveBeenCalled()
      expect(first.unsubscribe).not.toHaveBeenCalled()
      expect(second.unsubscribe).not.toHaveBeenCalled()
      expect(first.unsubscribeAudio).not.toHaveBeenCalled()
      expect(second.unsubscribeAudio).not.toHaveBeenCalled()
      expect(first.unsubscribeFailure).toHaveBeenCalledTimes(1)
      expect(first.unsubscribeClose).toHaveBeenCalledTimes(1)
      expect(second.unsubscribeFailure).toHaveBeenCalledTimes(1)
      expect(second.unsubscribeClose).toHaveBeenCalledTimes(1)
      expect(() => session.selectPage(page)).toThrow('ended')
    } finally {
      await session.endRecording()
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('pauses the native timeline while excluding preparation from the manifest clock', async () => {
    vi.useFakeTimers()
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-audio-clock-test-'))
    const pushAudio = vi.fn<(paused: boolean) => Promise<void>>(() => Promise.resolve())
    vi.mocked(createNativeVideoRecorder).mockReturnValue({
      failure: new Promise<never>(() => undefined),
      start: vi.fn(),
      ready: Promise.resolve(),
      pause: pushAudio,
      firstAtMs: 0,
      stop: () => Promise.resolve(),
    })
    const live = source()
    const { session } = await createNativeRecordingSession({
      source: live.browser,
      ffmpegPath: '/unused',
      capture: { audio: true },
      output: { pathFor: (name) => join(directory, name), attach: () => Promise.resolve() },
    })
    try {
      await vi.advanceTimersByTimeAsync(500)
      live.emitAudio()
      await session.withCapturePaused(async () => {
        await vi.advanceTimersByTimeAsync(1_000)
        live.emitAudio()
      })
      await vi.advanceTimersByTimeAsync(250)
      live.emitAudio()
      expect(pushAudio).toHaveBeenCalledTimes(2)
      expect(pushAudio.mock.calls.map((call) => call[0])).toEqual([true, false])
      expect(session.now()).toBe(750)
    } finally {
      await session.endRecording()
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('does not subscribe to page audio when capture audio is silent', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-silent-audio-test-'))
    const live = source()
    vi.mocked(createNativeVideoRecorder).mockReturnValue({
      failure: new Promise<never>(() => undefined),
      start: vi.fn(),
      ready: Promise.resolve(),
      pause: vi.fn(() => Promise.resolve()),
      firstAtMs: 0,
      stop: () => Promise.resolve(),
    })
    const { session } = await createNativeRecordingSession({
      source: live.browser,
      ffmpegPath: '/unused',
      capture: { audio: false },
      output: { pathFor: (name) => join(directory, name), attach: () => Promise.resolve() },
    })
    try {
      expect(createNativeVideoRecorder).toHaveBeenCalledWith(
        expect.objectContaining({ audio: false }),
      )
      expect(live.unsubscribeAudio).not.toHaveBeenCalled()
    } finally {
      await session.endRecording()
      await rm(directory, { recursive: true, force: true })
    }
  })
})
