import { spawn } from 'node:child_process'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import {
  open,
  chmod,
  copyFile,
  mkdir,
  mkdtemp,
  readdir,
  rename,
  rm,
  writeFile,
  access,
} from 'node:fs/promises'
import { dirname, join } from 'node:path'
import { Readable, Transform } from 'node:stream'
import { pipeline } from 'node:stream/promises'
import { type ReadableStream } from 'node:stream/web'
import { setTimeout as delay } from 'node:timers/promises'

import { open as openZip, type ZipFile, type Entry } from 'yauzl'

import { MEDIA_BUILDS, MEDIA_BUILD_ID } from './ffmpeg-builds.js'
import { mediaToolDirectory, mediaToolPaths } from './media-tool-paths.js'
import { runProcess } from './process.js'

const WINDOWS_RENAME_RETRY_CODES = new Set(['EACCES', 'EBUSY', 'EPERM'])

/** Publish a verified directory atomically despite short-lived Windows scanner locks. */
export async function publishMediaDirectory(
  source: string,
  destination: string,
  options: {
    platform?: NodeJS.Platform
    renameDirectory?: typeof rename
    wait?: (milliseconds: number) => Promise<void>
  } = {},
): Promise<void> {
  const platform = options.platform ?? process.platform
  const renameDirectory = options.renameDirectory ?? rename
  const wait = options.wait ?? delay
  for (let attempt = 0; ; attempt += 1) {
    try {
      await renameDirectory(source, destination)
      return
    } catch (error) {
      const code = (error as NodeJS.ErrnoException).code
      if (
        platform !== 'win32' ||
        code === undefined ||
        !WINDOWS_RENAME_RETRY_CODES.has(code) ||
        attempt >= 6
      )
        throw error
      await wait(250 * 2 ** attempt)
    }
  }
}

/** Streams an archive to disk and rejects any bytes that differ from the pinned build. */
export async function downloadMediaArchive(
  url: string,
  sha256: string,
  destination: string,
): Promise<void> {
  const signal = AbortSignal.timeout(300_000)
  const response = await fetch(url, { signal, headers: { 'User-Agent': 'SuiteCut/1.0' } })
  if (!response.ok || !response.body) {
    await response.body?.cancel()
    throw new Error(`Media download failed: HTTP ${response.status} for ${url}`)
  }
  const hash = createHash('sha256')
  let bytes = 0
  const meter = new Transform({
    transform(chunk: Buffer, _encoding, callback) {
      bytes += chunk.length
      if (bytes > 512 * 1024 * 1024) {
        callback(new Error('Media archive exceeds 512 MiB'))
        return
      }
      hash.update(chunk)
      callback(null, chunk)
    },
  })
  const file = await open(destination, 'wx')
  try {
    await pipeline(
      Readable.fromWeb(response.body as ReadableStream<Uint8Array>),
      meter,
      file.createWriteStream(),
      { signal },
    )
    if (hash.digest('hex') !== sha256) throw new Error(`SHA-256 mismatch for ${url}`)
  } catch (error) {
    await rm(destination, { force: true })
    throw error
  }
}

/** Extract only executables and notices to flat, controlled paths; reject links and duplicate names. */
export async function extractMediaArchive(archive: string, directory: string): Promise<void> {
  await mkdir(directory, { recursive: true })
  if (archive.endsWith('.tar.xz')) {
    // tar writes selected members to stdout, never paths or links from the archive.
    const listing = await runProcess('tar', ['-tJf', archive], { timeoutMs: 60_000 })
    for (const member of listing.stdout.trim().split('\n')) {
      const name = member.split('/').at(-1) ?? ''
      if (
        !/^(?:ffmpeg|ffprobe|LICENSE(?:\..*)?|COPYING(?:\..*)?|NOTICE(?:\..*)?|README(?:\..*)?)$/u.test(
          name,
        )
      )
        continue
      if (member.startsWith('/') || member.split('/').includes('..'))
        throw new Error('Unsafe media archive member')
      const file = await open(join(directory, name), 'wx')
      const child = spawn('tar', ['-xOJf', archive, '--', member], {
        stdio: ['ignore', 'pipe', 'ignore'],
        signal: AbortSignal.timeout(60_000),
      })
      let bytes = 0
      const meter = new Transform({
        transform(chunk: Buffer, _encoding, callback) {
          bytes += chunk.length
          callback(
            bytes > 512 * 1024 * 1024 ? new Error('Extracted media file exceeds 512 MiB') : null,
            chunk,
          )
        },
      })
      try {
        const [closed] = await Promise.all([
          once(child, 'close'),
          pipeline(child.stdout, meter, file.createWriteStream()),
        ])
        if (closed[0] !== 0 || bytes === 0)
          throw new Error(`Cannot extract media archive member: ${member}`)
      } finally {
        child.kill()
        await file.close()
      }
    }
    return
  }
  const zip = await new Promise<ZipFile>((resolve, reject) => {
    openZip(archive, { lazyEntries: true }, (error, file) => {
      if (error) reject(error)
      else resolve(file)
    })
  })
  try {
    await new Promise<void>((resolve, reject) => {
      zip.once('error', reject)
      zip.once('end', resolve)
      zip.on('entry', (entry: Entry) => {
        void (async () => {
          if (((entry.externalFileAttributes >>> 16) & 0xf000) === 0xa000)
            throw new Error('Media archives must not contain symbolic links')
          const name = entry.fileName.split('/').at(-1) ?? ''
          if (
            !/^(?:ffmpeg|ffprobe)(?:\.exe)?$/u.test(name) &&
            !/^(?:licen[cs]e|copying|notice|readme|copyright)(?:[._-].*)?$/iu.test(name)
          ) {
            zip.readEntry()
            return
          }
          if (entry.uncompressedSize > 512 * 1024 * 1024)
            throw new Error('Extracted media file exceeds 512 MiB')
          const input = await new Promise<Readable>((resolveStream, rejectStream) => {
            zip.openReadStream(entry, (error, stream) => {
              if (error) rejectStream(error)
              else resolveStream(stream)
            })
          })
          const file = await open(join(directory, name), 'wx')
          await pipeline(input, file.createWriteStream())
          zip.readEntry()
        })().catch(reject)
      })
      zip.readEntry()
    })
  } finally {
    zip.close()
  }
}

/** Checks the baseline codecs and protocols used by SuiteCut, without contacting the network. */
export async function verifyMediaTools(
  paths: { ffmpeg: string; ffprobe: string },
  managed = false,
): Promise<void> {
  const commands = [
    [paths.ffmpeg, '-version'],
    [paths.ffprobe, '-version'],
    [paths.ffmpeg, '-encoders'],
    [paths.ffmpeg, '-protocols'],
  ] as const
  const results = await Promise.all(
    commands.map(([executable, argument]) =>
      runProcess(executable, ['-hide_banner', argument], { timeoutMs: 15_000 }),
    ),
  )
  for (const result of results)
    if (result.exitCode !== 0) throw new Error(`Media tool failed: ${result.executable}`)
  if (
    managed &&
    results
      .slice(0, 2)
      .some((result) => `${result.stdout}${result.stderr}`.includes('--enable-nonfree'))
  )
    throw new Error('Managed media tools must not be built with --enable-nonfree')
  const encoders = results[2]?.stdout ?? ''
  for (const codec of ['libx264', 'aac'])
    if (!new RegExp(`\\b${codec}\\b`, 'u').test(encoders))
      throw new Error(`FFmpeg is missing the ${codec} encoder`)
  const protocols = (results[3]?.stdout ?? '').split(/\s+/u)
  for (const protocol of ['file', 'pipe', 'http', 'rtmp', 'rtmps'])
    if (!protocols.includes(protocol)) throw new Error(`FFmpeg is missing the ${protocol} protocol`)
}

async function findBinary(directory: string, name: string): Promise<string | undefined> {
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const path = join(directory, entry.name)
    if (entry.isFile() && entry.name === name) return path
    if (entry.isDirectory()) {
      const found = await findBinary(path, name)
      if (found) return found
    }
  }
  return undefined
}

/** Explicit setup only. A validated pair is published atomically; failed downloads leave no cache entry. */
export async function installMediaTools(): Promise<{ ffmpeg: string; ffprobe: string }> {
  const platform = `${process.platform}-${process.arch}`
  const archives = MEDIA_BUILDS[platform]
  if (!archives)
    throw new Error(
      `No managed FFmpeg build for ${platform}. Install system FFmpeg and FFprobe or configure their paths.`,
    )
  const directory = mediaToolDirectory()
  const paths = mediaToolPaths(directory)
  const marker = join(directory, 'installation.json')
  const installed = await access(marker).then(
    () => true,
    () => false,
  )
  if (installed) {
    await verifyMediaTools(paths, true)
    return paths
  }
  await mkdir(dirname(directory), { recursive: true })
  const lock = `${directory}.lock`
  try {
    await mkdir(lock)
  } catch (error) {
    if ((error as NodeJS.ErrnoException).code === 'EEXIST')
      throw new Error(
        `Another media installation holds ${lock}. Retry after it finishes; remove this lock if that installer was terminated.`,
        { cause: error },
      )
    throw error
  }
  let staging: string | undefined
  try {
    // Another process may have completed between the first check and acquiring the lock.
    if (
      await access(marker).then(
        () => true,
        () => false,
      )
    ) {
      await verifyMediaTools(paths, true)
      return paths
    }
    staging = await mkdtemp(join(dirname(directory), '.install-'))
    const output = join(staging, 'ready')
    await mkdir(output)
    const stagedPaths = mediaToolPaths(output)
    const found = new Set<string>()
    for (const [index, archive] of archives.entries()) {
      const zip = join(staging, `${index}${archive.url.endsWith('.tar.xz') ? '.tar.xz' : '.zip'}`)
      await downloadMediaArchive(archive.url, archive.sha256, zip)
      const unpacked = join(staging, `archive-${index}`)
      await extractMediaArchive(zip, unpacked)
      for (const name of ['ffmpeg', 'ffprobe'] as const) {
        const binary = await findBinary(
          unpacked,
          process.platform === 'win32' ? `${name}.exe` : name,
        )
        if (!binary) continue
        await copyFile(binary, stagedPaths[name])
        await chmod(stagedPaths[name], 0o755)
        await rm(binary)
        found.add(name)
      }
      // Preserve the upstream licensing and build notices.
      await rm(zip)
      await rename(unpacked, join(output, `upstream-${index}`))
    }
    if (found.size !== 2)
      throw new Error('The pinned archives did not contain both FFmpeg and FFprobe')
    await verifyMediaTools(stagedPaths, true)
    await writeFile(
      join(output, 'installation.json'),
      JSON.stringify({ build: MEDIA_BUILD_ID, platform, archives }, null, 2),
    )
    await publishMediaDirectory(output, directory)
    return paths
  } finally {
    if (staging) await rm(staging, { recursive: true, force: true })
    await rm(lock, { recursive: true, force: true })
  }
}
