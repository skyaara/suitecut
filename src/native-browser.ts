import { spawn } from 'node:child_process'
import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { isAbsolute, join } from 'node:path'

import * as z from 'zod'

import { raceWithAbort } from './abort.js'
import { NativePacketDecoder, NATIVE_PROTOCOL_VERSION } from './native-protocol.js'
import { type UntrustedInput } from './untrusted.js'

const LaunchSchema = z
  .strictObject({
    executablePath: z
      .string()
      .refine(
        (value) => isAbsolute(value) && !value.includes('\0'),
        'must be an absolute executable path without null bytes',
      ),
    width: z.number().int().min(2).max(3840).default(1920),
    height: z.number().int().min(2).max(2160).default(1080),
    deviceScaleFactor: z.union([z.literal(1), z.literal(2)]).default(1),
    framesPerSecond: z.union([z.literal(30), z.literal(60)]).default(60),
    pixelFormat: z.enum(['bgra', 'i420']).default('bgra'),
    graphics: z.enum(['auto', 'swiftshader']).default('auto'),
    webrtcPolicy: z.enum(['default', 'disable-non-proxied-udp']).default('default'),
    startupTimeoutMs: z.number().int().min(100).max(120_000).default(30_000),
    signal: z.instanceof(AbortSignal).optional(),
  })
  .refine(
    (v) =>
      Math.round(v.width * v.deviceScaleFactor) <= 3840 &&
      Math.round(v.height * v.deviceScaleFactor) <= 2160,
    'physical frame size must not exceed 3840x2160',
  )

export type NativeWebRTCPolicy = 'default' | 'disable-non-proxied-udp'
export type NativeGraphicsMode = 'auto' | 'swiftshader'
export type NativePixelFormat = 'bgra' | 'i420'
export type NativeBrowserState = 'running' | 'closing' | 'closed' | 'failed'

export interface NativeBrowserOptions {
  /** Absolute path to the separately built suitecut-browser executable. */
  executablePath: string
  width?: number
  height?: number
  deviceScaleFactor?: 1 | 2
  framesPerSecond?: 30 | 60
  /** BGRA8 for exact pixels; planar BT.709 limited-range I420 for lower-bandwidth streaming. */
  pixelFormat?: NativePixelFormat
  /** Auto uses Chromium defaults; SwiftShader explicitly opts into software WebGL with reduced graphics security protections. */
  graphics?: NativeGraphicsMode
  /** Disable unproxied UDP for proxy-based RTC; usable TCP/TLS TURN and network access are still required. */
  webrtcPolicy?: NativeWebRTCPolicy
  startupTimeoutMs?: number
  signal?: AbortSignal
}

export interface NativeVideoFrame {
  /** Owned packed BGRA8 or contiguous I420 Y/U/V planes (BT.709 limited range), top-left origin. */
  data: Buffer
  pixelFormat: NativePixelFormat
  width: number
  height: number
  /** Capture time in this Node process's performance.now() clock. */
  timestampMs: number
  /** Cumulative frames replaced by the native writer under backpressure. */
  droppedFrames: number
}

export interface NativeAudioPacket {
  /** Owned interleaved signed 16-bit little-endian stereo PCM at 48 kHz. */
  data: Buffer
  frames: number
  /** Estimated first-sample time in this Node process's performance.now() clock. */
  timestampMs: number
}

export interface NativeBrowser {
  readonly state: NativeBrowserState
  readonly pixelFormat: NativePixelFormat
  readonly width: number
  readonly height: number
  readonly framesPerSecond: 30 | 60
  readonly cefVersion: string
  readonly nativeMedia?: boolean
  command?(command: string, fields?: Record<string, UntrustedInput>): Promise<UntrustedInput>
  onProtocolEvent?(listener: (message: UntrustedInput, bytes?: number) => void): () => void
  /** Rejects on runtime, renderer, audio, protocol, or subscriber failure. */
  readonly failure: Promise<never>
  /** Settles only after the native process and its stdio have closed. */
  readonly closed: Promise<void>
  /** Removable lifecycle observation for reusable caller-owned sources. */
  onFailure?(listener: (error: Error) => void): () => void
  onClose?(listener: () => void): () => void
  /** Navigates an HTTP(S) page and waits for its main-frame load. */
  navigate(url: string): Promise<void>
  /** Executes a DevTools method through private process IPC, without opening a debugging port. */
  sendDevToolsCommand(
    method: string,
    params?: Record<string, UntrustedInput>,
  ): Promise<UntrustedInput>
  /** Subscribers must return promptly; buffers may be retained but must not be modified. */
  onFrame(listener: (frame: NativeVideoFrame) => void): () => void
  onAudio(listener: (packet: NativeAudioPacket) => void): () => void
  /** Idempotent; closes CEF gracefully, then kills the helper if shutdown stalls. */
  close(): Promise<void>
}

/** Launches an isolated CEF source with raw frame/audio capture and private command IPC. */
export async function launchNativeBrowser(options: NativeBrowserOptions): Promise<NativeBrowser> {
  const config = LaunchSchema.parse(options)
  if (config.pixelFormat === 'i420' && (config.width % 2 || config.height % 2))
    throw new Error('I420 sources require even CSS dimensions')
  config.signal?.throwIfAborted()
  const profile = await mkdtemp(join(tmpdir(), 'suitecut-cef-'))
  const launch = () =>
    spawn(
      config.executablePath,
      [
        `--suitecut-width=${config.width}`,
        `--suitecut-height=${config.height}`,
        `--suitecut-scale=${config.deviceScaleFactor}`,
        `--suitecut-fps=${config.framesPerSecond}`,
        `--suitecut-pixel-format=${config.pixelFormat}`,
        `--suitecut-graphics=${config.graphics}`,
        `--suitecut-webrtc-policy=${config.webrtcPolicy}`,
        `--suitecut-profile=${profile}`,
        '--suitecut-frame-subscriptions',
      ],
      { shell: false, stdio: ['pipe', 'pipe', 'pipe'] },
    )
  let child: ReturnType<typeof launch>
  try {
    child = launch()
  } catch (error) {
    await rm(profile, { recursive: true, force: true })
    throw error
  }
  const frames = new Set<(frame: NativeVideoFrame) => void>()
  const audio = new Set<(packet: NativeAudioPacket) => void>()
  const failures = new Set<(error: Error) => void>()
  const closes = new Set<() => void>()
  const pending = new Map<
    number,
    {
      resolve: (value: UntrustedInput) => void
      reject: (error: Error) => void
      timer: NodeJS.Timeout
    }
  >()
  let nextId = 0
  let closing: Promise<void> | undefined
  let stopping = false
  let exited = false
  let fullyClosed = false
  let terminal: Error | undefined
  let lastFrame: NativeVideoFrame | undefined
  let clockOffset: number | undefined
  let cefVersion = ''
  let frameSubscriptions = false
  let audioSubscriptions = false
  let nativeMedia = false
  const protocolEvents = new Set<(message: UntrustedInput, bytes?: number) => void>()
  let rejectFailure: (error: Error) => void = () => undefined
  let resolveReady: () => void = () => undefined
  let resolveClosed: () => void = () => undefined
  const failure = new Promise<never>((_, reject) => {
    rejectFailure = reject
  })
  void failure.catch(() => undefined)
  const ready = new Promise<void>((resolve) => {
    resolveReady = resolve
  })
  const closed = new Promise<void>((resolve) => {
    resolveClosed = resolve
  })
  const fail = (error: Error): void => {
    if (terminal) return
    terminal = error
    rejectFailure(error)
    for (const listener of failures) {
      try {
        listener(error)
      } catch {
        // Observers cannot prevent terminal process cleanup.
      }
    }
    failures.clear()
    for (const request of pending.values()) {
      clearTimeout(request.timer)
      request.reject(error)
    }
    pending.clear()
    if (!exited) child.kill('SIGKILL')
  }
  const decoder = new NativePacketDecoder((header, payload) => {
    if (terminal) return
    if (header.type === 'ready') {
      if (clockOffset !== undefined) throw new Error('Duplicate native handshake')
      clockOffset = performance.now() - header.timestampMs
      cefVersion = header.cefVersion
      frameSubscriptions = header.frameSubscriptions === true
      audioSubscriptions = header.audioSubscriptions === true
      nativeMedia = header.nativeMedia === true
      resolveReady()
      return
    }
    if (clockOffset === undefined) throw new Error('Native packet before handshake')
    if (header.type === 'event') {
      const message = JSON.parse(payload.toString('utf8')) as UntrustedInput
      for (const listener of protocolEvents) listener(message, payload.length)
      return
    }
    if (header.type === 'error') {
      fail(new Error(`Native browser: ${header.error}`))
      return
    }
    if (header.type === 'response') {
      const request = pending.get(header.id)
      if (!request) return // A timed-out command may complete later.
      const result =
        header.ok && payload.length
          ? (JSON.parse(payload.toString('utf8')) as UntrustedInput)
          : undefined
      pending.delete(header.id)
      clearTimeout(request.timer)
      if (header.ok) request.resolve(result)
      else request.reject(new Error(header.error ?? 'Native command failed'))
      return
    }
    if (stopping) return
    if (header.type === 'frame') {
      if (
        header.pixelFormat !== config.pixelFormat ||
        header.width !== Math.round(config.width * config.deviceScaleFactor) ||
        header.height !== Math.round(config.height * config.deviceScaleFactor)
      ) {
        fail(new Error('Native frame dimensions changed unexpectedly'))
        return
      }
      if (frames.size === 0 && frameSubscriptions) return
      // The ready packet's one-way transit time can overestimate the clock
      // offset. A captured video frame cannot arrive from the future. Refine
      // that upper bound without moving the clock forward on delayed packets.
      clockOffset = Math.min(clockOffset ?? 0, performance.now() - header.timestampMs)
      lastFrame = {
        data: payload,
        pixelFormat: header.pixelFormat,
        width: header.width,
        height: header.height,
        timestampMs: header.timestampMs + clockOffset,
        droppedFrames: header.dropped,
      }
      for (const listener of frames) listener(lastFrame)
    } else {
      const packet = {
        data: payload,
        frames: header.frames,
        timestampMs: header.timestampMs + clockOffset,
      }
      for (const listener of audio) listener(packet)
    }
  })
  child.stdout.on('data', (chunk: Buffer) => {
    if (terminal) return
    try {
      decoder.push(chunk)
    } catch {
      fail(new Error('Native browser protocol or subscriber failure'))
    }
  })
  // Drain Chromium diagnostics without exposing page URLs or credentials to logs.
  child.stderr.resume()
  child.stdin.on('error', () => {
    if (!stopping) fail(new Error('Native browser command pipe failed'))
  })
  child.once('error', () => fail(new Error('Could not launch the native browser executable')))
  child.once('close', (code) => {
    exited = true
    try {
      decoder.end()
    } catch {
      if (!stopping) fail(new Error('Native browser output was truncated'))
    }
    if (!stopping) fail(new Error(`Native browser exited unexpectedly (${String(code)})`))
    frames.clear()
    audio.clear()
    protocolEvents.clear()
    lastFrame = undefined
    config.signal?.removeEventListener('abort', abort)
    const finish = () => {
      fullyClosed = true
      resolveClosed()
      for (const listener of closes) {
        try {
          listener()
        } catch {
          // Closing all subscriptions takes precedence over observer errors.
        }
      }
      closes.clear()
      failures.clear()
    }
    void rm(profile, { recursive: true, force: true }).then(finish, () => {
      fail(new Error('Could not remove the native browser temporary profile'))
      finish()
    })
  })
  const request = (
    command: string,
    fields: Record<string, UntrustedInput> = {},
  ): Promise<UntrustedInput> => {
    if (terminal || stopping || exited)
      return Promise.reject(terminal ?? new Error('Native browser is closed'))
    if (pending.size >= 64) return Promise.reject(new Error('Too many pending native commands'))
    const id = ++nextId
    const message =
      JSON.stringify({ ...fields, version: NATIVE_PROTOCOL_VERSION, id, command }) + '\n'
    if (Buffer.byteLength(message) > 4_194_304)
      return Promise.reject(new Error('Native command exceeds 4 MiB'))
    return new Promise((resolve, reject) => {
      const timer = setTimeout(
        () => {
          pending.delete(id)
          reject(new Error('Native browser command timed out'))
        },
        command === 'media.render' || command === 'media.cut' ? 3_600_000 : 30_000,
      )
      pending.set(id, { resolve, reject, timer })
      child.stdin.write(message)
    })
  }
  const close = (): Promise<void> => {
    if (closing) return closing
    stopping = true
    closing = (async () => {
      for (const item of pending.values()) {
        clearTimeout(item.timer)
        item.reject(new Error('Native browser is closing'))
      }
      pending.clear()
      if (!exited) child.stdin.end()
      const timer = setTimeout(() => child.kill('SIGKILL'), 15000)
      try {
        await closed
      } finally {
        clearTimeout(timer)
      }
    })()
    return closing
  }
  const abort = (): void => {
    fail(new Error('Native browser aborted'))
    void close()
  }
  config.signal?.addEventListener('abort', abort, { once: true })
  if (config.signal?.aborted) abort()
  try {
    await raceWithAbort(
      Promise.race([ready, failure]),
      AbortSignal.timeout(config.startupTimeoutMs),
    )
    if (frameSubscriptions) await request('frames', { enabled: false })
    if (audioSubscriptions) await request('audio', { enabled: false })
  } catch (error) {
    await close()
    throw error
  }
  return {
    get state() {
      return terminal ? 'failed' : exited ? 'closed' : stopping ? 'closing' : 'running'
    },
    pixelFormat: config.pixelFormat,
    width: Math.round(config.width * config.deviceScaleFactor),
    height: Math.round(config.height * config.deviceScaleFactor),
    framesPerSecond: config.framesPerSecond,
    cefVersion,
    nativeMedia,
    command: request,
    onProtocolEvent: (listener) => {
      protocolEvents.add(listener)
      return () => {
        protocolEvents.delete(listener)
      }
    },
    failure,
    closed,
    onFailure: (listener) => {
      if (terminal) listener(terminal)
      else if (!fullyClosed) failures.add(listener)
      return () => {
        failures.delete(listener)
      }
    },
    onClose: (listener) => {
      if (fullyClosed) listener()
      else closes.add(listener)
      return () => {
        closes.delete(listener)
      }
    },
    navigate: async (url) => {
      const parsed = new URL(url)
      if (!['http:', 'https:'].includes(parsed.protocol))
        throw new Error('Native navigation requires HTTP(S)')
      await request('navigate', { url: parsed.href })
    },
    sendDevToolsCommand: (method, params = {}) => request('devtools', { method, params }),
    onFrame: (listener) => {
      if (terminal || stopping || exited) throw new Error('Native browser is not running')
      const first = frames.size === 0
      frames.add(listener)
      if (first && frameSubscriptions)
        void request('frames', { enabled: true }).catch((error: Error) => {
          if (!stopping && !terminal) fail(error)
        })
      if (lastFrame) listener(lastFrame)
      return () => {
        frames.delete(listener)
        if (frames.size === 0 && frameSubscriptions && !stopping && !terminal) {
          lastFrame = undefined
          void request('frames', { enabled: false }).catch((error: Error) => {
            if (!stopping && !terminal) fail(error)
          })
        }
      }
    },
    onAudio: (listener) => {
      if (terminal || stopping || exited) throw new Error('Native browser is not running')
      const first = audio.size === 0
      audio.add(listener)
      if (first && audioSubscriptions)
        void request('audio', { enabled: true }).catch((error: Error) => {
          if (!stopping) fail(error)
        })
      return () => {
        audio.delete(listener)
        if (audio.size === 0 && audioSubscriptions && !stopping)
          void request('audio', { enabled: false }).catch((error: Error) => {
            if (!stopping) fail(error)
          })
      }
    },
    close,
  }
}
