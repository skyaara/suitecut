// SPDX-License-Identifier: MIT
import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { afterEach, describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativeRecordingPipeline } from '../src/native-recording-pipeline.js'
import { type NativeRecordingInfo } from '../src/native-recording-protocol.js'
import { createNativeRecordingSession } from '../src/native-session.js'
import { type UntrustedInput } from '../src/untrusted.js'

vi.mock('../src/native-recording-pipeline.js', () => ({ createNativeRecordingPipeline: vi.fn() }))

function source() {
  const lifecycle = new AbortController()
  let state: NativeBrowser['state'] = 'running'
  let resolveClosed: () => void = () => undefined
  const closed = new Promise<void>((resolve) => {
    resolveClosed = resolve
  })
  const close = vi.fn(() => Promise.resolve())
  const onFrame = vi.fn(() => () => undefined)
  const onAudio = vi.fn(() => () => undefined)
  const browser: NativeBrowser = {
    get state() {
      return state
    },
    url: 'http://fixture/',
    signal: lifecycle.signal,
    width: 2,
    height: 2,
    pixelFormat: 'bgra',
    framesPerSecond: 30,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed,
    close,
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({ result: { value: 'http://fixture/' } }),
    runAutomation: () => Promise.resolve({ result: { value: ['primitive', 'http://fixture/'] } }),
    onFrame,
    onAudio,
  }
  return {
    browser,
    close,
    onFrame,
    onAudio,
    beginClosing() {
      state = 'closing'
      lifecycle.abort(new Error('Native browser is closing'))
    },
    physicallyClosed() {
      state = 'closed'
      resolveClosed()
    },
    fail(error: Error) {
      state = 'failed'
      lifecycle.abort(error)
      resolveClosed()
    },
  }
}
function recorder(firstTimestampMs = performance.now(), durationMs = 300) {
  const info: NativeRecordingInfo = {
    id: 'recording-test',
    phase: 'complete',
    outputPath: '/native.webm',
    failure: '',
    diagnostics: '',
    firstTimestampMs,
    durationMs,
    encodedDurationMs: durationMs,
    framesPerSecond: 120,
    inputFrames: 2,
    outputFrames: 36,
    heldFrames: 34,
    supersededFrames: 0,
    audioInputFrames: 960,
    audioOutputFrames: 14400,
    audioOverlapFrames: 0,
    audioGapFrames: 13440,
    outputBytes: 2048,
  }
  return {
    failure: new Promise<never>(() => undefined),
    firstTimestampMs,
    complete: false,
    pause: vi.fn(() => Promise.resolve(info)),
    resume: vi.fn(() => Promise.resolve(info)),
    cancel: vi.fn(() => Promise.resolve()),
    stop: vi.fn(() => Promise.resolve(info)),
  }
}
async function directory() {
  return mkdtemp(join(tmpdir(), 'suitecut-native-session-'))
}
describe('native recording session clock and ownership', () => {
  afterEach(() => {
    vi.useRealTimers()
    vi.restoreAllMocks()
    vi.mocked(createNativeRecordingPipeline).mockReset()
  })
  it('leaves frame cadence and page audio ingestion in the native pipeline', async () => {
    const path = await directory()
    const live = source()
    vi.mocked(createNativeRecordingPipeline).mockImplementation(() => Promise.resolve(recorder()))
    const { session } = await createNativeRecordingSession({
      source: live.browser,
      ffmpegPath: '/unused',
      capture: { audio: true, framesPerSecond: 47 },
      output: { pathFor: (name) => join(path, name), attach: () => Promise.resolve() },
    })
    try {
      const captured = await session.seal()
      expect(live.onFrame).not.toHaveBeenCalled()
      expect(live.onAudio).not.toHaveBeenCalled()
      expect(createNativeRecordingPipeline).toHaveBeenCalledWith(
        expect.objectContaining({ framesPerSecond: 47, audio: true }),
      )
      expect(captured.videos[0]?.nativeRecording).toMatchObject({
        outputFrames: 36,
        heldFrames: 34,
        audioInputFrames: 960,
        durationMs: 300,
      })
      expect(live.close).not.toHaveBeenCalled()
    } finally {
      await session.endRecording()
      await rm(path, { recursive: true, force: true })
    }
  })
  it('allows real native completion after the graceful closing signal before physical source closure', async () => {
    const path = await directory()
    const live = source()
    const pipeline = recorder()
    vi.mocked(createNativeRecordingPipeline).mockResolvedValue(pipeline)
    const { session } = await createNativeRecordingSession({
      source: live.browser,
      ffmpegPath: '/unused',
      capture: { audio: true },
      output: { pathFor: (name) => join(path, name), attach: () => Promise.resolve() },
    })
    let failure: UntrustedInput
    void session.streamFailure?.catch((error: UntrustedInput) => {
      failure = error
    })
    try {
      live.beginClosing()
      await Promise.resolve()
      expect(failure).toBeUndefined()
      pipeline.complete = true
      live.physicallyClosed()
      await Promise.resolve()
      expect(failure).toBeUndefined()
      expect((await session.seal()).videos[0]?.nativeRecording?.outputFrames).toBe(36)
    } finally {
      await session.endRecording()
      await rm(path, { recursive: true, force: true })
    }
  })
  it('preserves the original unexpected source failure instead of treating it as graceful close', async () => {
    const path = await directory()
    const live = source()
    vi.mocked(createNativeRecordingPipeline).mockResolvedValue(recorder())
    const { session } = await createNativeRecordingSession({
      source: live.browser,
      ffmpegPath: '/unused',
      capture: { audio: false },
      output: { pathFor: (name) => join(path, name), attach: () => Promise.resolve() },
    })
    try {
      const error = new Error('Native browser exited with signal SIGKILL')
      live.fail(error)
      await expect(session.streamFailure).rejects.toBe(error)
    } finally {
      await session.endRecording()
      await rm(path, { recursive: true, force: true })
    }
  })
  it('preserves first capture timing across future nested pauses and late source registration', async () => {
    vi.useFakeTimers()
    const path = await directory()
    const first = source()
    const second = source()
    const recordings: ReturnType<typeof recorder>[] = []
    vi.mocked(createNativeRecordingPipeline).mockImplementation(() => {
      const value = recorder()
      recordings.push(value)
      return Promise.resolve(value)
    })
    const { session, addSource } = await createNativeRecordingSession({
      source: first.browser,
      ffmpegPath: '/unused',
      capture: { audio: true },
      output: { pathFor: (name) => join(path, name), attach: () => Promise.resolve() },
    })
    try {
      await vi.advanceTimersByTimeAsync(500)
      await session.withCapturePaused(async () => {
        expect(() => addSource(second.browser)).toThrow('paused')
        expect(createNativeRecordingPipeline).toHaveBeenCalledTimes(1)
        await vi.advanceTimersByTimeAsync(1000)
        await session.withCapturePaused(async () => {
          await vi.advanceTimersByTimeAsync(1000)
        })
        expect(session.now()).toBe(500)
      })
      await vi.advanceTimersByTimeAsync(500)
      const late = await addSource(second.browser)
      const captured = await session.seal()
      const firstVideo = captured.videos.find((video) => video.pageId === captured.pages[0]?.id)
      const lateVideo = captured.videos.find((video) => video.pageId === session.pageIdFor(late))
      expect(firstVideo?.sourceStartedAtMs).toBe(0)
      expect(lateVideo?.sourceStartedAtMs).toBe(1000)
      expect(lateVideo?.firstFrameEpochMs).toBe(session.clock.originEpochMs + 3000)
      expect(recordings[0]?.pause).toHaveBeenCalledTimes(1)
      expect(recordings[0]?.resume).toHaveBeenCalledTimes(1)
      expect(first.close).not.toHaveBeenCalled()
      expect(second.close).not.toHaveBeenCalled()
    } finally {
      await session.endRecording()
      await rm(path, { recursive: true, force: true })
    }
  })
  it('registers before waiting for native first-frame admission and retains native clip duration', async () => {
    vi.useFakeTimers()
    const path = await directory()
    const live = source()
    vi.mocked(createNativeRecordingPipeline).mockImplementation(async () => {
      const value = recorder(performance.now() + 10, 300)
      await vi.advanceTimersByTimeAsync(100)
      return value
    })
    const { session } = await createNativeRecordingSession({
      source: live.browser,
      ffmpegPath: '/unused',
      capture: { audio: false },
      output: { pathFor: (name) => join(path, name), attach: () => Promise.resolve() },
    })
    try {
      const captured = await session.seal()
      expect(captured.pages[0]?.createdAtMs).toBe(0)
      expect(captured.videos[0]?.sourceStartedAtMs).toBe(10)
      expect(captured.videos[0]?.nativeRecording?.durationMs).toBe(300)
      expect(captured.endedAtMs).toBe(310)
    } finally {
      await session.endRecording()
      await rm(path, { recursive: true, force: true })
    }
  })
})
