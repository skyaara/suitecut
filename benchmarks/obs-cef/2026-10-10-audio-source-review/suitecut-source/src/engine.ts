import { randomUUID } from 'node:crypto'
import { resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'

import { type Browser, type Page, type ConnectOverCDPTransport } from 'playwright'
import * as z from 'zod'

import { MediaSchema } from './manifest.js'
import {
  launchNativeBrowser,
  type NativeBrowser,
  type NativeBrowserOptions,
} from './native-browser.js'
import { renderSuiteCut, type SuiteCutRenderRequest, type SuiteCutRenderReport } from './render.js'
import { type SuiteCutArtifact, type SuiteCutMedia } from './types.js'
import { type UntrustedInput } from './untrusted.js'

const StatusSchema = z.strictObject({
  ready: z.boolean(),
  stopped: z.boolean(),
  paused: z.boolean(),
  videoFrames: z.number(),
  audioSamples: z.number(),
  repeatedFrames: z.number(),
  droppedAudioSamples: z.number(),
  missingAudioSamples: z.number(),
  lateAudioSamples: z.number(),
  queuedVideoBytes: z.number(),
  queuedAudioBytes: z.number(),
  startedMs: z.number(),
  durationMs: z.number(),
  maximumEncodeMs: z.number(),
  error: z.string(),
})
export type NativeOutputStatus = z.infer<typeof StatusSchema>
export type NativeVideoCodec =
  | 'libx264'
  | 'libx265'
  | 'libvpx'
  | 'libvpx-vp9'
  | 'libsvtav1'
  | 'libaom-av1'
  | 'ffv1'
  | 'h264_nvenc'
  | 'hevc_nvenc'
  | 'av1_nvenc'
export type NativeAudioCodec = 'aac' | 'libopus' | 'flac' | 'pcm_s16le'
export type NativeOutputContainer = 'mp4' | 'webm' | 'mkv' | 'mov'
const EncoderInfoSchema = z.strictObject({
  name: z.string(),
  kind: z.enum(['video', 'audio']),
  codec: z.string(),
  /** Linked encoder availability; hardware still requires a working device/driver. */
  linked: z.boolean(),
  hardware: z.boolean(),
  lossless: z.boolean(),
  containers: z.array(z.enum(['mp4', 'webm', 'mkv', 'mov', 'flv'])),
})
export type NativeEncoderInfo = z.infer<typeof EncoderInfoSchema>
export interface NativeOutputOptions {
  destination: string
  width?: number
  height?: number
  fps?: 30 | 60
  audio?: boolean
  videoCodec?: NativeVideoCodec
  audioCodec?: NativeAudioCodec
  container?: NativeOutputContainer
  /** Software lossy recording quality; range depends on encoder. */
  crf?: number
  bitrateKbps?: number
  audioBitrateKbps?: number
  /** Native audio readiness is mandatory when audio is enabled. Timeout: 1–60000 ms. */
  audioStartupTimeoutMs?: number
}
export interface NativeOutput {
  readonly failure: Promise<never>
  selectTarget(targetId: string): Promise<void>
  health(): { captureLagMs: number; outputLagMs: number; progressAgeMs: number }
  status(): Promise<NativeOutputStatus>
  pause(paused: boolean): Promise<void>
  stop(): Promise<NativeOutputStatus>
}
export interface SuiteCutEngine {
  readonly browser: Browser
  readonly source: NativeBrowser
  encoders(): Promise<NativeEncoderInfo[]>
  record(page: Page, options: NativeOutputOptions): Promise<NativeOutput>
  stream(page: Page, options: NativeOutputOptions): Promise<NativeOutput>
  select(output: NativeOutput, page: Page): Promise<void>
  render(request: Omit<SuiteCutRenderRequest, 'nativeSource'>): Promise<SuiteCutRenderReport>
  close(): Promise<void>
}

/** Public Playwright transport over private IPC. No debugging TCP port or raw media. */
export async function connectCEF(source: NativeBrowser): Promise<Browser> {
  const { chromium } = await import('playwright')
  if (!source.command || !source.onProtocolEvent)
    throw new Error('CEF protocol adapter is unavailable')
  let disconnected = false
  const transport: ConnectOverCDPTransport = {
    send(message) {
      void source
        .command?.('cdp', { message: message as Record<string, UntrustedInput> })
        .catch((error: Error) => {
          transport.onclose?.(error.message)
        })
    },
    close() {
      if (disconnected) return
      disconnected = true
      unsubscribe()
      transport.onclose?.('CEF transport closed')
    },
  }
  const unsubscribe = source.onProtocolEvent((message) => transport.onmessage?.(message as object))
  const unsubscribeClose = source.onClose?.(() => transport.close())
  try {
    const browser = await chromium.connectOverCDP(transport)
    browser.on('disconnected', () => {
      unsubscribe()
      unsubscribeClose?.()
    })
    return browser
  } catch (error) {
    unsubscribe()
    unsubscribeClose?.()
    throw error
  }
}

export async function createNativeOutput(
  source: NativeBrowser,
  options: NativeOutputOptions,
  streaming = false,
  targetId?: string,
): Promise<NativeOutput> {
  if (!source.command || !source.nativeMedia) throw new Error('This helper has no C++ media engine')
  const command = source.command.bind(source)
  if ('audioStartup' in options)
    throw new Error('Audio startup policies were removed; realtime audio readiness is mandatory')
  const fields = targetId ? { targetId } : {}
  const config = {
    ...options,
    destination: streaming ? options.destination : resolve(options.destination),
    width: options.width ?? source.width,
    height: options.height ?? source.height,
    fps: options.fps ?? source.framesPerSecond,
    audio: options.audio ?? true,
    streaming,
    ...fields,
  }
  const started = z
    .object({
      outputId: z.number().int().positive(),
      audioReadinessRequired: z.boolean().optional(),
    })
    .parse(await command('media.start', config))
  const { outputId } = started
  if (config.audio && started.audioReadinessRequired !== true) {
    await command('media.stop', { outputId, ...fields }).catch(() => undefined)
    throw new Error('Native helper did not confirm mandatory audio readiness')
  }
  let latest: NativeOutputStatus | undefined
  let receivedAt = performance.now()
  const status = async () => {
    const result = StatusSchema.parse(await command('media.status', { outputId, ...fields }))
    latest = result
    receivedAt = performance.now()
    return result
  }
  let stopped: Promise<NativeOutputStatus> | undefined
  let rejectFailure: (error: Error) => void = () => undefined
  const failure = new Promise<never>((_resolve, reject) => {
    rejectFailure = reject
  })
  void failure.catch(() => undefined)
  let timer: ReturnType<typeof setInterval> | undefined = undefined
  let unsubscribe: (() => void) | undefined = undefined
  let unsubscribeClose: (() => void) | undefined = undefined
  const cleanup = () => {
    clearInterval(timer)
    unsubscribe?.()
    unsubscribeClose?.()
  }
  const fail = (error: Error) => {
    cleanup()
    rejectFailure(error)
  }
  unsubscribe = source.onFailure?.(fail)
  unsubscribeClose = source.onClose?.(() => {
    if (!stopped) fail(new Error('Native engine closed during output'))
  })
  let polling = false
  timer = setInterval(() => {
    if (polling || stopped) return
    polling = true
    void status()
      .then((value) => {
        if (value.error) fail(new Error(value.error))
      })
      .catch(fail)
      .finally(() => {
        polling = false
      })
  }, 1000)
  timer.unref()
  const output: NativeOutput = {
    failure,
    selectTarget: async (targetId) => {
      await command('media.select', { outputId, targetId })
    },
    health: () => ({
      captureLagMs: latest
        ? ((latest.queuedVideoBytes / Math.max(1, config.width * config.height * 1.5)) * 1000) /
          config.fps
        : 0,
      outputLagMs: latest?.maximumEncodeMs ?? 0,
      progressAgeMs: performance.now() - receivedAt,
    }),
    status: () => stopped ?? status(),
    pause: async (paused) => {
      await command('media.pause', { outputId, paused, ...fields })
    },
    stop: () => {
      if (!stopped) {
        cleanup()
        stopped = command('media.stop', { outputId, ...fields }).then((value) => {
          const status = StatusSchema.parse(value)
          if (status.error) {
            const error = new Error(status.error)
            fail(error)
            throw error
          }
          return status
        })
      }
      return stopped
    },
  }
  try {
    const startupDeadline =
      performance.now() + Math.max(10000, (options.audioStartupTimeoutMs ?? 5000) + 5000)
    while (performance.now() < startupDeadline) {
      const value = await status()
      if (value.error) throw new Error(value.error)
      if (value.ready) return output
      await delay(50)
    }
    throw new Error('Native output startup timed out')
  } catch (error) {
    await output.stop().catch(() => undefined)
    throw error
  }
}

/** C++ owns CEF, encoding, audio timelines and muxing; TypeScript sends commands. */
export async function launchEngine(options: NativeBrowserOptions): Promise<SuiteCutEngine> {
  const source = await launchNativeBrowser({ ...options, pixelFormat: 'i420' })
  try {
    const browser = await connectCEF(source)
    const output = async (page: Page, config: NativeOutputOptions, streaming: boolean) => {
      if (page.context().browser() !== browser) throw new Error('Page belongs to another engine')
      const session = await page.context().newCDPSession(page)
      try {
        const { targetInfo } = await session.send('Target.getTargetInfo')
        return await createNativeOutput(source, config, streaming, targetInfo.targetId)
      } finally {
        await session.detach()
      }
    }
    const select = async (output: NativeOutput, page: Page) => {
      if (page.context().browser() !== browser) throw new Error('Page belongs to another engine')
      const session = await page.context().newCDPSession(page)
      try {
        const { targetInfo } = await session.send('Target.getTargetInfo')
        await output.selectTarget(targetInfo.targetId)
      } finally {
        await session.detach()
      }
    }
    return {
      browser,
      source,
      encoders: async () => {
        if (!source.command) throw new Error('Native control adapter is unavailable')
        return z
          .object({ encoders: z.array(EncoderInfoSchema) })
          .parse(await source.command('media.encoders')).encoders
      },
      select,
      render: (request) => renderSuiteCut({ ...request, nativeSource: source }),
      record: (page, config) => output(page, config, false),
      stream: (page, config) => output(page, config, true),
      close: () => source.close(),
    }
  } catch (error) {
    await source.close()
    throw error
  }
}

/** Reads measured metadata directly through libavformat in the native engine. */
export async function probeNativeMedia(
  source: NativeBrowser,
  artifact: SuiteCutArtifact,
  path: string,
): Promise<SuiteCutMedia> {
  if (!source.command) throw new Error('Native media commands are unavailable')
  const result = (await source.command('media.probe', { input: resolve(path) })) as Record<
    string,
    UntrustedInput
  >
  return MediaSchema.parse({
    id: `media-${randomUUID()}`,
    artifactId: artifact.id,
    formatName: result.formatName,
    durationMs: result.durationMs,
    streams: result.streams,
  })
}
