// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { type NativeStreamingInfo } from '../src/native-streaming-protocol.js'
import { createNativeBroadcast } from '../src/native.js'
import { type UntrustedInput } from '../src/untrusted.js'

const info: NativeStreamingInfo = {
  clientToken: 'fixture',
  id: 'stream-1',
  phase: 'streaming',
  failure: '',
  diagnostics: '',
  firstTimestampMs: 100,
  durationMs: 500,
  inputFrames: 15,
  submittedFrames: 60,
  heldFrames: 45,
  supersededFrames: 0,
  encodedFrames: 58,
  audioInputFrames: 12000,
  audioSubmittedFrames: 24000,
  audioGapFrames: 12000,
  audioOverlapFrames: 0,
  pausedAudioFrames: 0,
  pausedInputAudioFrames: 0,
  switchedAudioFrames: 0,
  muxedBytes: 5000,
  encodedProgressObserved: true,
  muxedProgressObserved: true,
  physicalEncoderPid: 123,
}
function fixture() {
  const controller = new AbortController()
  let state: NativeBrowser['state'] = 'running'
  let close: () => void = () => undefined
  const closed = new Promise<void>((resolve) => {
    close = resolve
  })
  let listener: ((value: NativeStreamingInfo) => void) | undefined
  const detach = vi.fn()
  let clientToken = info.clientToken
  const onFrame = vi.fn(() => () => undefined),
    onAudio = vi.fn(() => () => undefined)
  const runStreaming =
    vi.fn<
      (params: Record<string, UntrustedInput>, signal?: AbortSignal) => Promise<UntrustedInput>
    >()
  runStreaming.mockImplementation((params) => {
    if (params.action === 'start' && typeof params.clientToken === 'string')
      clientToken = params.clientToken
    return Promise.resolve({
      ...info,
      clientToken,
      phase: params.action === 'stop' || params.action === 'cancel' ? 'complete' : 'streaming',
      physicalEncoderPid: params.action === 'stop' || params.action === 'cancel' ? 0 : 123,
    })
  })
  const source = {
    pageId: 1,
    get state() {
      return state
    },
    signal: controller.signal,
    closed,
    width: 64,
    height: 64,
    framesPerSecond: 120,
    onFrame,
    onAudio,
    runStreaming,
    onStreamingMessage(callback: (value: NativeStreamingInfo) => void) {
      listener = callback
      return detach
    },
  } as UntrustedInput as NativeBrowser & {
    runStreaming: typeof runStreaming
    onStreamingMessage(callback: (value: NativeStreamingInfo) => void): () => void
  }
  return {
    source,
    runStreaming,
    onFrame,
    onAudio,
    detach,
    emit(value: NativeStreamingInfo, token = clientToken) {
      listener?.({ ...value, clientToken: token })
    },
    closing() {
      state = 'closing'
      controller.abort(new Error('Native browser closing'))
    },
    physicallyClosed() {
      state = 'closed'
      close()
    },
    fail(error: Error) {
      state = 'failed'
      controller.abort(error)
      close()
    },
  }
}
const stream = { url: 'rtmp://localhost/live/native' }
describe('native broadcast binary ownership', () => {
  it('routes output settings and controls through native without subscribing to capture', async () => {
    const f = fixture()
    const broadcast = await createNativeBroadcast({
      source: f.source,
      ffmpegPath: '/usr/bin/ffmpeg',
      stream: {
        ...stream,
        framesPerSecond: 29.97,
        audio: true,
        bitrateKbps: 8000,
        size: { width: 128, height: 72 },
        adaptiveBitrate: false,
        reconnect: false,
      },
    })
    expect(f.runStreaming.mock.calls[0]?.[0]).toMatchObject({
      action: 'start',
      videoCodec: 'h264',
      audioCodec: 'aac',
      container: 'flv',
      framesPerSecond: 29.97,
      width: 128,
      height: 72,
      audio: true,
      bitrateKbps: 8000,
      destination: stream.url,
    })
    await broadcast.ready()
    expect((await broadcast.health()).encodedFrames).toBe(58)
    expect((await broadcast.status()).audioSubmittedFrames).toBe(24000)
    await broadcast.pause()
    await broadcast.resume()
    await broadcast.stop()
    await broadcast.stop()
    expect(f.runStreaming.mock.calls.filter(([params]) => params.action === 'stop')).toHaveLength(1)
    expect(f.onFrame).not.toHaveBeenCalled()
    expect(f.onAudio).not.toHaveBeenCalled()
  })
  it('awaits the genuine native source-selection acknowledgement', async () => {
    const f = fixture()
    const actual = { ...f.source, pageId: 2 }
    f.source.pageSource = (id) => (id === 2 ? actual : f.source)
    const broadcast = await createNativeBroadcast({
      source: f.source,
      ffmpegPath: '/ffmpeg',
      stream,
    })
    let ack: (value: UntrustedInput) => void = () => undefined
    f.runStreaming.mockImplementationOnce(
      () =>
        new Promise((resolve) => {
          ack = resolve
        }),
    )
    let selected = false
    const selection = broadcast.selectSource(actual).then(() => {
      selected = true
    })
    await Promise.resolve()
    expect(selected).toBe(false)
    ack(info)
    await selection
    expect(selected).toBe(true)
    await expect(broadcast.selectSource({ ...actual })).rejects.toThrow('same native Engine')
    await broadcast.stop()
  })
  it('passes explicitly requested bounded reconnect ownership into the binary', async () => {
    const f = fixture()
    const broadcast = await createNativeBroadcast({
      source: f.source,
      ffmpegPath: '/usr/bin/ffmpeg',
      stream: {
        url: 'rtmp://localhost/live/native',
        reconnect: { maxAttempts: 2, initialDelayMs: 1000, maxDelayMs: 3000 },
      },
    })
    expect(f.runStreaming.mock.calls[0]?.[0]).toMatchObject({
      reconnect: { maxAttempts: 2, initialDelayMs: 1000, maxDelayMs: 3000, timeoutMs: 30000 },
    })
    expect(f.onFrame).not.toHaveBeenCalled()
    expect(f.onAudio).not.toHaveBeenCalled()
    await broadcast.stop()
    for (const reconnect of [
      { maxAttempts: 0 },
      { maxAttempts: 9 },
      { initialDelayMs: 5001 },
      { maxDelayMs: 30001 },
    ]) {
      const rejected = fixture()
      await expect(
        createNativeBroadcast({
          source: rejected.source,
          ffmpegPath: '/usr/bin/ffmpeg',
          stream: { url: 'rtmp://localhost/live/native', reconnect },
        }),
      ).rejects.toThrow()
      expect(rejected.runStreaming).not.toHaveBeenCalled()
    }
  })

  it('propagates actual native failures without reconnecting in JavaScript', async () => {
    const f = fixture()
    const broadcast = await createNativeBroadcast({
      source: f.source,
      ffmpegPath: '/ffmpeg',
      stream,
    })
    f.emit({ ...info, phase: 'failed', failure: 'Connection reset by peer', physicalEncoderPid: 0 })
    await expect(broadcast.failure).rejects.toThrow('Connection reset by peer')
    await expect(broadcast.stop()).rejects.toThrow('Connection reset by peer')
    expect(f.runStreaming.mock.calls.filter(([params]) => params.action === 'start')).toHaveLength(
      1,
    )
  })
  it.each([{ adaptiveBitrate: true }, { onDiagnostic: () => undefined }])(
    'rejects unsupported settings before admitting a physical publisher: %j',
    async (extra) => {
      const f = fixture()
      await expect(
        createNativeBroadcast({
          source: f.source,
          ffmpegPath: '/ffmpeg',
          stream: { ...stream, ...extra },
        }),
      ).rejects.toThrow('not supported')
      expect(f.runStreaming).not.toHaveBeenCalled()
    },
  )
  it('rejects canceled creation before native admission', async () => {
    const f = fixture()
    const controller = new AbortController()
    controller.abort(new Error('caller canceled'))
    await expect(
      createNativeBroadcast({
        source: f.source,
        ffmpegPath: '/ffmpeg',
        stream,
        signal: controller.signal,
      }),
    ).rejects.toThrow('caller canceled')
    expect(f.runStreaming).not.toHaveBeenCalled()
  })
})
