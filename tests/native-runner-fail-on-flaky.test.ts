// SPDX-License-Identifier: MIT
import { ChildProcess, spawn } from 'node:child_process'
import console from 'node:console'
import process from 'node:process'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { runNativeSuite } from '../src/native-runner.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))
const base = {
  executablePath: '/native/runner',
  suitePath: '/suite/tests.mjs',
  reportPath: '/reports/run.json',
}
const cliBase = [
  'node',
  '/sdk/native-runner-cli.js',
  '--executable',
  base.executablePath,
  '--suite',
  base.suitePath,
  '--report',
  base.reportPath,
]
const originalArgv = process.argv
const originalExit = process.exitCode
let outcome: { code: number | null; signal: NodeJS.Signals | null }
const args = () => {
  const call = vi.mocked(spawn).mock.calls[0]
  if (!call || !Array.isArray(call[1]))
    throw new Error('Expected genuine runner argument construction')
  return call[1] as string[]
}
beforeEach(() => {
  vi.clearAllMocks()
  vi.resetModules()
  process.exitCode = undefined
  outcome = { code: 0, signal: null }
  vi.mocked(spawn).mockImplementation(() => {
    const child = new ChildProcess()
    queueMicrotask(() => child.emit('close', outcome.code, outcome.signal))
    return child
  })
  vi.spyOn(console, 'error').mockImplementation(() => undefined)
  vi.spyOn(console, 'log').mockImplementation(() => undefined)
})
afterEach(() => {
  process.argv = originalArgv
  process.exitCode = originalExit
  vi.restoreAllMocks()
})

describe('native fail-on-flaky launcher bridge', () => {
  it.each([undefined, false, true])(
    'forwards strict SDK setting %s without changing native exit',
    async (failOnFlakyTests) => {
      outcome.code = 1
      const result = await runNativeSuite({
        ...base,
        ...(failOnFlakyTests === undefined ? {} : { failOnFlakyTests }),
      })
      expect(args().filter((value) => value === '--fail-on-flaky-tests')).toHaveLength(
        failOnFlakyTests ? 1 : 0,
      )
      expect(args()).not.toContain('--retries')
      expect(spawn).toHaveBeenCalledOnce()
      expect(result).toEqual({ exitCode: 1, signal: null })
    },
  )
  it.each(['true', 'false', 0, 1, null, {}, []])(
    'rejects nonboolean SDK input %j before launch',
    (value) => {
      expect(() => runNativeSuite({ ...base, failOnFlakyTests: value as never })).toThrow()
      expect(spawn).not.toHaveBeenCalled()
    },
  )
  it('composes the policy flag with existing native directory/project arguments', async () => {
    await runNativeSuite({
      executablePath: base.executablePath,
      reportPath: base.reportPath,
      testDir: '/suite/directory',
      projects: ['real-project'],
      workers: 2,
      failOnFlakyTests: true,
    })
    expect(args()).toEqual(
      expect.arrayContaining([
        '--test-dir',
        '/suite/directory',
        '--project',
        'real-project',
        '--workers',
        '2',
        '--fail-on-flaky-tests',
      ]),
    )
    expect(args()).not.toContain('--suite')
  })
})
describe('native fail-on-flaky public CLI bridge', () => {
  it.each([false, true])('passes a bare CLI flag only when present (%s)', async (enabled) => {
    process.argv = [...cliBase, ...(enabled ? ['--fail-on-flaky-tests'] : [])]
    outcome.code = 1
    await import('../src/native-runner-cli.js')
    expect(args().filter((value) => value === '--fail-on-flaky-tests')).toHaveLength(
      enabled ? 1 : 0,
    )
    expect(process.exitCode).toBe(1)
    expect(console.error).not.toHaveBeenCalled()
  })
  it('rejects a string-valued CLI flag rather than coercing false', async () => {
    process.argv = [...cliBase, '--fail-on-flaky-tests=false']
    await import('../src/native-runner-cli.js')
    expect(spawn).not.toHaveBeenCalled()
    expect(process.exitCode).toBe(2)
    expect(console.error).toHaveBeenCalledOnce()
  })
  it('documents the flag without launching a runner', async () => {
    process.argv = ['node', '/sdk/native-runner-cli.js', '--help']
    await import('../src/native-runner-cli.js')
    expect(console.log).toHaveBeenCalledWith(expect.stringContaining('[--fail-on-flaky-tests]'))
    expect(spawn).not.toHaveBeenCalled()
  })
  it('preserves actual native signal termination instead of declaring policy success', async () => {
    process.argv = [...cliBase, '--fail-on-flaky-tests']
    outcome = { code: null, signal: 'SIGTERM' }
    await import('../src/native-runner-cli.js')
    expect(process.exitCode).toBe(143)
    expect(console.error).toHaveBeenCalledWith(
      'Native runner terminated from actual signal SIGTERM',
    )
  })
})
