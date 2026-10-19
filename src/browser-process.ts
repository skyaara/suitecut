// SPDX-License-Identifier: MIT
import { spawn } from 'node:child_process'
import { constants as fsConstants } from 'node:fs'
import { mkdtemp, open, readFile, readdir, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { type LaunchProtocolBrowserOptions } from './browser-protocol-types.js'

const OUTPUT_LIMIT = 16 * 1024
const TERMINATE_GRACE_MS = 1_000
const DESCENDANT_DRAIN_MS = 250

interface LinuxProcessStat {
  state: string
  group: number
  session: number
  started: bigint
}

async function linuxProcessStat(pid: number): Promise<LinuxProcessStat | undefined> {
  let text: string
  try {
    text = await readFile(`/proc/${pid}/stat`, 'utf8')
  } catch (error) {
    if (
      error instanceof Error &&
      'code' in error &&
      (error.code === 'ENOENT' || error.code === 'ESRCH')
    )
      return undefined
    throw error
  }
  // comm can contain spaces and closing parentheses. The remaining fields start
  // after its final delimiter; starttime (field 22) fences a reused leader PID.
  const fields = text
    .slice(text.lastIndexOf(') ') + 2)
    .trim()
    .split(/\s+/)
  const state = fields[0]
  const started = fields[19]
  if (!state || !/^[A-Za-z]$/.test(state) || !started || !/^\d+$/.test(started))
    throw new Error('Invalid Linux process stat')
  const group = Number(fields[2])
  const session = Number(fields[3])
  if (!Number.isSafeInteger(group) || !Number.isSafeInteger(session))
    throw new Error('Invalid Linux process identity')
  return { state, group, session, started: BigInt(started) }
}

async function linuxGroupHasLiveMembers(
  group: number,
  leader: LinuxProcessStat,
): Promise<boolean | undefined> {
  try {
    const currentLeader = await linuxProcessStat(group)
    if (currentLeader?.started !== undefined && currentLeader.started !== leader.started)
      return false
    for (const entry of await readdir('/proc')) {
      if (!/^\d+$/.test(entry)) continue
      const member = await linuxProcessStat(Number(entry))
      if (member?.group !== group) continue
      // A detached launch creates its own session as well as its process group.
      // Never signal a later, unrelated owner of the same numeric group ID.
      if (member.session !== group || member.started < leader.started) return false
      if (member.state !== 'Z' && member.state !== 'X' && member.state !== 'x') return true
    }
    // Zombies retain a process group until their actual parent reaps them, but
    // have no remaining work and cannot be helped by a grace period or SIGKILL.
    return false
  } catch {
    // /proc may be absent or restricted. Preserve the portable group probe and
    // bounded shutdown grace instead of treating an incomplete scan as empty.
    return undefined
  }
}

export interface ProtocolBrowserProcess {
  readonly endpointURL: string
  /** Resolves after the child exits and its temporary profile has been removed. */
  readonly closed: Promise<void>
  close(): Promise<void>
}

function abortError(signal: AbortSignal): Error {
  return signal.reason instanceof Error
    ? signal.reason
    : new DOMException('Browser launch was aborted', 'AbortError')
}

function validateOptions(options: LaunchProtocolBrowserOptions): void {
  if (options.backend !== 'cdp' && options.backend !== 'bidi') {
    throw new Error('Browser backend must be cdp or bidi')
  }
  if (typeof options.executablePath !== 'string' || !options.executablePath.trim()) {
    throw new Error('A browser executablePath is required')
  }
  if (
    options.timeoutMs !== undefined &&
    (!Number.isFinite(options.timeoutMs) || options.timeoutMs <= 0)
  ) {
    throw new Error('Browser launch timeoutMs must be positive and finite')
  }
  if (options.args !== undefined && !Array.isArray(options.args))
    throw new Error('Browser args must be an array')
  for (const arg of options.args ?? []) {
    if (typeof arg !== 'string' || arg.includes('\0'))
      throw new Error('Browser args must be strings without null bytes')
    if (
      arg === '--' ||
      arg.startsWith('-P') ||
      /^-{1,2}(?:remote-debugging-(?:port|address|host|pipe)|user-data-dir|profile(?:-directory|-manager)?|profilemanager|p)(?:=|$)/i.test(
        arg,
      )
    ) {
      throw new Error(
        `Browser launch manages the debugging endpoint and temporary profile; argument is not allowed: ${arg}`,
      )
    }
  }
}

function endpointFromLine(
  line: string,
  backend: LaunchProtocolBrowserOptions['backend'],
): string | undefined {
  const prefix = backend === 'cdp' ? 'DevTools listening on ' : 'WebDriver BiDi listening on '
  const offset = line.indexOf(prefix)
  if (offset < 0) return undefined
  const candidate = line
    .slice(offset + prefix.length)
    .trim()
    .split(/\s/, 1)[0]
  if (!candidate) return undefined
  try {
    const url = new URL(candidate)
    if (
      url.protocol !== 'ws:' ||
      !['127.0.0.1', 'localhost', '[::1]'].includes(url.hostname) ||
      !url.port ||
      url.username ||
      url.password
    )
      return undefined
    if (backend === 'cdp' && !/^\/devtools\/browser\/[^/]+$/.test(url.pathname)) return undefined
    if (backend === 'bidi' && url.pathname === '/') url.pathname = '/session'
    return url.href
  } catch {
    return undefined
  }
}

/** Launches a browser directly, without a shell, driver server, or shared profile. */
export async function launchProtocolProcess(
  options: LaunchProtocolBrowserOptions,
): Promise<ProtocolBrowserProcess> {
  validateOptions(options)
  if (options.signal?.aborted) throw abortError(options.signal)
  const profile = await mkdtemp(join(tmpdir(), 'suitecut-protocol-browser-'))
  if (options.signal?.aborted) {
    await rm(profile, { recursive: true, force: true })
    throw abortError(options.signal)
  }
  const args = [...(options.args ?? [])]
  if (options.backend === 'cdp') {
    args.push(
      '--remote-debugging-port=0',
      '--remote-debugging-address=127.0.0.1',
      `--user-data-dir=${profile}`,
      '--no-first-run',
      '--no-default-browser-check',
    )
    if (options.headless !== false) args.push('--headless=new')
  } else {
    args.push('--remote-debugging-port=0', '--profile', profile)
    if (options.headless !== false) args.push('--headless')
  }

  let child: ReturnType<typeof spawn>
  try {
    child = spawn(options.executablePath, args, {
      stdio: ['ignore', 'pipe', 'pipe'],
      windowsHide: true,
      detached: process.platform !== 'win32',
    })
  } catch (error) {
    await rm(profile, { recursive: true, force: true })
    throw error
  }

  const linuxLeader =
    process.platform === 'linux' && child.pid
      ? linuxProcessStat(child.pid).catch(() => undefined)
      : Promise.resolve(undefined)

  let exited = false
  let tail = ''
  let stdoutPending = ''
  let stderrPending = ''
  let polling: ReturnType<typeof setTimeout> | undefined
  let killTimer: ReturnType<typeof setTimeout> | undefined
  let closing: Promise<void> | undefined
  let resolveExit: () => void = () => undefined
  let rejectExit: (error: Error) => void = () => undefined
  const exit = new Promise<void>((resolve, reject) => {
    resolveExit = resolve
    rejectExit = reject
  })
  // Observe rejection immediately: cleanup errors must not become unhandled if the
  // caller is still connecting the protocol when a browser exits spontaneously.
  const closed = exit.then(() =>
    rm(profile, { recursive: true, force: true, maxRetries: 3, retryDelay: 50 }),
  )
  void closed.catch(() => undefined)
  let resolveEndpoint: (endpoint: string) => void = () => undefined
  let rejectEndpoint: (error: Error) => void = () => undefined
  let endpointSettled = false
  const signalChild = (signal: NodeJS.Signals): void => {
    if (!child.pid) return
    try {
      // An isolated process group also owns renderers and utility processes.
      if (process.platform !== 'win32') process.kill(-child.pid, signal)
      else child.kill(signal)
    } catch (error) {
      if (!(error instanceof Error && 'code' in error && error.code === 'ESRCH')) throw error
    }
  }
  const groupAlive = (): boolean => {
    if (process.platform === 'win32' || !child.pid) return false
    try {
      process.kill(-child.pid, 0)
      return true
    } catch (error) {
      if (error instanceof Error && 'code' in error && error.code === 'ESRCH') return false
      throw error
    }
  }
  const drainDescendants = async (): Promise<void> => {
    // A successful browser shutdown may retire its parent before renderers and
    // utility processes finish. Do not turn that normal exit into SIGKILL.
    const deadline = performance.now() + DESCENDANT_DRAIN_MS
    const leader = await linuxLeader
    while (groupAlive()) {
      if (leader && child.pid) {
        if ((await linuxGroupHasLiveMembers(child.pid, leader)) === false) return
      }
      const remaining = deadline - performance.now()
      if (remaining <= 0) {
        signalChild('SIGKILL')
        return
      }
      await new Promise<void>((resolve) => setTimeout(resolve, Math.min(10, remaining)))
    }
  }
  const endpoint = new Promise<string>((resolve, reject) => {
    resolveEndpoint = (value) => {
      if (!endpointSettled) {
        endpointSettled = true
        resolve(value)
      }
    }
    rejectEndpoint = (error) => {
      if (!endpointSettled) {
        endpointSettled = true
        reject(error)
      }
    }
  })
  const finishExit = (): void => {
    if (exited) return
    exited = true
    if (killTimer) clearTimeout(killTimer)
    rejectEndpoint(
      new Error(
        `Browser exited before exposing its ${options.backend} endpoint${tail ? `:\n${tail}` : ''}`,
      ),
    )
    void drainDescendants().then(
      () => {
        child.stdout?.destroy()
        child.stderr?.destroy()
        resolveExit()
      },
      (error: Error) => {
        child.stdout?.destroy()
        child.stderr?.destroy()
        rejectExit(error)
      },
    )
  }
  child.once('exit', finishExit)
  child.once('error', (error) => {
    rejectEndpoint(error)
    if (!child.pid) finishExit()
  })
  const consume = (chunk: Buffer, stream: 'stdout' | 'stderr'): void => {
    if (endpointSettled) return
    const text = chunk.toString('utf8')
    tail = (tail + text).slice(-OUTPUT_LIMIT)
    const pending = ((stream === 'stdout' ? stdoutPending : stderrPending) + text).slice(
      -OUTPUT_LIMIT,
    )
    const lines = pending.split('\n')
    const remainder = lines.pop() ?? ''
    if (stream === 'stdout') stdoutPending = remainder
    else stderrPending = remainder
    if (!endpointSettled)
      for (const line of lines) {
        const value = endpointFromLine(line, options.backend)
        if (value) {
          resolveEndpoint(value)
          break
        }
      }
  }
  child.stdout?.on('data', (chunk: Buffer) => consume(chunk, 'stdout'))
  child.stderr?.on('data', (chunk: Buffer) => consume(chunk, 'stderr'))

  const close = (): Promise<void> => {
    if (closing) return closing
    closing = (async () => {
      if (!exited) {
        signalChild('SIGTERM')
        killTimer = setTimeout(() => {
          if (!exited) signalChild('SIGKILL')
        }, TERMINATE_GRACE_MS)
      }
      await closed
    })()
    return closing
  }
  const abort = (): void => {
    if (options.signal) rejectEndpoint(abortError(options.signal))
  }
  options.signal?.addEventListener('abort', abort, { once: true })
  if (options.signal?.aborted) abort()
  const startupTimer = setTimeout(
    () => {
      const error = new Error(
        `Browser did not expose its ${options.backend} endpoint within ${options.timeoutMs ?? 30_000}ms${tail ? `:\n${tail}` : ''}`,
      )
      error.name = 'TimeoutError'
      rejectEndpoint(error)
    },
    Math.min(options.timeoutMs ?? 30_000, 2_147_483_647),
  )

  // Chrome may suppress its log message. Its per-launch profile also contains
  // an authoritative endpoint file; reads are bounded even for broken executables.
  const poll = async (): Promise<void> => {
    if (endpointSettled || exited) return
    let file: Awaited<ReturnType<typeof open>> | undefined
    try {
      file = await open(
        join(profile, 'DevToolsActivePort'),
        fsConstants.O_RDONLY | fsConstants.O_NONBLOCK,
      )
      if (!(await file.stat()).isFile()) return
      const buffer = Buffer.alloc(4_096)
      const { bytesRead } = await file.read(buffer, 0, buffer.length, 0)
      const [port, path] = buffer.toString('utf8', 0, bytesRead).split(/\r?\n/)
      if (port && /^\d{1,5}$/.test(port) && Number(port) > 0 && Number(port) <= 65535 && path) {
        const value = endpointFromLine(`DevTools listening on ws://127.0.0.1:${port}${path}`, 'cdp')
        if (value && !exited) resolveEndpoint(value)
      }
    } catch {
      // The file normally appears only after browser initialization.
    } finally {
      await file?.close().catch(() => undefined)
      if (!endpointSettled && !exited)
        polling = setTimeout(() => {
          void poll()
        }, 50)
    }
  }
  if (options.backend === 'cdp') void poll()

  try {
    const endpointURL = await endpoint
    if (exited) throw new Error('Browser exited during startup')
    tail = ''
    stdoutPending = ''
    stderrPending = ''
    return { endpointURL, closed, close }
  } catch (error) {
    await close()
    throw error
  } finally {
    clearTimeout(startupTimer)
    if (polling) clearTimeout(polling)
    options.signal?.removeEventListener('abort', abort)
  }
}
