// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { OutputFrameRateSchema } from './frame-rate.js'
import { type NativeBrowser } from './native-browser.js'
import { type NativeStreamingInfo } from './native-streaming-protocol.js'
import { createNativePublisher, type NativePublisher } from './native-streaming.js'
import { SuiteCutStreamOptionsSchema } from './schemas.js'

/** Native media controls; actual progress is read asynchronously from the binary. */
export interface NativeBroadcast extends Omit<NativePublisher, 'stop'> {
  ready(): Promise<void>
  health(): Promise<NativeStreamingInfo>
  stop(): Promise<void>
}

export interface NativeBroadcastOptions {
  source: NativeBrowser
  ffmpegPath: string
  stream: NativeStreamOptions
  signal?: AbortSignal
}

const NativeStreamOptionsSchema = SuiteCutStreamOptionsSchema.omit({ audio: true }).extend({
  framesPerSecond: OutputFrameRateSchema.exactOptional(),
  captureLatencyMs: z.number().int().min(20).max(1000).exactOptional(),
  audio: z.boolean().default(false),
})
export type NativeStreamOptions = z.input<typeof NativeStreamOptionsSchema>

/** C++ owns CEF ingestion, resampling, audio, encoder and network lifetime. */
export async function createNativeBroadcast(
  options: NativeBroadcastOptions,
): Promise<NativeBroadcast> {
  options.signal?.throwIfAborted()
  const config = NativeStreamOptionsSchema.parse(options.stream)
  if (config.adaptiveBitrate === true)
    throw new Error('Native broadcast adaptive bitrate is not supported; use a fixed bitrateKbps')
  if (config.onDiagnostic !== undefined)
    throw new Error(
      'Native broadcast onDiagnostic is not supported; use asynchronous status() or health()',
    )
  const publisher = await createNativePublisher({
    source: options.source,
    ffmpegPath: options.ffmpegPath,
    publisher: {
      destination: config.url,
      ...(config.reconnect ? { reconnect: config.reconnect } : {}),
      videoCodec: 'h264',
      audioCodec: 'aac',
      container: 'flv',
      audio: config.audio,
      ...(config.captureLatencyMs === undefined
        ? {}
        : { captureLatencyMs: config.captureLatencyMs }),
      ...(config.framesPerSecond === undefined ? {} : { framesPerSecond: config.framesPerSecond }),
      ...(config.bitrateKbps === undefined ? {} : { bitrateKbps: config.bitrateKbps }),
      ...(config.size === undefined
        ? {}
        : { width: config.size.width, height: config.size.height }),
    },
    ...(options.signal === undefined ? {} : { signal: options.signal }),
  })
  return {
    failure: publisher.failure,
    ready: () => Promise.resolve(),
    status: () => publisher.status(),
    health: () => publisher.status(),
    selectSource: (source) => publisher.selectSource(source),
    pause: () => publisher.pause(),
    resume: () => publisher.resume(),
    cancel: () => publisher.cancel(),
    stop: async () => {
      await publisher.stop()
    },
  }
}
