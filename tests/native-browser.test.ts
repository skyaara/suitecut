import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { launchNativeBrowser } from '../src/native-browser.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

describe('native browser lifecycle', () => {
  let child: ChildProcess
  let output: PassThrough
  let input: PassThrough
  const path = '/native/suitecut-browser'

  const emit = (header: object, data = Buffer.alloc(0)): void => {
    const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
    const prefix = Buffer.alloc(4)
    prefix.writeUInt32LE(json.length)
    output.write(Buffer.concat([prefix, json, data]))
  }
  const launch = async () => {
    const promise = launchNativeBrowser({ executablePath: path, width: 2, height: 2 })
    await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
    emit({ type: 'ready', timestampMs: 1, cefVersion: 'test' })
    return promise
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

  it('uses private pipes and delivers physical frames without browser automation', async () => {
    const browser = await launch()
    const received = vi.fn()
    browser.onFrame(received)
    const data = Buffer.alloc(16, 42)
    emit({ type: 'frame', timestampMs: 2, width: 2, height: 2, dropped: 3 }, data)
    expect(received).toHaveBeenCalledWith(expect.objectContaining({ data, droppedFrames: 3 }))
    expect(spawn).toHaveBeenCalledWith(path, expect.not.arrayContaining(['--no-sandbox']), {
      shell: false,
      stdio: ['pipe', 'pipe', 'pipe'],
    })
    await browser.close()
    await browser.close()
    expect(input.writableEnded).toBe(true)
    expect(browser.state).toBe('closed')
    expect(() => browser.onFrame(() => undefined)).toThrow('not running')
  })

  it('correlates concurrent commands by ID and rejects unsupported navigation', async () => {
    const browser = await launch()
    const first = browser.sendDevToolsCommand('Runtime.evaluate', { expression: '1+1' })
    const second = browser.navigate('https://example.com')
    emit({ type: 'response', id: 2, ok: true })
    emit({ type: 'response', id: 1, ok: true }, Buffer.from('{"value":2}'))
    await expect(first).resolves.toEqual({ value: 2 })
    await second
    await expect(browser.navigate('file:///etc/passwd')).rejects.toThrow('HTTP(S)')
    await browser.close()
    await expect(browser.sendDevToolsCommand('Runtime.evaluate')).rejects.toThrow('closed')
  })

  it('rejects pending commands and reports a renderer failure', async () => {
    const browser = await launch()
    const command = browser.sendDevToolsCommand('Runtime.evaluate')
    const commandRejected = expect(command).rejects.toThrow()
    const failed = expect(browser.failure).rejects.toThrow()
    emit({ type: 'error', error: 'Browser renderer terminated' })
    await Promise.all([commandRejected, failed, browser.closed])
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting the mocked method, not invoking it.
    expect(child.kill).toHaveBeenCalledWith('SIGKILL')
  })

  it('rejects pending commands even when their response payload is malformed', async () => {
    const browser = await launch()
    const command = browser.sendDevToolsCommand('Runtime.evaluate')
    const rejected = expect(command).rejects.toThrow()
    emit({ type: 'response', id: 1, ok: true }, Buffer.from('{'))
    await rejected
    await browser.closed
  })

  it('rejects startup if the native process exits before handshaking', async () => {
    const promise = launchNativeBrowser({ executablePath: path })
    await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
    child.emit('close', 1)
    await expect(promise).rejects.toThrow('exited unexpectedly')
  })

  it('rejects changed frame dimensions before consumers see them', async () => {
    const browser = await launch()
    const listener = vi.fn()
    browser.onFrame(listener)
    emit({ type: 'frame', timestampMs: 2, width: 1, height: 1, dropped: 0 }, Buffer.alloc(4))
    await expect(browser.failure).rejects.toThrow()
    expect(listener).not.toHaveBeenCalled()
    await browser.closed
  })

  it('bounds command count and size', async () => {
    const browser = await launch()
    await expect(
      browser.sendDevToolsCommand('Runtime.evaluate', { expression: 'x'.repeat(65536) }),
    ).rejects.toThrow('64 KiB')
    const pending = Array.from({ length: 64 }, () =>
      browser.sendDevToolsCommand('Runtime.evaluate').catch(() => undefined),
    )
    await expect(browser.sendDevToolsCommand('Runtime.evaluate')).rejects.toThrow('Too many')
    await browser.close()
    await Promise.all(pending)
  })

  it('does not spawn for invalid physical dimensions or an already aborted signal', async () => {
    await expect(
      launchNativeBrowser({ executablePath: path, width: 3840, deviceScaleFactor: 2 }),
    ).rejects.toThrow()
    await expect(
      launchNativeBrowser({ executablePath: path, signal: AbortSignal.abort() }),
    ).rejects.toThrow()
    expect(spawn).not.toHaveBeenCalled()
  })
})
