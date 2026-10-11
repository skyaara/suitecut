import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativeVideoRecorder } from '../src/native-recorder.js'
import { type UntrustedInput } from '../src/untrusted.js'

function source(fail = false, finalizationFailure = false): NativeBrowser {
  return {
    state: 'running',
    width: 320,
    height: 180,
    pixelFormat: 'i420',
    framesPerSecond: 30,
    cefVersion: 'test',
    nativeMedia: true,
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve(undefined),
    onFrame() {
      throw new Error('Raw video entered the SDK')
    },
    onAudio() {
      throw new Error('Raw audio entered the SDK')
    },
    command: vi.fn((command: string): Promise<UntrustedInput> => {
      if (fail && command === 'media.start')
        return Promise.reject(new Error('Native encoder unavailable'))
      if (command === 'media.start') return Promise.resolve({ outputId: 1 })
      return Promise.resolve({
        ready: true,
        stopped: command === 'media.stop',
        paused: false,
        videoFrames: 30,
        audioSamples: 48000,
        repeatedFrames: 0,
        droppedAudioSamples: 0,
        queuedVideoBytes: 0,
        queuedAudioBytes: 0,
        startedMs: 100,
        durationMs: 1000,
        maximumEncodeMs: 3,
        error:
          finalizationFailure && command === 'media.stop' ? 'Native mux finalization failed' : '',
      })
    }),
  }
}
function record(browser: NativeBrowser) {
  return createNativeVideoRecorder({
    source: browser,
    ffmpegPath: '/must-not-launch',
    outputPath: '/test.webm',
    size: { width: 320, height: 180 },
    audio: true,
  })
}
describe('native recorder ownership', () => {
  it('captures without JS media subscriptions and preserves manifest timing', async () => {
    const recorder = record(source())
    await recorder.ready
    recorder.start(100)
    recorder.start(200)
    const stopped = recorder.stop(1000)
    expect(recorder.stop(2000)).toBe(stopped)
    await stopped
    expect(recorder.firstAtMs).toBe(100)
  })
  it('surfaces native startup failure to both lifecycle promises', async () => {
    const recorder = record(source(true))
    await expect(recorder.ready).rejects.toThrow('Native encoder unavailable')
    await expect(recorder.failure).rejects.toThrow('Native encoder unavailable')
    await expect(recorder.stop(0)).rejects.toThrow('Native encoder unavailable')
  })
  it('surfaces errors first reported during native finalization', async () => {
    const recorder = record(source(false, true))
    await recorder.ready
    await expect(recorder.stop(1000)).rejects.toThrow('Native mux finalization failed')
  })
  it('refuses a helper without a native engine', async () => {
    const browser = source()
    const unsupported = { ...browser, nativeMedia: false }
    const recorder = record(unsupported)
    await expect(recorder.ready).rejects.toThrow('no C++ media engine')
    await expect(recorder.stop(0)).rejects.toThrow('no C++ media engine')
  })
})
