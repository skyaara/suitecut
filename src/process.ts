import { spawn } from 'node:child_process'
import { constants as fsConstants } from 'node:fs'
import { access, readdir } from 'node:fs/promises'
import { homedir } from 'node:os'
import { basename, dirname, join } from 'node:path'

export interface SuiteCutProcessResult {
  executable: string
  args: string[]
  exitCode: number | null
  signal: NodeJS.Signals | null
  stdout: string
  stderr: string
}

export function runProcess(
  executable: string,
  args: readonly string[],
  options: { cwd?: string } = {},
): Promise<SuiteCutProcessResult> {
  return new Promise((resolve, reject) => {
    const child = spawn(executable, [...args], {
      cwd: options.cwd,
      env: process.env,
      shell: false,
      stdio: ['ignore', 'pipe', 'pipe'],
    })
    const stdout: Buffer[] = []
    const stderr: Buffer[] = []
    child.stdout.on('data', (chunk: Buffer) => stdout.push(chunk))
    child.stderr.on('data', (chunk: Buffer) => stderr.push(chunk))
    child.once('error', reject)
    child.once('close', (exitCode, signal) => {
      resolve({
        executable,
        args: [...args],
        exitCode,
        signal,
        stdout: Buffer.concat(stdout).toString('utf8'),
        stderr: Buffer.concat(stderr).toString('utf8'),
      })
    })
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
  try {
    return (await runProcess(executable, ['-version'])).exitCode === 0
  } catch {
    return false
  }
}

async function playwrightFfmpegPath(): Promise<string | undefined> {
  const cacheRoot =
    process.platform === 'darwin'
      ? join(homedir(), 'Library', 'Caches', 'ms-playwright')
      : join(homedir(), '.cache', 'ms-playwright')
  let entries: string[]
  try {
    entries = await readdir(cacheRoot)
  } catch {
    return undefined
  }

  const executableName =
    process.platform === 'win32'
      ? 'ffmpeg-win64.exe'
      : process.platform === 'darwin'
        ? 'ffmpeg-mac'
        : 'ffmpeg-linux'
  const candidates = entries
    .filter((entry) => entry.startsWith('ffmpeg-'))
    .sort()
    .reverse()
    .map((entry) => join(cacheRoot, entry, executableName))
  for (const candidate of candidates) {
    if (await isExecutable(candidate)) return candidate
  }
  return undefined
}

export async function resolveFfmpeg(explicitPath?: string): Promise<string> {
  const configuredPath = explicitPath ?? process.env.SUITECUT_FFMPEG_PATH
  if (configuredPath !== undefined) {
    if (await isExecutable(configuredPath)) return configuredPath
    throw new Error(`FFmpeg is not executable: ${configuredPath}`)
  }

  if (await commandStarts('ffmpeg')) return 'ffmpeg'

  const playwrightPath = await playwrightFfmpegPath()
  if (playwrightPath !== undefined) return playwrightPath
  throw new Error('FFmpeg was not found. Install ffmpeg or set SUITECUT_FFMPEG_PATH.')
}

export async function resolveFfprobe(ffmpegPath?: string): Promise<string> {
  const configuredPath = process.env.SUITECUT_FFPROBE_PATH
  if (configuredPath !== undefined) {
    if (await isExecutable(configuredPath)) return configuredPath
    throw new Error(`FFprobe is not executable: ${configuredPath}`)
  }

  if (await commandStarts('ffprobe')) return 'ffprobe'

  const executable = await resolveFfmpeg(ffmpegPath)
  if (executable !== 'ffmpeg') {
    const name = basename(executable).replace(/^ffmpeg/u, 'ffprobe')
    const sibling = join(dirname(executable), name)
    if (await isExecutable(sibling)) return sibling
  }
  throw new Error('FFprobe was not found. Install ffmpeg or set SUITECUT_FFPROBE_PATH.')
}
