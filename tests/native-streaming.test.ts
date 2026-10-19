// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { type NativeStreamingInfo } from '../src/native-streaming-protocol.js'
import { createNativePublisher } from '../src/native-streaming.js'
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
const config = {
  ffmpegPath: '/usr/bin/ffmpeg',
  publisher: { destination: 'rtmp://localhost/live/native' },
}
describe('native publisher control adapter', () => {
  it('passes a bounded native reconnect policy while preserving the original start deadline', async () => {
    const f = fixture()
    const publisher = await createNativePublisher({
      ...config,
      source: f.source,
      publisher: { ...config.publisher, reconnect: {}, timeoutMs: 47 },
    })
    expect(f.runStreaming.mock.calls[0]?.[0]).toMatchObject({
      action: 'start',
      timeoutMs: 47,
      reconnect: { maxAttempts: 3, initialDelayMs: 250, maxDelayMs: 5000, timeoutMs: 30000 },
    })
    expect(f.onFrame).not.toHaveBeenCalled()
    expect(f.onAudio).not.toHaveBeenCalled()
    await publisher.stop()
    const disabled = fixture()
    const single = await createNativePublisher({
      ...config,
      source: disabled.source,
      publisher: { ...config.publisher, reconnect: false },
    })
    expect(disabled.runStreaming.mock.calls[0]?.[0]).not.toHaveProperty('reconnect')
    await single.stop()
  })

  it('rejects invalid or unbounded native retry policies before admitting a native job', async () => {
    for (const reconnect of [
      { maxAttempts: 0 },
      { maxAttempts: 9 },
      { initialDelayMs: -1 },
      { initialDelayMs: 5001 },
      { initialDelayMs: 1000, maxDelayMs: 999 },
      { maxDelayMs: 30001 },
      { timeoutMs: 0 },
      { timeoutMs: 120001 },
    ]) {
      const f = fixture()
      await expect(
        createNativePublisher({
          ...config,
          source: f.source,
          publisher: { ...config.publisher, reconnect },
        }),
      ).rejects.toThrow()
      expect(f.runStreaming).not.toHaveBeenCalled()
    }
  })

  it('keeps an observed native outage separate from actual terminal failure', async () => {
    const f = fixture(),
      publisher = await createNativePublisher({
        ...config,
        source: f.source,
        publisher: { ...config.publisher, reconnect: { maxAttempts: 1 } },
      })
    const rejected = vi.fn()
    void publisher.failure.catch(rejected)
    f.emit({
      ...info,
      phase: 'reconnecting',
      reconnecting: true,
      lastAttemptFailure: 'actual previous attempt Connection reset',
      physicalEncoderPid: 0,
    })
    await Promise.resolve()
    expect(rejected).not.toHaveBeenCalled()
    const terminal = {
      ...info,
      phase: 'failed' as const,
      failure: 'actual native retry exhaustion',
      reconnecting: false,
      physicalEncoderPid: 0,
    }
    f.emit(terminal)
    await expect(publisher.failure).rejects.toThrow('actual native retry exhaustion')
    expect((await publisher.status()).failure).toBe(terminal.failure)
  })

  it('admits an explicit native capture buffer and rejects unbounded timing values before start', async () => {
    const f = fixture()
    const publisher = await createNativePublisher({
      ...config,
      source: f.source,
      publisher: { ...config.publisher, captureLatencyMs: 180 },
    })
    expect(f.runStreaming.mock.calls[0]?.[0]).toMatchObject({
      action: 'start',
      captureLatencyMs: 180,
    })
    await publisher.stop()
    for (const captureLatencyMs of [0, 19, 1001, Infinity]) {
      const invalid = fixture()
      await expect(
        createNativePublisher({
          ...config,
          source: invalid.source,
          publisher: { ...config.publisher, captureLatencyMs },
        }),
      ).rejects.toThrow()
      expect(invalid.runStreaming).not.toHaveBeenCalled()
    }
  })

  it('passes fractional output and original deadline to native without host media subscriptions', async () => {
    const f = fixture()
    const publisher = await createNativePublisher({
      ...config,
      source: f.source,
      publisher: { ...config.publisher, framesPerSecond: 29.97, timeoutMs: 47, audio: true },
    })
    expect(f.runStreaming.mock.calls[0]?.[0]).toMatchObject({
      action: 'start',
      framesPerSecond: 29.97,
      timeoutMs: 47,
      audio: true,
    })
    expect((await publisher.status()).encodedFrames).toBe(58)
    expect(f.onFrame).not.toHaveBeenCalled()
    expect(f.onAudio).not.toHaveBeenCalled()
    await publisher.pause()
    await publisher.resume()
    await publisher.stop()
    await publisher.stop()
    expect(f.runStreaming.mock.calls.filter(([params]) => params.action === 'stop')).toHaveLength(1)
    expect(f.detach).toHaveBeenCalled()
  })
  it('rejects pipe-write readiness without actual native encoded progress', async () => {
    const f = fixture()
    f.runStreaming.mockResolvedValue({ ...info, encodedFrames: 0, encodedProgressObserved: false })
    await expect(createNativePublisher({ ...config, source: f.source })).rejects.toThrow(
      'encoded readiness',
    )
    expect(f.detach).toHaveBeenCalled()
  })
  it('admits real same-Engine page identity and rejects another helper with identical page ID', async () => {
    const f = fixture()
    const actual = { ...f.source, pageId: 2 }
    f.source.pageSource = (id) => (id === 2 ? actual : f.source)
    const publisher = await createNativePublisher({ ...config, source: f.source })
    await publisher.selectSource(actual)
    expect(f.runStreaming).toHaveBeenLastCalledWith({
      action: 'select',
      streamId: info.id,
      timeoutMs: 30_000,
      sourcePageId: 2,
    })
    await expect(publisher.selectSource({ ...actual })).rejects.toThrow('same native Engine')
    await publisher.stop()
  })
  it('retains actual failure metadata and releases terminal subscription', async () => {
    const f = fixture()
    const publisher = await createNativePublisher({ ...config, source: f.source })
    f.emit({ ...info, phase: 'failed', failure: 'Connection reset by peer', physicalEncoderPid: 0 })
    await expect(publisher.failure).rejects.toThrow('Connection reset by peer')
    await expect(publisher.stop()).rejects.toThrow('Connection reset by peer')
    expect(f.detach).toHaveBeenCalled()
  })
  it('waits for native completion during graceful helper close and preserves forced failure', async () => {
    const f = fixture()
    const publisher = await createNativePublisher({ ...config, source: f.source })
    f.closing()
    const stop = publisher.stop()
    f.emit({ ...info, phase: 'complete', physicalEncoderPid: 0 })
    f.physicallyClosed()
    expect((await stop).phase).toBe('complete')
    expect(f.runStreaming.mock.calls.filter(([params]) => params.action === 'stop')).toHaveLength(0)
    const forced = fixture()
    const other = await createNativePublisher({ ...config, source: forced.source })
    const failure = new Error('Native helper received SIGKILL')
    forced.fail(failure)
    await expect(other.failure).rejects.toBe(failure)
  })
  it('retains its genuine terminal before ACK despite128 unrelated completed stream events', async () => {
    const f = fixture()
    f.runStreaming.mockImplementation((params) => {
      expect(params.action).toBe('start')
      if (typeof params.clientToken !== 'string') throw new Error('Missing correlation token')
      f.emit({ ...info, phase: 'complete', physicalEncoderPid: 0 }, params.clientToken)
      for (let index = 0; index < 128; ++index)
        f.emit(
          { ...info, id: `other-${index}`, phase: 'failed', failure: 'unrelated failure' },
          `foreign-${index}`,
        )
      return Promise.resolve({ ...info, clientToken: params.clientToken })
    })
    const publisher = await createNativePublisher({ ...config, source: f.source })
    expect((await publisher.stop()).phase).toBe('complete')
    expect(f.runStreaming).toHaveBeenCalledTimes(1)
  })
  it('caller cancellation uses the reserved native command without the already-aborted signal', async () => {
    const f = fixture(),
      controller = new AbortController()
    const publisher = await createNativePublisher({
      ...config,
      source: f.source,
      signal: controller.signal,
    })
    controller.abort()
    await publisher.cancel()
    expect(
      f.runStreaming.mock.calls.filter(([params]) => params.action === 'cancel').length,
    ).toBeGreaterThan(0)
    expect(
      f.runStreaming.mock.calls.find(([params]) => params.action === 'cancel')?.[1],
    ).toBeUndefined()
  })
})
