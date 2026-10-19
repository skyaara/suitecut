// SPDX-License-Identifier: MIT
import { type NativeBrowser } from './native-browser.js'
import { NativeRecordingInfoSchema, type NativeRecordingInfo } from './native-recording-protocol.js'
import { type SuiteCutCaptureSize } from './schemas.js'
import { type UntrustedInput } from './untrusted.js'

interface RecordingTransport {
  runRecording(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
  onRecordingMessage(listener: (info: NativeRecordingInfo) => void): () => void
}

/** Thin control adapter; capture, cadence, PCM, FFmpeg and files stay in C++. */
export async function createNativeRecordingPipeline(options: {
  source: NativeBrowser & Partial<RecordingTransport>
  ffmpegPath: string
  outputPath: string
  size: SuiteCutCaptureSize
  framesPerSecond?: number | undefined
  audio?: boolean
  signal?: AbortSignal
}) {
  const source = options.source
  if (!source.runRecording || !source.onRecordingMessage)
    throw new Error('Native recording transport is unavailable')
  const transport: RecordingTransport = {
    runRecording: source.runRecording.bind(source),
    onRecordingMessage: source.onRecordingMessage.bind(source),
  }
  let id: string | undefined
  let terminal: NativeRecordingInfo | undefined
  let stopping: Promise<NativeRecordingInfo> | undefined
  let rejectFailure: (error: Error) => void = () => undefined
  const failure = new Promise<never>((_, reject) => {
    rejectFailure = reject
  })
  void failure.catch(() => undefined)
  const early: NativeRecordingInfo[] = []
  let detach = (): void => undefined
  let released = false
  const unsubscribe = (): void => {
    if (released) return
    released = true
    detach()
  }
  const received = (info: NativeRecordingInfo): void => {
    if (!id) {
      if (early.length === 16) early.shift()
      early.push(info)
      return
    }
    if (info.id !== id || !['complete', 'failed'].includes(info.phase)) return
    terminal = info
    if (info.failure) rejectFailure(new Error(info.failure))
    unsubscribe()
  }
  detach = transport.onRecordingMessage(received)
  let first: NativeRecordingInfo
  try {
    first = NativeRecordingInfoSchema.parse(
      await transport.runRecording(
        {
          action: 'start',
          ffmpegPath: options.ffmpegPath,
          outputPath: options.outputPath,
          framesPerSecond: options.framesPerSecond ?? source.framesPerSecond,
          width: options.size.width,
          height: options.size.height,
          audio: options.audio ?? false,
          timeoutMs: 30_000,
        },
        options.signal,
      ),
    )
    if (first.phase !== 'recording' || first.firstTimestampMs < 0 || first.failure)
      throw new Error(first.failure || 'Native recording did not start')
    id = first.id
    for (const info of early) received(info)
    early.length = 0
  } catch (error) {
    unsubscribe()
    throw error
  }
  if (source.nativeClockOffsetMs === undefined) {
    await transport.runRecording({ action: 'cancel', recordingId: id }).finally(unsubscribe)
    throw new Error('Native recording requires the measured capture clock mapping')
  }
  const firstTimestampMs = first.firstTimestampMs + source.nativeClockOffsetMs
  const closedWithoutCompletion = (): Error =>
    source.state === 'failed' && source.signal.reason instanceof Error
      ? source.signal.reason
      : new Error('Native source closed without a completed recording')
  const command = async (action: string): Promise<NativeRecordingInfo> =>
    NativeRecordingInfoSchema.parse(
      await transport.runRecording({ action, recordingId: id, timeoutMs: 60_000 }),
    )
  return {
    failure,
    firstTimestampMs,
    get complete() {
      return terminal?.phase === 'complete'
    },
    pause: () => command('pause'),
    resume: () => command('resume'),
    cancel: async () => {
      try {
        terminal = await command('cancel')
      } finally {
        unsubscribe()
      }
    },
    stop(): Promise<NativeRecordingInfo> {
      return (stopping ??= (async () => {
        try {
          // Native EOF finalizes the encoder before the source physically closes.
          // Subscribe to closure only once the source is actually closing.
          if (!terminal && source.state === 'closing') {
            await source.closed
            if (!terminal) throw closedWithoutCompletion()
          }
          const result = terminal ?? (await command('stop'))
          terminal = result
          if (result.failure) throw new Error(result.failure)
          return result
        } finally {
          unsubscribe()
        }
      })())
    },
  }
}
