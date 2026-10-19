// SPDX-License-Identifier: MIT
import { ChildProcess, spawn } from 'node:child_process'
import { getEventListeners } from 'node:events'
import { PassThrough } from 'node:stream'

import { afterEach, describe, expect, it, vi } from 'vitest'

import { launchNativeBrowser, type NativeBrowser } from '../src/native-browser.js'
import {
  NativeBrowserContexts,
  type NativeContextEvent,
  type NativeContextTransport,
} from '../src/native-context.js'
import { type UntrustedInput } from '../src/untrusted.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))
afterEach(() => {
  vi.useRealTimers()
  vi.restoreAllMocks()
})
async function fixture() {
  const child = new ChildProcess(),
    input = new PassThrough(),
    output = new PassThrough()
  child.stdin = input
  child.stdout = output
  child.stderr = new PassThrough()
  input.resume()
  input.once('finish', () => queueMicrotask(() => child.emit('close', 0)))
  vi.mocked(spawn).mockReturnValue(child)
  const emit = (header: object, data = Buffer.alloc(0)) => {
    const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
    const prefix = Buffer.alloc(4)
    prefix.writeUInt32LE(json.length)
    output.write(Buffer.concat([prefix, json, data]))
  }
  const pending = launchNativeBrowser({
    executablePath: '/process-model/browser',
    width: 2,
    height: 2,
  })
  await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
  emit({ type: 'ready', timestampMs: 1, cefVersion: 'model' })
  return { browser: await pending, input, emit }
}
describe('SDK lifecycle hardening', () => {
  it.each([
    { label: 'null', replacement: null },
    { label: 'array', replacement: [] },
    { label: 'number', replacement: 17 },
    { label: 'string', replacement: 'scalar' },
    { label: 'boolean', replacement: true },
  ])(
    'rejects a control envelope replaced by an authored toJSON $label before transport admission',
    async ({ replacement }) => {
      const f = await fixture()
      const writes: string[] = []
      f.input.on('data', (chunk: Buffer) => {
        writes.push(chunk.toString())
      })
      const outcome = f.browser
        .runAutomation('networkBody', {
          bodyId: 'actual-body',
          toJSON: () => replacement,
        })
        .then(
          () => undefined,
          (error: UntrustedInput) => error,
        )
      try {
        expect(writes).toEqual([])
        expect(await outcome).toBeInstanceOf(Error)
      } finally {
        await f.browser.close()
        await outcome
      }
    },
  )

  it('keeps deadline-expired native screenshot work until its actual response and preserves the timeout', async () => {
    const f = await fixture()
    vi.useFakeTimers()
    const controller = new AbortController()
    let settled = false
    const commands: { id: number; command: string }[] = []
    f.input.on('data', (chunk: Buffer) => {
      commands.push(JSON.parse(chunk.toString()) as (typeof commands)[number])
    })
    const operation = f.browser
      .runAutomation(
        'artifact',
        { action: 'compareCommit', inputToken: 'actual-capture', timeoutMs: 1 },
        controller.signal,
      )
      .then(
        () => {
          settled = true
          return undefined
        },
        (error: UntrustedInput) => {
          settled = true
          return error
        },
      )
    const id = commands.find((command) => command.command === 'artifact')!.id
    await vi.advanceTimersByTimeAsync(1001)
    expect(settled).toBe(false)
    expect(getEventListeners(controller.signal, 'abort')).toHaveLength(0)
    expect(commands.filter((command) => command.command === 'cancel')).toHaveLength(1)
    f.emit({ type: 'response', id, ok: true }, Buffer.from('{}'))
    expect(await operation).toMatchObject({
      name: 'TimeoutError',
      message: 'Native browser command timed out',
    })
    expect(settled).toBe(true)
    vi.useRealTimers()
    await f.browser.close()
  })

  it('snapshots queued controls without retaining caller mutations and preserves original queued deadlines', async () => {
    const f = await fixture()
    const commands: { id: number; command: string; bodyId?: string }[] = []
    f.input.on('data', (chunk: Buffer) => {
      commands.push(JSON.parse(chunk.toString()) as (typeof commands)[number])
    })
    const busy = Array.from({ length: 128 }, () =>
      f.browser.runAutomation('networkBody', { bodyId: 'busy', timeoutMs: 0 }),
    )
    vi.useFakeTimers()
    const expires = f.browser.runAutomation('networkBody', { bodyId: 'expired', timeoutMs: 10 })
    const expired = expect(expires).rejects.toThrow('deadline expired while queued')
    await vi.advanceTimersByTimeAsync(10)
    await expired
    expect(commands).toHaveLength(128)
    const fields = { bodyId: 'original', timeoutMs: 30000 }
    const queued = f.browser.runAutomation('networkBody', fields)
    fields.bodyId = 'mutated'
    expect(commands).toHaveLength(128)
    for (const command of [...commands]) f.emit({ type: 'response', id: command.id, ok: true })
    await Promise.all(busy)
    await Promise.resolve()
    const actual = commands.find((command) => command.bodyId === 'original')
    expect(actual).toBeDefined()
    f.emit({ type: 'response', id: actual!.id, ok: true })
    await queued
    vi.useRealTimers()
    await f.browser.close()
  })

  it('finishes all context close callbacks and detaches observers before exposing authored errors', () => {
    const lifecycle = new AbortController(),
      events = new Set<(event: NativeContextEvent) => void>()
    const consoleEvents = new Set<(event: never) => void>()
    const transport: NativeContextTransport = {
      signal: lifecycle.signal,
      runContext: () => Promise.resolve(undefined),
      pageSource: () =>
        ({
          signal: lifecycle.signal,
          runAutomation: () => Promise.resolve(),
        }) as UntrustedInput as NativeBrowser,
      onContextEvent: (listener) => {
        events.add(listener)
        return () => events.delete(listener)
      },
      onContextConsole: (listener) => {
        consoleEvents.add(listener)
        return () => consoleEvents.delete(listener)
      },
    }
    const contexts = new NativeBrowserContexts(transport),
      context = contexts.defaultContext()
    const original = new Error('authored close callback'),
      observed = vi.fn(),
      queued: (() => void)[] = []
    vi.spyOn(globalThis, 'queueMicrotask').mockImplementation((callback) => {
      queued.push(callback)
    })
    context.on('close', () => {
      throw original
    })
    context.on('close', observed)
    context.on('console', () => undefined)
    expect(consoleEvents.size).toBe(1)
    for (const listener of [...events]) listener({ event: 'context-closed', contextId: 1 })
    expect(observed).toHaveBeenCalledOnce()
    expect(consoleEvents.size).toBe(0)
    expect(contexts.contexts()).toEqual([])
    expect(queued).toHaveLength(1)
    expect(queued[0]).toThrow(original)
    lifecycle.abort()
  })
})
