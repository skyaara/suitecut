import process from 'node:process'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { runSuiteCutCli } from '../src/cli.js'

interface CliResult {
  status: number | null
  stdout: string
  stderr: string
}

let stdout: string[]
let stderr: string[]

beforeEach(() => {
  stdout = []
  stderr = []
  vi.spyOn(process.stdout, 'write').mockImplementation((chunk) => {
    stdout.push(String(chunk))
    return true
  })
  vi.spyOn(process.stderr, 'write').mockImplementation((chunk) => {
    stderr.push(String(chunk))
    return true
  })
})

afterEach(() => {
  vi.restoreAllMocks()
})

async function runCli(args: readonly string[]): Promise<CliResult> {
  return {
    status: await runSuiteCutCli(args),
    stdout: stdout.join(''),
    stderr: stderr.join(''),
  }
}

describe('SuiteCut CLI', () => {
  it('prints usage for the help command', async () => {
    const result = await runCli(['--help'])

    expect(result.status).toBe(0)
    expect(result.stdout).toContain('Usage:')
    expect(result.stdout).toContain('suitecut render')
    expect(result.stdout).toContain('Output width. Default 1920.')
    expect(result.stdout).toContain('Output frame rate. Default 30.')
    expect(result.stdout).toContain('Default standard.')
    expect(result.stderr).toBe('')
  })

  it.each([
    { args: ['unknown'], message: 'Unknown command: unknown' },
    { args: ['render'], message: 'render requires --manifest' },
    { args: ['render', '--manifest', 'manifest.json'], message: 'render requires --output' },
  ])('reports invalid command-line input: $message', async ({ args, message }) => {
    const result = await runCli(args)

    expect(result.status).toBe(1)
    expect(result.stderr.trim()).toBe(message)
    expect(result.stdout).toBe('')
  })
})
