// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'

import * as z from 'zod'

import { OutputFrameRateSchema } from './frame-rate.js'
import { type NativeBrowser } from './native-browser.js'
import { NativeStreamingInfoSchema, type NativeStreamingInfo } from './native-streaming-protocol.js'
import { toError, type UntrustedInput } from './untrusted.js'

interface PublisherTransport {
  runStreaming(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
  onStreamingMessage(listener: (info: NativeStreamingInfo) => void): () => void
}

const NativeReconnectPolicySchema = z
  .strictObject({
    /** Replacement attempts per actual outage, bounded independently of lifetime history. */
    maxAttempts: z.number().int().min(1).max(8).default(3),
    initialDelayMs: z.number().int().min(0).max(5000).default(250),
    maxDelayMs: z.number().int().min(0).max(30_000).default(5000),
    timeoutMs: z.number().int().min(1).max(120_000).default(30_000),
  })
  .refine(
    (value) => value.maxDelayMs >= value.initialDelayMs,
    'Reconnect maxDelayMs must cover initialDelayMs',
  )

const PublisherConfigSchema = z.strictObject({
  destination: z.string().min(1).max(4096),
  reconnect: z.union([z.literal(false), NativeReconnectPolicySchema]).exactOptional(),
  videoCodec: z.enum(['h264', 'vp9']).default('h264'),
  audioCodec: z.enum(['aac', 'opus']).default('aac'),
  container: z.enum(['flv', 'webm']).default('flv'),
  framesPerSecond: OutputFrameRateSchema.exactOptional(),
  width: z.number().int().min(2).max(8192).exactOptional(),
  height: z.number().int().min(2).max(8192).exactOptional(),
  audio: z.boolean().default(false),
  bitrateKbps: z.number().int().min(100).max(50_000).default(4500),
  captureLatencyMs: z.number().int().min(20).max(1000).default(120),
  maxWriteLagMs: z.number().int().min(1).max(30_000).default(2000),
  tcpSendBufferBytes: z
    .number()
    .int()
    .min(0)
    .max(4 * 1024 * 1024)
    .default(0),
  timeoutMs: z.number().finite().min(0).max(2_147_483_647).default(30_000),
})
export type NativePublisherConfig = z.input<typeof PublisherConfigSchema>
export interface NativePublisher {
  readonly failure: Promise<never>
  /** Actual native/FFmpeg counters; muxed bytes do not imply remote receipt. */
  status(): Promise<NativeStreamingInfo>
  pause(): Promise<NativeStreamingInfo>
  resume(): Promise<NativeStreamingInfo>
  /** Initial native publisher supports actual pages in one helper/Engine. */
  selectSource(source: NativeBrowser): Promise<NativeStreamingInfo>
  stop(): Promise<NativeStreamingInfo>
  cancel(): Promise<NativeStreamingInfo>
}

/** Thin controls for native continuous resampling, audio and physical publisher ownership. */
export async function createNativePublisher(options: {
  source: NativeBrowser & Partial<PublisherTransport>
  ffmpegPath: string
  publisher: NativePublisherConfig
  signal?: AbortSignal
}): Promise<NativePublisher> {
  const source = options.source
  const config = PublisherConfigSchema.parse(options.publisher)
  options.signal?.throwIfAborted()
  if (!source.runStreaming || !source.onStreamingMessage)
    throw new Error('Native publisher transport is unavailable')
  if (source.state !== 'running') throw new Error('Native publisher source is not running')
  const run = source.runStreaming.bind(source)
  const clientToken = randomUUID()
  let id: string | undefined
  let terminal: NativeStreamingInfo | undefined
  let stopping: Promise<NativeStreamingInfo> | undefined
  let rejectFailure: (error: Error) => void = () => undefined
  const failure = new Promise<never>((_, reject) => {
    rejectFailure = reject
  })
  void failure.catch(() => undefined)
  let release = (): void => undefined
  let earlyTerminal: NativeStreamingInfo | undefined
  const received = (info: NativeStreamingInfo): void => {
    if (info.clientToken !== clientToken) return
    if (!id) {
      if (['complete', 'failed'].includes(info.phase)) earlyTerminal = info
      return
    }
    if (info.id !== id || !['complete', 'failed'].includes(info.phase)) return
    terminal = info
    if (info.failure) rejectFailure(new Error(info.failure))
    release()
  }
  const detach = source.onStreamingMessage(received)
  release = detach
  let first: NativeStreamingInfo
  try {
    const { timeoutMs, reconnect, ...nativeConfig } = config
    first = NativeStreamingInfoSchema.parse(
      await run(
        {
          ...nativeConfig,
          ...(reconnect ? { reconnect } : {}),
          action: 'start',
          clientToken,
          ffmpegPath: options.ffmpegPath,
          width: config.width ?? source.width,
          height: config.height ?? source.height,
          framesPerSecond: config.framesPerSecond ?? source.framesPerSecond,
          timeoutMs,
        },
        options.signal,
      ),
    )
    if (
      first.failure ||
      first.phase !== 'streaming' ||
      first.firstTimestampMs < 0 ||
      !first.encodedProgressObserved ||
      first.encodedFrames === 0
    )
      throw new Error(first.failure || 'Native publisher did not acknowledge encoded readiness')
    if (first.clientToken !== clientToken || (earlyTerminal && earlyTerminal.id !== first.id))
      throw new Error('Native publisher start correlation did not match its actual stream')
    id = first.id
    if (earlyTerminal) received(earlyTerminal)
    earlyTerminal = undefined
  } catch (error) {
    detach()
    throw error
  }
  const command = async (
    action: string,
    fields: Record<string, UntrustedInput> = {},
  ): Promise<NativeStreamingInfo> =>
    NativeStreamingInfoSchema.parse(
      await run({ action, streamId: id, timeoutMs: config.timeoutMs, ...fields }),
    )
  const cleanup = (): void => {
    detach()
    options.signal?.removeEventListener('abort', abort)
    source.signal.removeEventListener('abort', sourceEnded)
  }
  const closedError = (): Error =>
    source.state === 'failed' && source.signal.reason instanceof Error
      ? source.signal.reason
      : new Error('Native publisher source closed without physical completion')
  const cancel = async (): Promise<NativeStreamingInfo> => {
    try {
      terminal ??= await command('cancel')
      return terminal
    } finally {
      cleanup()
    }
  }
  const abort = (): void => {
    void cancel().catch((error: UntrustedInput) => rejectFailure(toError(error)))
  }
  const sourceEnded = (): void => {
    if (terminal) {
      cleanup()
      return
    }
    if (source.state === 'closing') {
      void source.closed.then(() => {
        if (!terminal) rejectFailure(closedError())
        cleanup()
      })
      return
    }
    rejectFailure(closedError())
    cleanup()
  }
  release = cleanup
  options.signal?.addEventListener('abort', abort, { once: true })
  source.signal.addEventListener('abort', sourceEnded, { once: true })
  if (terminal) cleanup()
  if (options.signal?.aborted && !terminal) abort()
  if (source.signal.aborted) sourceEnded()
  return {
    failure,
    status: () => (terminal ? Promise.resolve(terminal) : command('status')),
    pause: () => command('pause'),
    resume: () => command('resume'),
    selectSource: (selected) => {
      if (terminal || stopping) return Promise.reject(new Error('Native publisher has completed'))
      if (!selected.pageId || source.pageSource?.(selected.pageId) !== selected)
        return Promise.reject(
          new Error('Native publisher requires a page in the same native Engine'),
        )
      return command('select', { sourcePageId: selected.pageId })
    },
    cancel,
    stop: () =>
      (stopping ??= (async () => {
        try {
          if (!terminal && source.state === 'closing') {
            await source.closed
            if (!terminal) throw closedError()
          }
          const result = terminal ?? (await command('stop'))
          terminal = result
          if (result.failure) throw new Error(result.failure)
          return result
        } finally {
          cleanup()
        }
      })()),
  }
}
