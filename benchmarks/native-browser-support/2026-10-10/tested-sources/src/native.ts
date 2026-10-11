import * as z from 'zod'

import { createNativeOutput } from './engine.js'
import { type SuiteCutLiveStream } from './live-stream.js'
import { type NativeBrowser } from './native-browser.js'
import { SuiteCutStreamOptionsSchema } from './schemas.js'

export { launchNativeBrowser } from './native-browser.js'
export type {
  NativeAudioPacket,
  NativeBrowser,
  NativeBrowserOptions,
  NativeBrowserState,
  NativePixelFormat,
  NativeGraphicsMode,
  NativeVideoFrame,
} from './native-browser.js'

export interface NativeBroadcast extends Omit<SuiteCutLiveStream, 'update'> {
  /** A publisher belongs to one native process. Use engine.select() to switch its pages. */
  selectSource(source: NativeBrowser): void
}
export interface NativeBroadcastOptions {
  source: NativeBrowser
  /** Retained for source compatibility; native outputs use linked FFmpeg libraries. */
  ffmpegPath?: string
  stream: NativeStreamOptions
  signal?: AbortSignal
}
const NativeStreamOptionsSchema = SuiteCutStreamOptionsSchema.omit({ audio: true }).extend({
  audio: z.boolean().default(false),
})
export type NativeStreamOptions = z.input<typeof NativeStreamOptionsSchema>

/** Thin native publisher adapter; media never enters TypeScript. */
export async function createNativeBroadcast(
  options: NativeBroadcastOptions,
): Promise<NativeBroadcast> {
  options.signal?.throwIfAborted()
  const config = NativeStreamOptionsSchema.parse(options.stream)
  if (config.adaptiveBitrate || config.reconnect)
    throw new Error('Native reconnect and adaptive bitrate are not implemented')
  const output = await createNativeOutput(
    options.source,
    {
      destination: config.url,
      audio: config.audio,
      ...(config.size ?? {}),
      ...(config.bitrateKbps ? { bitrateKbps: config.bitrateKbps } : {}),
    },
    true,
  )
  let stopped: Promise<void> | undefined
  const stop = () =>
    (stopped ??= output
      .stop()
      .then((status) => {
        if (status.error) throw new Error(status.error)
      })
      .finally(() => {
        options.signal?.removeEventListener('abort', onAbort)
      }))
  const onAbort = () => {
    void stop().catch(() => undefined)
  }
  options.signal?.addEventListener('abort', onAbort, { once: true })
  if (options.signal?.aborted) onAbort()
  return {
    failure: output.failure,
    health: () => output.health(),
    ready: () => Promise.resolve(),
    stop,
    selectSource(source) {
      if (source !== options.source)
        throw new Error(
          'Separate CEF processes cannot share a native encoder; use engine.select() with pages in one engine',
        )
    },
  }
}
export { createNativePage, NativePage, NativeLocator } from './native-page.js'
export { recordNative } from './native-recording.js'
export type {
  NativeRecordingOptions,
  NativeRecordingContext,
  NativeRecordingCaptureOptions,
} from './native-recording.js'
export { launchEngine, connectCEF, createNativeOutput } from './engine.js'
export type {
  SuiteCutEngine,
  NativeOutput,
  NativeOutputOptions,
  NativeOutputStatus,
  NativeVideoCodec,
  NativeAudioCodec,
  NativeOutputContainer,
  NativeEncoderInfo,
} from './engine.js'
