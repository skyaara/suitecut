// SPDX-License-Identifier: MIT
import { type ChildProcess, spawn } from 'node:child_process'
import { EventEmitter } from 'node:events'
import { createInterface } from 'node:readline'
import { PassThrough } from 'node:stream'

import { afterEach, describe, expect, it, vi } from 'vitest'
import * as z from 'zod'

import { servePrototypeBrowserMcp } from '../src/prototype-browser-mcp.js'
import { launchPrototypeBrowser } from '../src/prototype-browser.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

const command = z.looseObject({ id: z.number(), cmd: z.string(), tab: z.number().optional() })
type Command = z.infer<typeof command>

// A stand-in for suitecut-browser in control mode. `answer` decides each reply;
// returning undefined leaves the command unanswered.
function fakeBrowser(
  answer: (message: Command, emit: (line: object) => void) => object | undefined,
) {
  const child = new EventEmitter() as ChildProcess & EventEmitter
  const stdin = new PassThrough()
  const stdout = new PassThrough()
  Object.assign(child, {
    stdin,
    stdout,
    stderr: new PassThrough(),
    kill: () => child.emit('exit', null),
  })
  const emit = (line: object) => stdout.write(`${JSON.stringify(line)}\n`)
  const seen: Command[] = []
  createInterface({ input: stdin }).on('line', (line) => {
    const message = command.parse(JSON.parse(line))
    seen.push(message)
    if (message.cmd === 'quit') {
      emit({ id: message.id, ok: true })
      setImmediate(() => child.emit('exit', 0))
      return
    }
    const result = answer(message, emit)
    if (result) emit({ id: message.id, ...result })
  })
  vi.mocked(spawn).mockReturnValueOnce(child)
  setImmediate(() => {
    emit({ event: 'ready', tab: 1 })
    emit({ event: 'loading', tab: 1, loading: false, url: 'about:blank' })
  })
  return { seen, emit }
}

const options = { executablePath: '/fake/suitecut-browser', profileDirectory: '/tmp/unused' }

describe('prototype browser SDK', () => {
  afterEach(() => vi.mocked(spawn).mockReset())

  it('waits for a navigation to finish and reports load errors', async () => {
    fakeBrowser((message, emit) => {
      if (message.cmd === 'navigate')
        setImmediate(() => {
          emit({ event: 'loading', tab: 1, loading: true, url: 'about:blank' })
          emit({ event: 'load-error', tab: 1, code: -105, error: 'ERR_NAME_NOT_RESOLVED' })
          emit({ event: 'loading', tab: 1, loading: false, url: 'https://missing.test/' })
        })
      return { ok: true }
    })
    const browser = await launchPrototypeBrowser(options)
    const result = await browser.tab(1).goto('https://missing.test/')
    expect(result).toEqual({ url: 'https://missing.test/', error: 'ERR_NAME_NOT_RESOLVED' })
    await browser.close()
  })

  it('returns from a click that opens a dialog and holds page reads until it is answered', async () => {
    const { seen } = fakeBrowser((message, emit) => {
      const method = typeof message.method === 'string' ? message.method : ''
      if (message.cmd === 'cdp' && method === 'Input.dispatchMouseEvent') {
        const params = z.object({ type: z.string() }).parse(message.params)
        if (params.type !== 'mouseReleased') return { ok: true, result: {} }
        // Chromium answers only once the dialog closes; here, never.
        emit({ event: 'dialog', tab: 1, type: 1, message: 'Submit?' })
        return undefined
      }
      return { ok: true, result: {} }
    })
    const browser = await launchPrototypeBrowser(options)
    const tab = browser.tab(1)
    await tab.click(10, 10)
    await expect(tab.evaluate('1')).rejects.toThrow('A confirm dialog is open: "Submit?"')
    await expect(tab.snapshot()).resolves.toContain('Open dialog: "Submit?"')
    await tab.answerDialog(true)
    expect(seen.find((m) => m.cmd === 'dialog')).toMatchObject({ tab: 1, accept: true })
    expect(seen.find((m) => m.cmd === 'cdp')).toMatchObject({
      method: 'Emulation.setFocusEmulationEnabled',
    })
    await browser.close()
  })
})

describe('prototype browser MCP server', () => {
  afterEach(() => vi.mocked(spawn).mockReset())

  it('lists tools and runs them against the browser', async () => {
    fakeBrowser((message, emit) => {
      if (message.cmd === 'tabs')
        return { ok: true, result: [{ tab: 1, title: 'T', url: 'about:blank', active: true }] }
      if (message.cmd === 'navigate')
        setImmediate(() => {
          emit({ event: 'loading', tab: 1, loading: true })
          emit({ event: 'loading', tab: 1, loading: false, url: 'https://example.test/' })
        })
      return { ok: true }
    })
    const input = new PassThrough()
    const output = new PassThrough()
    const replies = createInterface({ input: output })[Symbol.asyncIterator]()
    const served = servePrototypeBrowserMcp({ browser: options, input, output })
    const rpc = async (id: number, method: string, params: object) => {
      input.write(`${JSON.stringify({ jsonrpc: '2.0', id, method, params })}\n`)
      const next = await replies.next()
      return z
        .looseObject({ id: z.number(), result: z.json().optional() })
        .parse(JSON.parse(z.string().parse(next.value)))
    }
    const init = await rpc(1, 'initialize', { protocolVersion: '2025-06-18' })
    expect(init.result).toMatchObject({
      protocolVersion: '2025-06-18',
      capabilities: { tools: {} },
    })
    const listed = await rpc(2, 'tools/list', {})
    expect(listed.result).toMatchObject({
      tools: expect.arrayContaining([
        expect.objectContaining({
          name: 'browser_snapshot',
          inputSchema: expect.objectContaining({ type: 'object' }),
        }),
      ]),
    })
    const navigated = await rpc(3, 'tools/call', {
      name: 'browser_navigate',
      arguments: { url: 'https://example.test/' },
    })
    expect(navigated.result).toEqual({
      content: [{ type: 'text', text: 'Loaded https://example.test/' }],
    })
    const missing = await rpc(4, 'tools/call', { name: 'browser_click', arguments: {} })
    expect(missing.result).toMatchObject({
      isError: true,
      content: [{ text: 'Give a ref, or both x and y' }],
    })
    input.end()
    await served
  })
})
