import { execFileSync } from 'node:child_process'
import { createHash } from 'node:crypto'
import { mkdir, mkdtemp, readFile, readdir, rm, symlink, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { MEDIA_BUILDS } from '../src/ffmpeg-builds.js'
import { mediaToolDirectory } from '../src/media-tool-paths.js'
import {
  downloadMediaArchive,
  extractMediaArchive,
  installMediaTools,
  publishMediaDirectory,
} from '../src/media-tools.js'

let directory: string
const platform = `${process.platform}-${process.arch}`
const originalBuild = MEDIA_BUILDS[platform]

beforeEach(async () => {
  directory = await mkdtemp(join(tmpdir(), 'suitecut-media-test-'))
  vi.stubEnv('SUITECUT_MEDIA_CACHE', directory)
})
afterEach(async () => {
  if (originalBuild) MEDIA_BUILDS[platform] = originalBuild
  else delete MEDIA_BUILDS[platform]
  vi.unstubAllGlobals()
  vi.unstubAllEnvs()
  await rm(directory, { recursive: true, force: true })
})

describe('pinned media installation', () => {
  it('retries transient Windows locks while publishing a verified directory', async () => {
    const renameDirectory = vi
      .fn()
      .mockRejectedValueOnce(Object.assign(new Error('scanner lock'), { code: 'EPERM' }))
      .mockRejectedValueOnce(Object.assign(new Error('scanner lock'), { code: 'EBUSY' }))
      .mockResolvedValue(undefined)
    const wait = vi.fn(() => Promise.resolve())

    await publishMediaDirectory('staged', 'installed', {
      platform: 'win32',
      renameDirectory,
      wait,
    })

    expect(renameDirectory).toHaveBeenCalledTimes(3)
    expect(wait.mock.calls).toEqual([[250], [500]])
  })

  it('does not retry publish errors on other platforms', async () => {
    const error = Object.assign(new Error('permission denied'), { code: 'EPERM' })
    const renameDirectory = vi.fn(() => Promise.reject(error))
    const wait = vi.fn(() => Promise.resolve())

    await expect(
      publishMediaDirectory('staged', 'installed', {
        platform: 'linux',
        renameDirectory,
        wait,
      }),
    ).rejects.toBe(error)

    expect(renameDirectory).toHaveBeenCalledTimes(1)
    expect(wait).not.toHaveBeenCalled()
  })

  it.skipIf(process.platform === 'win32')(
    'extracts selected tar.xz binaries and notices without unrelated files',
    async () => {
      const input = join(directory, 'input')
      await mkdir(join(input, 'bin'), { recursive: true })
      await writeFile(join(input, 'bin', 'ffmpeg'), Buffer.from([0, 255, 12, 128]))
      await writeFile(join(input, 'bin', 'ffprobe'), 'probe')
      await writeFile(join(input, 'LICENSE.txt'), 'license')
      await writeFile(join(input, 'unrelated'), 'ignored')
      const archive = join(directory, 'archive.tar.xz')
      execFileSync('tar', ['-cJf', archive, '-C', directory, 'input'])
      const output = join(directory, 'output')
      await extractMediaArchive(archive, output)
      expect(await readdir(output)).toEqual(['LICENSE.txt', 'ffmpeg', 'ffprobe'])
      expect(await readFile(join(output, 'ffmpeg'))).toEqual(Buffer.from([0, 255, 12, 128]))
    },
  )

  it.skipIf(process.platform === 'win32')(
    'rejects tar.xz symlink binaries without following them',
    async () => {
      const input = join(directory, 'input')
      await mkdir(input)
      await writeFile(join(directory, 'outside'), 'outside')
      await symlink('../outside', join(input, 'ffmpeg'))
      const archive = join(directory, 'archive.tar.xz')
      execFileSync('tar', ['-cJf', archive, '-C', directory, 'input'])
      await expect(extractMediaArchive(archive, join(directory, 'output'))).rejects.toThrow(
        'Cannot extract',
      )
      expect(await readFile(join(directory, 'outside'), 'utf8')).toBe('outside')
    },
  )

  it('downloads the exact pinned bytes', async () => {
    const bytes = Buffer.from('verified archive')
    vi.stubGlobal(
      'fetch',
      vi.fn(() => Promise.resolve(new Response(bytes))),
    )
    const destination = join(directory, 'archive.zip')
    await downloadMediaArchive(
      'https://example.test/archive',
      createHash('sha256').update(bytes).digest('hex'),
      destination,
    )
    expect(await readFile(destination)).toEqual(bytes)
  })

  it('removes a corrupted download without publishing a cache entry', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(() => Promise.resolve(new Response('corrupted'))),
    )
    MEDIA_BUILDS[platform] = [{ url: 'https://example.test/archive', sha256: '0'.repeat(64) }]
    await expect(installMediaTools()).rejects.toThrow('SHA-256 mismatch')
    expect(await readdir(dirname(mediaToolDirectory()))).toEqual([])
  })

  it('leaves no partial install after an HTTP failure', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(() => Promise.resolve(new Response('unavailable', { status: 503 }))),
    )
    MEDIA_BUILDS[platform] = [{ url: 'https://example.test/archive', sha256: '0'.repeat(64) }]
    await expect(installMediaTools()).rejects.toThrow('HTTP 503')
    expect(await readdir(dirname(mediaToolDirectory()))).toEqual([])
  })

  it('does not overwrite an existing download destination', async () => {
    vi.stubGlobal(
      'fetch',
      vi.fn(() => Promise.resolve(new Response('new content'))),
    )
    const destination = join(directory, 'archive.zip')
    await writeFile(destination, 'existing content')
    await expect(
      downloadMediaArchive('https://example.test/archive', '0'.repeat(64), destination),
    ).rejects.toThrow()
    expect(await readFile(destination, 'utf8')).toBe('existing content')
  })

  it('rejects an unsupported platform before downloading', async () => {
    delete MEDIA_BUILDS[platform]
    const fetch = vi.fn()
    vi.stubGlobal('fetch', fetch)
    await expect(installMediaTools()).rejects.toThrow('No managed FFmpeg build')
    expect(fetch).not.toHaveBeenCalled()
  })
})

// ZIP fixtures contain a nested executable/license, a symbolic link, and a parent traversal.
const zipFixtures = {
  normal:
    'UEsDBBQAAAAAAE8aKV0F6eDGBgAAAAYAAAAQAAAAYnVpbGQvYmluL2ZmbXBlZ2JpbmFyeVBLAwQUAAAAAABPGildGfRoVwcAAAAHAAAADQAAAGJ1aWxkL0xJQ0VOU0VsaWNlbnNlUEsBAhQDFAAAAAAATxopXQXp4MYGAAAABgAAABAAAAAAAAAAAAAAAIABAAAAAGJ1aWxkL2Jpbi9mZm1wZWdQSwECFAMUAAAAAABPGildGfRoVwcAAAAHAAAADQAAAAAAAAAAAAAAgAE0AAAAYnVpbGQvTElDRU5TRVBLBQYAAAAAAgACAHkAAABmAAAAAAA=',
  symlink:
    'UEsDBBQAAAAAAAAAIQCjZRntCQAAAAkAAAAGAAAAZmZtcGVnLi4vZXNjYXBlUEsBAhQDFAAAAAAAAAAhAKNlGe0JAAAACQAAAAYAAAAAAAAAAAAAAP+hAAAAAGZmbXBlZ1BLBQYAAAAAAQABADQAAAAtAAAAAAA=',
  traversal:
    'UEsDBBQAAAAAAE8aKV37OSuCAwAAAAMAAAAJAAAALi4vZXNjYXBlYmFkUEsBAhQDFAAAAAAATxopXfs5K4IDAAAAAwAAAAkAAAAAAAAAAAAAAIABAAAAAC4uL2VzY2FwZVBLBQYAAAAAAQABADcAAAAqAAAAAAA=',
}

it('extracts only selected files into controlled paths', async () => {
  const zip = join(directory, 'input.zip')
  const output = join(directory, 'output')
  await writeFile(zip, Buffer.from(zipFixtures.normal, 'base64'))
  await extractMediaArchive(zip, output)
  expect(await readFile(join(output, 'ffmpeg'), 'utf8')).toBe('binary')
  expect(await readFile(join(output, 'LICENSE'), 'utf8')).toBe('license')
})
it.each(['symlink', 'traversal'] as const)('rejects %s archive entries', async (kind) => {
  const zip = join(directory, 'input.zip')
  await writeFile(zip, Buffer.from(zipFixtures[kind], 'base64'))
  await expect(extractMediaArchive(zip, join(directory, 'output'))).rejects.toThrow()
  await expect(readFile(join(directory, 'escape'))).rejects.toThrow()
})
