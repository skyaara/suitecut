import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativeBroadcast } from '../src/native.js'
import { type UntrustedInput } from '../src/untrusted.js'

function source(finalizationFailure = false): NativeBrowser {
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
    navigate: () => Promise.resolve(),
    close: vi.fn(() => Promise.resolve()),
    sendDevToolsCommand: () => Promise.resolve(undefined),
    onFrame() {
      throw new Error('Unexpected JS raw video')
    },
    onAudio() {
      throw new Error('Unexpected JS raw audio')
    },
    command: vi.fn((name: string): Promise<UntrustedInput> =>
      Promise.resolve(
        name === 'media.start'
          ? { outputId: 1, audioReadinessRequired: true }
          : {
              ready: true,
              paused: false,
              stopped: name === 'media.stop',
              videoFrames: 30,
              audioSamples: 48000,
              repeatedFrames: 0,
              droppedAudioSamples: 0,
              missingAudioSamples: 0,
              lateAudioSamples: 0,
              queuedAudioBytes: 0,
              queuedVideoBytes: 0,
              startedMs: 0,
              durationMs: 1000,
              maximumEncodeMs: 3,
              error:
                finalizationFailure && name === 'media.stop'
                  ? 'Native mux finalization failed'
                  : '',
            },
      ),
    ),
  }
}
describe('native publisher ownership', () => {
  it('rejects legacy audio modes', async () => {
    await expect(
      createNativeBroadcast({
        source: source(),
        stream: { url: 'rtmp://localhost/live/test', audio: 'tab' } as never,
      }),
    ).rejects.toThrow()
  })
  it('keeps publishing and source lifecycle independent of JS raw media', async () => {
    const first = source(),
      second = source()
    const broadcast = await createNativeBroadcast({
      source: first,
      stream: { url: 'rtmp://localhost/live/test', audio: true },
    })
    try {
      await broadcast.ready()
      broadcast.selectSource(first)
      expect(() => broadcast.selectSource(second)).toThrow('engine.select')
      expect(broadcast.health().outputLagMs).toBe(3)
    } finally {
      const stop = broadcast.stop()
      expect(broadcast.stop()).toBe(stop)
      await stop
    }
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting a mock.
    expect(first.close).not.toHaveBeenCalled()
  })
  it('surfaces native finalization failure', async () => {
    const broadcast = await createNativeBroadcast({
      source: source(true),
      stream: { url: 'rtmp://localhost/live/test' },
    })
    await expect(broadcast.stop()).rejects.toThrow('Native mux finalization failed')
  })
  it('finalizes native output when the caller aborts', async () => {
    const controller = new AbortController()
    const browser = source()
    const broadcast = await createNativeBroadcast({
      source: browser,
      stream: { url: 'rtmp://localhost/live/test' },
      signal: controller.signal,
    })
    controller.abort()
    await broadcast.stop()
    expect(
      // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting a mock.
      vi.mocked(browser.command!).mock.calls.filter(([name]) => name === 'media.stop'),
    ).toHaveLength(1)
  })
})
