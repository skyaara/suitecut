import * as z from 'zod'

import { createLiveAudioTransport } from './live-audio.js'
import { createLiveStream, type SuiteCutLiveStream } from './live-stream.js'
import { NativeAudioAssembler } from './native-audio.js'
import { type NativeAudioPacket, type NativeBrowser } from './native-browser.js'
import { SuiteCutStreamOptionsSchema, type SuiteCutStreamOptions } from './schemas.js'

export { launchNativeBrowser } from './native-browser.js'
export type {
  NativeAudioPacket,
  NativeBrowser,
  NativeBrowserOptions,
  NativeBrowserState,
  NativePixelFormat,
  NativeVideoFrame,
} from './native-browser.js'

export interface NativeBroadcast extends Omit<SuiteCutLiveStream, 'update'> {
  /** Switches sources without restarting the publisher; dimensions and frame rate must match. */
  selectSource(source: NativeBrowser): void
}

export interface NativeBroadcastOptions {
  source: NativeBrowser
  ffmpegPath: string
  stream: NativeStreamOptions
  signal?: AbortSignal
}

const NativeStreamOptionsSchema = SuiteCutStreamOptionsSchema.omit({ audio: true }).extend({
  /** Includes CEF page audio in the broadcast when explicitly enabled. */
  audio: z.boolean().default(false),
})

export type NativeStreamOptions = z.input<typeof NativeStreamOptionsSchema>

/** Feeds CEF's raw video and audio into SuiteCut's persistent RTMP publisher. */
export async function createNativeBroadcast(
  options: NativeBroadcastOptions,
): Promise<NativeBroadcast> {
  options.signal?.throwIfAborted()
  const nativeConfig = NativeStreamOptionsSchema.parse(options.stream)
  const audioEnabled = nativeConfig.audio
  const config: SuiteCutStreamOptions = SuiteCutStreamOptionsSchema.parse({
    ...nativeConfig,
    audio: audioEnabled,
  })
  const initial = options.source
  if (initial.width % 2 || initial.height % 2)
    throw new Error('Broadcast source dimensions must be even')
  const transport = audioEnabled
    ? await createLiveAudioTransport(
        () => undefined,
        () => undefined,
      )
    : undefined
  const controller = new AbortController()
  const signal = options.signal
    ? AbortSignal.any([options.signal, controller.signal])
    : controller.signal
  let stream: SuiteCutLiveStream
  try {
    stream = createLiveStream(
      options.ffmpegPath,
      config,
      initial.framesPerSecond,
      { width: initial.width, height: initial.height },
      signal,
      undefined,
      transport,
      { width: initial.width, height: initial.height },
      initial.pixelFormat,
    )
  } catch (error) {
    await transport?.close()
    throw error
  }
  let unsubscribeFrame = (): void => undefined
  let unsubscribeAudio = (): void => undefined
  let stopping: Promise<void> | undefined
  const assembler = new NativeAudioAssembler((at, pcm) => transport?.buffer.push(at, pcm))
  const selectSource = (source: NativeBrowser): void => {
    if (signal.aborted || stopping) throw new Error('Native broadcast is closed')
    if (source.state !== 'running') throw new Error('Native source is not running')
    if (
      source.width !== initial.width ||
      source.pixelFormat !== initial.pixelFormat ||
      source.height !== initial.height ||
      source.framesPerSecond !== initial.framesPerSecond
    )
      throw new Error(
        'Native broadcast sources must have matching dimensions, pixel format, and frame rate',
      )
    unsubscribeFrame()
    unsubscribeAudio()
    assembler.clear()
    transport?.buffer.clear()
    unsubscribeFrame = source.onFrame((frame) => stream.update(frame.data, frame.timestampMs))
    if (audioEnabled)
      unsubscribeAudio = source.onAudio((packet: NativeAudioPacket) => assembler.push(packet))
  }
  const stop = (): Promise<void> =>
    (stopping ??= (async () => {
      signal.removeEventListener('abort', onAbort)
      controller.abort()
      unsubscribeFrame()
      unsubscribeAudio()
      try {
        await stream.stop()
      } finally {
        await transport?.close()
      }
    })())
  const onAbort = (): void => {
    void stop().catch(() => undefined)
  }
  signal.addEventListener('abort', onAbort, { once: true })
  try {
    selectSource(initial)
  } catch (error) {
    await stop()
    throw error
  }
  void stream.failure.catch(() => stop().catch(() => undefined))
  return {
    failure: stream.failure,
    ready: () => stream.ready(),
    health: () => stream.health(),
    ...(stream.setBitrate
      ? { setBitrate: (value: number) => stream.setBitrate?.(value) ?? Promise.resolve() }
      : {}),
    selectSource,
    stop,
  }
}

export { createNativePage, NativePage, NativeLocator } from './native-page.js'
export { recordNative } from './native-recording.js'
export type {
  NativeRecordingOptions,
  NativeRecordingContext,
  NativeRecordingCaptureOptions,
} from './native-recording.js'
