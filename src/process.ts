import { spawn, type ChildProcess } from 'node:child_process'
import { constants as fsConstants } from 'node:fs'
import { access } from 'node:fs/promises'
import { basename, dirname, join } from 'node:path'

import { mediaToolPaths } from './media-tool-paths.js'

export interface SuiteCutProcessResult {
  executable: string
  args: string[]
  exitCode: number | null
  signal: NodeJS.Signals | null
  stdout: string
  stderr: string
}

export interface SuiteCutProcessOptions {
  cwd?: string
  signal?: AbortSignal
  timeoutMs?: number
  maxBufferBytes?: number
  forceKillAfterMs?: number
}

const commandProbeCache = new Map<string, Promise<boolean>>()
const DEFAULT_MAX_BUFFER_BYTES = 20 * 1024 * 1024
const DEFAULT_FORCE_KILL_AFTER_MS = 5_000
const COMMAND_PROBE_TIMEOUT_MS = 10_000

function timeoutError(message: string): Error {
  const error = new Error(message)
  error.name = 'TimeoutError'
  return error
}

async function withTimeout<T>(promise: Promise<T>, timeoutMs: number, message: string): Promise<T> {
  let timer: ReturnType<typeof setTimeout> | undefined
  try {
    return await Promise.race([
      promise,
      new Promise<never>((_, reject) => {
        timer = setTimeout(() => reject(timeoutError(message)), timeoutMs)
        timer.unref()
      }),
    ])
  } finally {
    if (timer !== undefined) clearTimeout(timer)
  }
}

/** Waits for graceful exit, then escalates from SIGTERM to SIGKILL. */
export async function waitForProcessExit(
  child: ChildProcess,
  completion: Promise<void>,
  options: { gracefulTimeoutMs?: number; forceKillAfterMs?: number } = {},
): Promise<void> {
  const gracefulTimeoutMs = options.gracefulTimeoutMs ?? 5_000
  const forceKillAfterMs = options.forceKillAfterMs ?? DEFAULT_FORCE_KILL_AFTER_MS

  try {
    await withTimeout(completion, gracefulTimeoutMs, 'Process did not exit after graceful shutdown')
    return
  } catch (error) {
    if (!(error instanceof Error) || error.name !== 'TimeoutError') throw error
  }

  child.kill('SIGTERM')
  try {
    await withTimeout(completion, forceKillAfterMs, 'Process did not exit after SIGTERM')
    return
  } catch (error) {
    if (!(error instanceof Error) || error.name !== 'TimeoutError') throw error
  }

  child.kill('SIGKILL')
  await completion
}

function abortReason(signal: AbortSignal): Error {
  if (signal.reason instanceof Error) return signal.reason
  return new DOMException('The operation was aborted', 'AbortError')
}

function validateProcessOptions(options: SuiteCutProcessOptions): void {
  if (
    options.timeoutMs !== undefined &&
    (!Number.isFinite(options.timeoutMs) || options.timeoutMs <= 0)
  ) {
    throw new Error('Process timeout must be a positive number')
  }
  if (
    options.maxBufferBytes !== undefined &&
    (!Number.isSafeInteger(options.maxBufferBytes) || options.maxBufferBytes <= 0)
  ) {
    throw new Error('Process output limit must be a positive integer')
  }
  if (
    options.forceKillAfterMs !== undefined &&
    (!Number.isFinite(options.forceKillAfterMs) || options.forceKillAfterMs < 0)
  ) {
    throw new Error('Process force-kill delay must be a non-negative number')
  }
}

export function runProcess(
  executable: string,
  args: readonly string[],
  options: SuiteCutProcessOptions = {},
): Promise<SuiteCutProcessResult> {
  validateProcessOptions(options)
  options.signal?.throwIfAborted()

  return new Promise((resolve, reject) => {
    const processSignal = options.signal
    const resolvedArgs = [...args]
    const child = spawn(executable, resolvedArgs, {
      cwd: options.cwd,
      env: process.env,
      shell: false,
      stdio: ['ignore', 'pipe', 'pipe'],
    })
    const stdout: Buffer[] = []
    const stderr: Buffer[] = []
    const maxBufferBytes = options.maxBufferBytes ?? DEFAULT_MAX_BUFFER_BYTES
    const forceKillAfterMs = options.forceKillAfterMs ?? DEFAULT_FORCE_KILL_AFTER_MS
    let bufferedBytes = 0
    let terminationError: Error | undefined
    let timeout: ReturnType<typeof setTimeout> | undefined
    let forceKillTimer: ReturnType<typeof setTimeout> | undefined
    let settled = false

    const clearLifecycleHooks = (): void => {
      if (timeout !== undefined) clearTimeout(timeout)
      if (forceKillTimer !== undefined) clearTimeout(forceKillTimer)
      processSignal?.removeEventListener('abort', onAbort)
    }

    const terminate = (error: Error): void => {
      if (terminationError !== undefined || child.exitCode !== null || child.signalCode !== null)
        return
      terminationError = error
      child.kill('SIGTERM')
      forceKillTimer = setTimeout(() => {
        if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL')
      }, forceKillAfterMs)
      forceKillTimer.unref()
    }

    const onAbort = (): void => {
      if (processSignal !== undefined) terminate(abortReason(processSignal))
    }
    const collect = (destination: Buffer[], chunk: Buffer): void => {
      bufferedBytes += chunk.length
      if (bufferedBytes > maxBufferBytes) {
        terminate(new Error(`Process output exceeded ${String(maxBufferBytes)} bytes`))
        return
      }
      destination.push(chunk)
    }

    child.stdout.on('data', (chunk: Buffer) => collect(stdout, chunk))
    child.stderr.on('data', (chunk: Buffer) => collect(stderr, chunk))
    child.once('error', (error) => {
      if (settled) return
      settled = true
      clearLifecycleHooks()
      reject(error)
    })
    child.once('close', (exitCode, signal) => {
      if (settled) return
      settled = true
      clearLifecycleHooks()
      if (terminationError !== undefined) {
        reject(terminationError)
        return
      }
      resolve({
        executable,
        args: resolvedArgs,
        exitCode,
        signal,
        stdout: Buffer.concat(stdout).toString('utf8'),
        stderr: Buffer.concat(stderr).toString('utf8'),
      })
    })

    processSignal?.addEventListener('abort', onAbort, { once: true })
    if (processSignal?.aborted === true) onAbort()
    if (options.timeoutMs !== undefined) {
      timeout = setTimeout(() => {
        const error = timeoutError(
          `Process timed out after ${String(options.timeoutMs)}ms: ${executable}`,
        )
        terminate(error)
      }, options.timeoutMs)
      timeout.unref()
    }
  })
}

async function isExecutable(path: string): Promise<boolean> {
  try {
    await access(path, fsConstants.X_OK)
    return true
  } catch {
    return false
  }
}

async function commandStarts(executable: string): Promise<boolean> {
  const cacheKey = `${executable}\0${process.env.PATH ?? ''}`
  const cached = commandProbeCache.get(cacheKey)
  if (cached !== undefined) return cached

  const pending = runProcess(executable, ['-version'], {
    timeoutMs: COMMAND_PROBE_TIMEOUT_MS,
  }).then(
    (result) => {
      const started = result.exitCode === 0
      if (!started) commandProbeCache.delete(cacheKey)
      return started
    },
    () => {
      commandProbeCache.delete(cacheKey)
      return false
    },
  )
  commandProbeCache.set(cacheKey, pending)
  return pending
}

export async function resolveFfmpeg(explicitPath?: string): Promise<string> {
  const configuredPath = explicitPath ?? process.env.SUITECUT_FFMPEG_PATH
  if (configuredPath !== undefined) {
    if (await isExecutable(configuredPath)) return configuredPath
    throw new Error(`FFmpeg is not executable: ${configuredPath}`)
  }
  const managed = mediaToolPaths().ffmpeg
  if (managed && (await commandStarts(managed))) return managed
  if (await commandStarts('ffmpeg')) return 'ffmpeg'
  throw new Error(
    'FFmpeg was not found. Run npx suitecut install, install system ffmpeg, or set SUITECUT_FFMPEG_PATH.',
  )
}

export async function resolveFfprobe(ffmpegPath?: string): Promise<string> {
  const configuredPath = process.env.SUITECUT_FFPROBE_PATH
  if (configuredPath !== undefined) {
    if (await isExecutable(configuredPath)) return configuredPath
    throw new Error(`FFprobe is not executable: ${configuredPath}`)
  }
  // Prefer the companion of a custom encoder before falling back to other installations.
  const customFfmpeg = ffmpegPath ?? process.env.SUITECUT_FFMPEG_PATH
  if (customFfmpeg && customFfmpeg !== 'ffmpeg') {
    const name = basename(customFfmpeg).replace(/^ffmpeg/u, 'ffprobe')
    if (name !== basename(customFfmpeg)) {
      const sibling = join(dirname(customFfmpeg), name)
      if (await commandStarts(sibling)) return sibling
    }
  }
  const managed = mediaToolPaths().ffprobe
  if (managed && (await commandStarts(managed))) return managed
  if (await commandStarts('ffprobe')) return 'ffprobe'
  throw new Error(
    'FFprobe was not found. Run npx suitecut install, install system ffmpeg, or set SUITECUT_FFPROBE_PATH.',
  )
}
