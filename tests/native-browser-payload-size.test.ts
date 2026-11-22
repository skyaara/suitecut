// SPDX-License-Identifier: MIT
import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { launchNativeBrowser, NativeEventPayloadError } from '../src/native-browser.js'

import type * as NodeBuffer from 'node:buffer'

// V8's real maximum string length is ~512 MiB; a small stand-in keeps the test cheap.
const limit = vi.hoisted(() => ({ value: 0 }))
vi.mock('node:buffer', async (original) => {
  const actual = await original<typeof NodeBuffer>()
  limit.value = actual.constants.MAX_STRING_LENGTH
  return {
    ...actual,
    constants: {
      ...actual.constants,
      get MAX_STRING_LENGTH() {
        return limit.value
      },
    },
  }
})
vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

describe('native event payloads beyond V8 string length', () => {
  let child: ChildProcess
  let output: PassThrough
  let input: PassThrough
  const emit = (header: object, data = Buffer.alloc(0)): void => {
    const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
    const prefix = Buffer.alloc(4)
    prefix.writeUInt32LE(json.length)
    output.write(Buffer.concat([prefix, json, data]))
  }
  beforeEach(() => {
    vi.mocked(spawn).mockReset()
    child = new ChildProcess()
    input = new PassThrough()
    output = new PassThrough()
    child.stdin = input
    child.stdout = output
    child.stderr = new PassThrough()
    vi.spyOn(child, 'kill').mockImplementation(() => {
      queueMicrotask(() => child.emit('close', 1))
      return true
    })
    input.once('finish', () => queueMicrotask(() => child.emit('close', 0)))
    vi.mocked(spawn).mockReturnValue(child)
  })
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('drops only the oversized event or command result and keeps the channel alive', async () => {
    const diagnostics: string[] = []
    const launched = launchNativeBrowser({
      executablePath: '/native/suitecut-browser',
      width: 2,
      height: 2,
      onDiagnostic: (message) => diagnostics.push(message),
    })
    await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
    emit({ type: 'ready', timestampMs: 1, cefVersion: 'test' })
    const browser = await launched
    const failed = vi.fn()
    browser.failure.catch(failed)
    const messages = vi.fn(),
      dropped = vi.fn(),
      routes = vi.fn()
    browser.onConsoleMessage!(messages)
    browser.onEventPayloadError!(dropped)
    browser.onContextRoute!(routes)
    const small = Buffer.from(
      JSON.stringify({
        type: 'log',
        text: 'ok',
        location: { url: 'https://example.test/', lineNumber: 0, columnNumber: 0 },
      }),
    )
    limit.value = small.length
    const oversized = Buffer.from(
      JSON.stringify({
        type: 'log',
        text: 'x'.repeat(small.length),
        location: { url: 'https://example.test/', lineNumber: 0, columnNumber: 0 },
      }),
    )
    emit({ type: 'console', sequence: 1 }, oversized)
    emit({ type: 'contextroute' }, oversized)
    emit({ type: 'console', sequence: 2 }, small)
    expect(messages).toHaveBeenCalledTimes(1)
    expect(messages).toHaveBeenCalledWith(expect.objectContaining({ text: 'ok' }))
    expect(routes).not.toHaveBeenCalled()
    expect(dropped).toHaveBeenCalledTimes(2)
    expect(dropped.mock.calls.map(([error]) => error as NativeEventPayloadError)).toEqual([
      expect.objectContaining({ eventType: 'console', bytes: oversized.length, pageId: 1 }),
      expect.objectContaining({ eventType: 'contextroute', bytes: oversized.length }),
    ])
    expect(dropped.mock.calls[0]?.[0]).toBeInstanceOf(NativeEventPayloadError)
    expect(diagnostics.join('')).toContain("exceeds V8's maximum string length")
    expect(browser.eventSequence?.()).toBe(2)

    // An oversized command result rejects that command alone.
    const pending = browser.runAutomation('evaluate', { expression: '1' })
    await vi.waitFor(() => expect(input.readableLength).toBeGreaterThan(0))
    // The console acknowledgement may share the read, depending on Node's stream timing.
    const commands = (input.read() as Buffer)
      .toString()
      .split('\n')
      .filter(Boolean)
      .map((line) => JSON.parse(line) as { id: number; command: string })
    const { id } = commands.find((command) => command.command === 'evaluate')!
    emit({ type: 'response', id, ok: true }, oversized)
    await expect(pending).rejects.toBeInstanceOf(NativeEventPayloadError)
    expect(browser.state).toBe('running')
    expect(failed).not.toHaveBeenCalled()
    limit.value = Number.MAX_SAFE_INTEGER
    await browser.close()
  })
})
