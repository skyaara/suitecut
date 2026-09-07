import * as childProcess from 'node:child_process'
import process from 'node:process'

import { afterEach, describe, expect, it, vi } from 'vitest'

import { resolveFfmpeg, runProcess } from '../src/process.js'

vi.mock('node:child_process', async (importOriginal) => {
  const original = await importOriginal<typeof childProcess>()
  return { ...original, spawn: vi.fn(original.spawn) }
})

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
    const original = await vi.importActual<typeof childProcess>('node:child_process')
    const mockedSpawn = vi.mocked(childProcess.spawn)
    mockedSpawn.mockClear()
    // Use a real, portable child process for the probe instead of a POSIX shell script.
    mockedSpawn.mockImplementationOnce((_executable, _args, options) =>
      original.spawn(process.execPath, ['-e', 'process.exit(0)'], options),
    )
    vi.stubEnv('PATH', 'suitecut-isolated-probe-path')

    await expect(Promise.all([resolveFfmpeg(), resolveFfmpeg()])).resolves.toEqual([
      'ffmpeg',
      'ffmpeg',
    ])
    await expect(resolveFfmpeg()).resolves.toBe('ffmpeg')
    expect(mockedSpawn).toHaveBeenCalledTimes(1)
    expect(mockedSpawn.mock.calls[0]?.slice(0, 2)).toEqual(['ffmpeg', ['-version']])
  })
})
