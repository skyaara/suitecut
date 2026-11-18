// SPDX-License-Identifier: MIT
import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { afterEach, describe, expect, it, vi } from 'vitest'

import { launchNativeBrowser } from '../src/native-browser.js'
import { NativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

async function launch() {
  const child = new ChildProcess(),
    input = new PassThrough(),
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
  const emit = (header: object, data = Buffer.alloc(0)) => {
    const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
    const prefix = Buffer.alloc(4)
    prefix.writeUInt32LE(json.length)
    output.write(Buffer.concat([prefix, json, data]))
  }
  const pending = launchNativeBrowser({
    executablePath: '/controlled/native-browser',
    width: 2,
    height: 2,
    framesPerSecond: 60,
  })
  await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
  emit({ type: 'ready', timestampMs: 1, cefVersion: 'controlled' })
  return { input, emit, browser: await pending }
}

afterEach(() => vi.restoreAllMocks())

// Each stdin write is its own readable chunk; collect everything written so far.
function drain(input: PassThrough): string {
  let text = ''
  for (
    let chunk = input.read() as Buffer | null;
    chunk !== null;
    chunk = input.read() as Buffer | null
  )
    text += chunk.toString('utf8')
  return text
}

describe('native debugger actual SDK control lane', () => {
  it('admits debugger work and open/close past the old 64 ordinary pending requests', async () => {
    const f = await launch()
    const ordinary = Array.from({ length: 64 }, () =>
      f.browser.runAutomation('evaluate', { timeoutMs: 0 }),
    )
    const waiting = f.browser
      .runAutomation('debugger', {
        action: 'waitForPause',
        session: 'controlled-existing',
        timeoutMs: 0,
      })
      .catch((error: UntrustedInput) => error)
    await vi.waitFor(() => expect(drain(f.input)).toContain('"id":65'))
    const page = new NativePage(f.browser)
    const pending = page.createDebuggerSession({ timeout: 0 })
    const text = drain(f.input)
    expect(text).toContain('"command":"debugger"')
    expect(text).toContain('"id":66')
    f.emit(
      { type: 'response', id: 66, ok: true },
      Buffer.from('{"session":"actual-controlled-token"}'),
    )
    const session = await pending
    const closing = session.close({ timeout: 0 })
    expect(drain(f.input)).toContain('"action":"close"')
    f.emit({ type: 'response', id: 67, ok: true }, Buffer.from('{}'))
    await closing
    for (let id = 1; id <= 65; ++id) f.emit({ type: 'response', id, ok: true }, Buffer.from('{}'))
    await Promise.all([...ordinary, waiting])
    await f.browser.close()
  })

  it('sends post-open canonical cleanup at once past the old 128 control slots', async () => {
    const f = await launch(),
      owner = new AbortController()
    const pending = new NativePage(f.browser, owner.signal).createDebuggerSession({ timeout: 0 })
    drain(f.input)
    f.emit(
      { type: 'response', id: 1, ok: true },
      Buffer.from('{"session":"actual-controlled-token"}'),
    )
    const session = await pending
    const held = Array.from({ length: 128 }, () =>
      f.browser.runAutomation('networkBody', { timeoutMs: 0 }),
    )
    drain(f.input)
    const reason = new Error('post-open owner while many controls are held')
    owner.abort(reason)
    await expect(session.pause()).rejects.toBe(reason)
    let closed = false
    const closing = session.close().then(() => {
      closed = true
    })
    await vi.waitFor(() => expect(f.input.readableLength).toBeGreaterThan(0))
    const text = drain(f.input)
    expect(text).toContain('"action":"close"')
    expect(text).toContain('"session":"actual-controlled-token"')
    expect(text).toContain('"id":130')
    expect(closed).toBe(false)
    f.emit({ type: 'response', id: 130, ok: true }, Buffer.from('{}'))
    await closing
    for (let id = 2; id <= 129; ++id) f.emit({ type: 'response', id, ok: true }, Buffer.from('{}'))
    await Promise.all(held)
    await f.browser.close()
  })

  it('sends a debugger open at once while 128 controls are held', async () => {
    const f = await launch()
    const held = Array.from({ length: 128 }, () =>
      f.browser.runAutomation('networkBody', { timeoutMs: 0 }),
    )
    drain(f.input)
    const pending = new NativePage(f.browser).createDebuggerSession({ timeout: 0 })
    await vi.waitFor(() => expect(f.input.readableLength).toBeGreaterThan(0))
    const text = drain(f.input)
    expect(text).toContain('"action":"open"')
    expect(text).toContain('"id":129')
    f.emit(
      { type: 'response', id: 129, ok: true },
      Buffer.from('{"session":"actual-controlled-token"}'),
    )
    const session = await pending
    const closing = session.close({ timeout: 0 })
    await vi.waitFor(() => expect(drain(f.input)).toContain('"action":"close"'))
    f.emit({ type: 'response', id: 130, ok: true }, Buffer.from('{}'))
    await closing
    for (let id = 1; id <= 128; ++id) f.emit({ type: 'response', id, ok: true }, Buffer.from('{}'))
    await Promise.all(held)
    await f.browser.close()
  })
})
