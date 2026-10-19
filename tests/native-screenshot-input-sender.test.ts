import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { describe, expect, it, vi } from 'vitest'
import * as z from 'zod'

import { launchNativeBrowser } from '../src/native-browser.js'
import { runNativeScreenshotInput } from '../src/native-screenshot-input.js'
import { type UntrustedInput } from '../src/untrusted.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

// Real public SDK sender/parser and its128-entry control lane over private
// streams. This controlled transport component does not claim CEF execution.
describe('screenshot abort while the real SDK control lane is full', () => {
  it('publishes the original abort before queued discard, then retains and sends actual nonce cleanup', async () => {
    const child = new ChildProcess(),
      input = new PassThrough(),
      output = new PassThrough()
    child.stdin = input
    child.stdout = output
    child.stderr = new PassThrough()
    input.once('finish', () => queueMicrotask(() => child.emit('close', 0)))
    vi.mocked(spawn).mockReturnValue(child)
    const commands: {
      id: number
      command: string
      action: string | undefined
      clientToken: string | undefined
    }[] = []
    input.on('data', (chunk: Buffer) => {
      for (const line of chunk.toString().trim().split('\n')) {
        const parsed = z
          .object({
            id: z.number(),
            command: z.string(),
            action: z.string().optional(),
            clientToken: z.string().optional(),
            params: z
              .object({ action: z.string().optional(), clientToken: z.string().optional() })
              .optional(),
          })
          .parse(JSON.parse(line))
        commands.push({
          id: parsed.id,
          command: parsed.command,
          action: parsed.params?.action ?? parsed.action,
          clientToken: parsed.params?.clientToken ?? parsed.clientToken,
        })
      }
    })
    const emit = (header: object, value?: UntrustedInput) => {
      const data = value === undefined ? Buffer.alloc(0) : Buffer.from(JSON.stringify(value))
      const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
      const prefix = Buffer.alloc(4)
      prefix.writeUInt32LE(json.length)
      output.write(Buffer.concat([prefix, json, data]))
    }
    const launching = launchNativeBrowser({
      executablePath: '/authored/control-helper',
      width: 2,
      height: 2,
    })
    await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
    emit({ type: 'ready', timestampMs: 1, cefVersion: 'controlled-component' })
    const source = await launching
    let heldOutcome: Promise<PromiseSettledResult<UntrustedInput>[]> | undefined
    try {
      const controller = new AbortController(),
        reason = new Error('exact caller abort under128 active controls')
      const result = runNativeScreenshotInput(
        (params, signal) => source.runAutomation('artifact', params, signal),
        source,
        Buffer.alloc(33),
        {},
        0,
        controller.signal,
      ).catch((error: UntrustedInput) => error)
      const prepare = commands.find((command) => command.action === 'comparePrepare')!
      expect(prepare).toBeDefined()
      emit(
        { type: 'response', id: prepare.id, ok: true },
        { token: 'nativeScreenshotInput:1', contextId: 1, pageId: 1 },
      )
      const held = Array.from({ length: 128 }, () =>
        source.runContext!('screenshotInput', { action: 'hold', timeoutMs: 0 }),
      )
      heldOutcome = Promise.allSettled(held)
      const controlCommands = commands.filter((command) => command.action === 'hold')
      expect(controlCommands).toHaveLength(128)
      controller.abort(reason)
      expect(await result).toBe(reason)
      await new Promise((resolve) => setImmediate(resolve))
      expect(commands.some((command) => command.action === 'discard')).toBe(false)
      emit({ type: 'response', id: controlCommands[0]!.id, ok: true }, {})
      await new Promise((resolve) => setImmediate(resolve))
      const discard = commands.find((command) => command.action === 'discard')!
      expect(discard).toBeDefined()
      expect(discard.clientToken).toBe(prepare.clientToken)
      emit({ type: 'response', id: discard.id, ok: true }, {})
      for (const command of controlCommands.slice(1))
        emit({ type: 'response', id: command.id, ok: true }, {})
      await Promise.all(held)
      await new Promise((resolve) => setImmediate(resolve))
    } finally {
      await source.close()
      await heldOutcome
      vi.restoreAllMocks()
    }
  })
})
