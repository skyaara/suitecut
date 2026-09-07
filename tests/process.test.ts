import { chmod, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import process from 'node:process'

import { afterEach, describe, expect, it, vi } from 'vitest'

import { resolveFfmpeg, runProcess } from '../src/process.js'

afterEach(() => {
  vi.unstubAllEnvs()
})

describe('SuiteCut process helpers', () => {
  it('captures output and exit status without invoking a shell', async () => {
    const args = [
      '-e',
      "process.stdout.write('stdout'); process.stderr.write('stderr'); process.exitCode = 7",
    ]
    const result = await runProcess(process.execPath, args)

    expect(result).toEqual({
      executable: process.execPath,
      args,
      exitCode: 7,
      signal: null,
      stdout: 'stdout',
      stderr: 'stderr',
    })
  })

  it('terminates a running process when its signal is aborted', async () => {
    const controller = new AbortController()
    const pending = runProcess(process.execPath, ['-e', 'setInterval(() => undefined, 1_000)'], {
      signal: controller.signal,
      forceKillAfterMs: 100,
    })

    controller.abort()

    await expect(pending).rejects.toMatchObject({ name: 'AbortError' })
  })

  it('terminates a process that exceeds its deadline', async () => {
    await expect(
      runProcess(process.execPath, ['-e', 'setInterval(() => undefined, 1_000)'], {
        timeoutMs: 25,
        forceKillAfterMs: 100,
      }),
    ).rejects.toMatchObject({ name: 'TimeoutError' })
  })

  it('terminates a process whose captured output exceeds the limit', async () => {
    await expect(
      runProcess(process.execPath, ['-e', "process.stdout.write('x'.repeat(1024))"], {
        maxBufferBytes: 64,
        forceKillAfterMs: 100,
      }),
    ).rejects.toThrow('Process output exceeded 64 bytes')
  })

  it('rejects an explicit FFmpeg path that is not executable', async () => {
    await expect(resolveFfmpeg('/suitecut-tests/missing-ffmpeg')).rejects.toThrow(
      'FFmpeg is not executable',
    )
  })

  it('reuses a successful executable probe while PATH is unchanged', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-process-'))
    const executable = join(directory, 'ffmpeg')
    const counter = join(directory, 'starts.txt')
    await writeFile(
      executable,
      `#!/bin/sh\nprintf x >> ${JSON.stringify(counter)}\nexit 0\n`,
      'utf8',
    )
    await chmod(executable, 0o755)
    vi.stubEnv('PATH', directory)

    try {
      await expect(Promise.all([resolveFfmpeg(), resolveFfmpeg()])).resolves.toEqual([
        'ffmpeg',
        'ffmpeg',
      ])
      await expect(resolveFfmpeg()).resolves.toBe('ffmpeg')
      expect(await readFile(counter, 'utf8')).toBe('x')
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })
})
