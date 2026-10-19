// SPDX-License-Identifier: MIT
import { ChildProcess, spawn } from 'node:child_process'
import { access } from 'node:fs/promises'
import { PassThrough } from 'node:stream'

import { afterEach, describe, expect, it, vi } from 'vitest'

import { launchProtocolProcess } from '../src/browser-process.js'

import type * as ChildProcesses from 'node:child_process'
import type * as FileSystem from 'node:fs/promises'

const proc = vi.hoisted(() => ({
  exited: false,
  scans: 0,
  inaccessible: false,
  reusedLeader: false,
  snapshots: [] as { pid: number; state: string; session?: number; started?: number }[][],
}))

vi.mock('node:child_process', async (original) => ({
  ...(await original<typeof ChildProcesses>()),
  spawn: vi.fn(),
}))
vi.mock('node:fs/promises', async (original) => {
  const actual = await original<typeof FileSystem>()
  return {
    ...actual,
    readFile: async (path: string, options: 'utf8') => {
      if (!path.startsWith('/proc/')) return actual.readFile(path, options)
      const pid = Number(path.split('/')[2])
      const member =
        pid === 12_345_000 && (!proc.exited || proc.reusedLeader)
          ? { pid, state: 'S', started: proc.reusedLeader && proc.exited ? 999 : 100 }
          : proc.snapshots[Math.min(proc.scans - 1, proc.snapshots.length - 1)]?.find(
              (entry) => entry.pid === pid,
            )
      if (!member) throw Object.assign(new Error('Process exited'), { code: 'ENOENT' })
      const fields = [
        member.state,
        '1',
        '12345000',
        String(member.session ?? 12_345_000),
        ...Array<string>(15).fill('0'),
        String(member.started ?? 101),
      ]
      return `${pid} (renderer with ) spaces) ${fields.join(' ')}\n`
    },
    readdir: async (path: string) => {
      if (path !== '/proc') return actual.readdir(path)
      if (proc.inaccessible) throw Object.assign(new Error('Restricted proc'), { code: 'EACCES' })
      const members = proc.snapshots[Math.min(proc.scans++, proc.snapshots.length - 1)] ?? []
      return ['self', ...members.map((member) => String(member.pid))]
    },
  }
})

afterEach(() => {
  vi.restoreAllMocks()
  vi.mocked(spawn).mockReset()
})

describe.skipIf(process.platform !== 'linux')('owned Linux process-group drain', () => {
  async function setup(
    snapshots: (typeof proc)['snapshots'],
    options: { inaccessible?: boolean; reusedLeader?: boolean } = {},
  ) {
    Object.assign(proc, {
      exited: false,
      scans: 0,
      inaccessible: false,
      reusedLeader: false,
      snapshots,
      ...options,
    })
    const child = Object.assign(new ChildProcess(), {
      pid: 12_345_000,
      stdout: new PassThrough(),
      stderr: new PassThrough(),
    })
    let profile = ''
    vi.mocked(spawn).mockImplementation((_executable, args) => {
      profile = (args as string[]).find((arg) => arg.startsWith('--user-data-dir='))!.slice(16)
      queueMicrotask(() => {
        child.stderr.write('DevTools listening on ws://127.0.0.1:12345/devtools/browser/drain\n')
      })
      return child
    })
    const signals: Parameters<typeof process.kill>[1][] = []
    vi.spyOn(process, 'kill').mockImplementation((pid, signal) => {
      expect(pid).toBe(-child.pid)
      signals.push(signal)
      if (signal === 'SIGTERM') {
        queueMicrotask(() => {
          proc.exited = true
          child.emit('exit', 0, null)
        })
      }
      return true
    })
    const browser = await launchProtocolProcess({ backend: 'cdp', executablePath: '/fake/browser' })
    return { browser, signals, profile: () => profile }
  }

  it('finishes a zombie-only group without the grace period or a futile SIGKILL', async () => {
    const { browser, signals, profile } = await setup([
      [
        { pid: 12_345_001, state: 'Z' },
        { pid: 12_345_002, state: 'X' },
      ],
    ])
    const started = performance.now()
    await browser.close()
    expect(performance.now() - started).toBeLessThan(150)
    expect(proc.scans).toBe(1)
    expect(signals).toEqual(['SIGTERM', 0])
    await expect(access(profile())).rejects.toMatchObject({ code: 'ENOENT' })
  })

  it('keeps waiting while any owned renderer is live, then stops when it becomes a zombie', async () => {
    const { browser, signals } = await setup([
      [
        { pid: 12_345_001, state: 'Z' },
        { pid: 12_345_002, state: 'S' },
      ],
      [
        { pid: 12_345_001, state: 'Z' },
        { pid: 12_345_002, state: 'Z' },
      ],
    ])
    await browser.close()
    expect(proc.scans).toBe(2)
    expect(signals).not.toContain('SIGKILL')
  })

  it('still kills a live renderer after the bounded grace period', async () => {
    const { browser, signals } = await setup([[{ pid: 12_345_001, state: 'S' }]])
    await browser.close()
    expect(proc.scans).toBeGreaterThan(1)
    expect(signals.at(-1)).toBe('SIGKILL')
  })

  it('preserves the portable bounded drain when proc inspection is restricted', async () => {
    const { browser, signals } = await setup([], { inaccessible: true })
    await browser.close()
    expect(signals.at(-1)).toBe('SIGKILL')
  })

  it('does not signal an unrelated process after the leader PID has been reused', async () => {
    const { browser, signals } = await setup([[{ pid: 12_345_001, state: 'S' }]], {
      reusedLeader: true,
    })
    await browser.close()
    expect(proc.scans).toBe(0)
    expect(signals).toEqual(['SIGTERM', 0])
  })

  it.each([
    { pid: 12_345_001, state: 'S', session: 456 },
    { pid: 12_345_001, state: 'S', started: 50 },
  ])('does not signal a group with incompatible ownership %o', async (member) => {
    const { browser, signals } = await setup([[member]])
    await browser.close()
    expect(signals).toEqual(['SIGTERM', 0])
  })
})
