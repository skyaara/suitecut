// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativeRecordingPipeline } from '../src/native-recording-pipeline.js'
import { type NativeRecordingInfo } from '../src/native-recording-protocol.js'
import { type UntrustedInput } from '../src/untrusted.js'

const info: NativeRecordingInfo = {
  id: 'recording-1',
  phase: 'recording',
  outputPath: '/actual-native-output.webm',
  failure: '',
  diagnostics: '',
  firstTimestampMs: 100,
  durationMs: 300,
  encodedDurationMs: 300,
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
function fixture() {
  const lifecycle = new AbortController()
  let state: NativeBrowser['state'] = 'running'
  let resolveClosed: () => void = () => undefined
  const closed = new Promise<void>((resolve) => {
    resolveClosed = resolve
  })
  let listener: ((value: NativeRecordingInfo) => void) | undefined
  const detach = vi.fn()
  const onFrame = vi.fn(() => () => undefined)
  const onAudio = vi.fn(() => () => undefined)
  const runRecording =
    vi.fn<
      (params: Record<string, UntrustedInput>, signal?: AbortSignal) => Promise<UntrustedInput>
    >()
  runRecording.mockImplementation((params) =>
    Promise.resolve({ ...info, phase: params.action === 'stop' ? 'complete' : 'recording' }),
  )
  const source = {
    get state() {
      return state
    },
    signal: lifecycle.signal,
    closed,
    nativeClockOffsetMs: 1000,
    framesPerSecond: 60,
    width: 64,
    height: 64,
    onFrame,
    onAudio,
    runRecording,
    onRecordingMessage(callback: (value: NativeRecordingInfo) => void) {
      listener = callback
      return detach
    },
  } as UntrustedInput as NativeBrowser & {
    runRecording: typeof runRecording
    onRecordingMessage(callback: (value: NativeRecordingInfo) => void): () => void
  }
  return {
    source,
    runRecording,
    onFrame,
    onAudio,
    detach,
    closing() {
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
    emit(value: NativeRecordingInfo) {
      listener?.(value)
    },
  }
}
const options = {
  ffmpegPath: '/native-ffmpeg',
  outputPath: '/actual-native-output.webm',
  size: { width: 64, height: 64 },
}
describe('native recording control adapter', () => {
  it.each(['complete', 'failed'] as const)(
    'detaches on a native %s terminal event before explicit stop or cancel',
    async (phase) => {
      const f = fixture()
      const recorder = await createNativeRecordingPipeline({ ...options, source: f.source })
      f.emit({ ...info, phase, failure: phase === 'failed' ? 'Actual encoder failure' : '' })
      expect(f.detach).toHaveBeenCalledOnce()
      if (phase === 'failed') {
        await expect(recorder.failure).rejects.toThrow('Actual encoder failure')
        await expect(recorder.stop()).rejects.toThrow('Actual encoder failure')
      } else await recorder.stop()
      expect(f.detach).toHaveBeenCalledOnce()
    },
  )

  it('detaches an early terminal delivered synchronously during subscription registration', async () => {
    const f = fixture()
    f.source.onRecordingMessage = (listener) => {
      listener({ ...info, phase: 'complete' })
      return f.detach
    }
    const recorder = await createNativeRecordingPipeline({ ...options, source: f.source })
    expect(recorder.complete).toBe(true)
    expect(f.detach).toHaveBeenCalledOnce()
    await recorder.stop()
    expect(f.detach).toHaveBeenCalledOnce()
  })

  it('preserves the measured native first frame and real CFR/audio metadata through native stop', async () => {
    const f = fixture()
    const recorder = await createNativeRecordingPipeline({
      ...options,
      source: f.source,
      framesPerSecond: 120,
      audio: true,
    })
    expect(recorder.firstTimestampMs).toBe(1100)
    const result = await recorder.stop()
    expect(result.outputFrames).toBe(36)
    expect(result.heldFrames).toBe(34)
    expect(result.audioInputFrames).toBe(960)
    expect(f.onFrame).not.toHaveBeenCalled()
    expect(f.onAudio).not.toHaveBeenCalled()
    expect(f.runRecording.mock.calls.map(([params]) => params.action)).toEqual(['start', 'stop'])
    expect(f.detach).toHaveBeenCalledTimes(1)
  })
  it('does not lose a native encoder failure delivered in the same packet batch as the start reply', async () => {
    const f = fixture()
    f.runRecording.mockImplementationOnce(() => {
      f.emit({ ...info, phase: 'failed', failure: 'Native encoder exited with signal25' })
      return Promise.resolve(info)
    })
    const recorder = await createNativeRecordingPipeline({ ...options, source: f.source })
    await expect(recorder.failure).rejects.toThrow('signal25')
    await expect(recorder.stop()).rejects.toThrow('signal25')
    expect(f.runRecording).toHaveBeenCalledTimes(1)
  })
  it('uses the genuine complete event after page close instead of issuing a command to a closed page', async () => {
    const f = fixture()
    const recorder = await createNativeRecordingPipeline({ ...options, source: f.source })
    f.emit({ ...info, phase: 'complete' })
    expect(recorder.complete).toBe(true)
    expect(await recorder.stop()).toEqual({ ...info, phase: 'complete' })
    expect(f.runRecording).toHaveBeenCalledTimes(1)
  })
  it('waits for native physical closure and uses its completion instead of sending stop during EOF', async () => {
    const f = fixture()
    const recorder = await createNativeRecordingPipeline({ ...options, source: f.source })
    f.closing()
    let settled = false
    const stopping = recorder.stop().finally(() => {
      settled = true
    })
    await Promise.resolve()
    expect(settled).toBe(false)
    expect(f.runRecording).toHaveBeenCalledTimes(1)
    f.emit({ ...info, phase: 'complete' })
    f.physicallyClosed()
    expect(await stopping).toEqual({ ...info, phase: 'complete' })
    expect(f.runRecording).toHaveBeenCalledTimes(1)
  })
  it('reports missing native completion after physical EOF without inventing a completed artifact', async () => {
    const f = fixture()
    const recorder = await createNativeRecordingPipeline({ ...options, source: f.source })
    f.closing()
    const stopping = recorder.stop()
    f.physicallyClosed()
    await expect(stopping).rejects.toThrow('closed without a completed recording')
    expect(f.runRecording).toHaveBeenCalledTimes(1)
  })
  it('retains the genuine native encoder failure while graceful closure is in progress', async () => {
    const f = fixture()
    const recorder = await createNativeRecordingPipeline({ ...options, source: f.source })
    f.closing()
    const stopping = recorder.stop()
    f.emit({ ...info, phase: 'failed', failure: 'Native encoder received signal9' })
    f.physicallyClosed()
    await expect(stopping).rejects.toThrow('signal9')
    await expect(recorder.failure).rejects.toThrow('signal9')
  })
  it('cancels its native job if the transport lacks a measured capture clock', async () => {
    const f = fixture()
    Object.defineProperty(f.source, 'nativeClockOffsetMs', { value: undefined })
    await expect(createNativeRecordingPipeline({ ...options, source: f.source })).rejects.toThrow(
      'measured capture clock',
    )
    expect(f.runRecording.mock.calls.map(([params]) => params.action)).toEqual(['start', 'cancel'])
    expect(f.detach).toHaveBeenCalledTimes(1)
  })
})
