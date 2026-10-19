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

describe('native debugger actual SDK control lane', () => {
  it('admits open/close while all64 ordinary automation requests remain pending', async () => {
    const f = await launch()
    const ordinary = Array.from({ length: 64 }, () =>
      f.browser.runAutomation('evaluate', { timeoutMs: 0 }),
    )
    await expect(
      f.browser.runAutomation('debugger', {
        action: 'waitForPause',
        session: 'controlled-existing',
        timeoutMs: 0,
      }),
    ).rejects.toThrow('Too many pending native commands')
    const page = new NativePage(f.browser)
    const pending = page.createDebuggerSession({ timeout: 0 })
    const sent = f.input.read() as UntrustedInput
    const text = Buffer.isBuffer(sent) ? sent.toString('utf8') : ''
    expect(text).toContain('"command":"debugger"')
    expect(text).toContain('"id":65')
    f.emit(
      { type: 'response', id: 65, ok: true },
      Buffer.from('{"session":"actual-controlled-token"}'),
    )
    const session = await pending
    const closing = session.close({ timeout: 0 })
    const close = f.input.read() as UntrustedInput
    expect(Buffer.isBuffer(close) ? close.toString('utf8') : '').toContain('"action":"close"')
    f.emit({ type: 'response', id: 66, ok: true }, Buffer.from('{}'))
    await closing
    for (let id = 1; id <= 64; ++id) f.emit({ type: 'response', id, ok: true }, Buffer.from('{}'))
    await Promise.all(ordinary)
    await f.browser.close()
  })

  it('retains post-open canonical cleanup across all128 occupied control slots', async () => {
    const f = await launch(),
      owner = new AbortController()
    const pending = new NativePage(f.browser, owner.signal).createDebuggerSession({ timeout: 0 })
    f.input.read()
    f.emit(
      { type: 'response', id: 1, ok: true },
      Buffer.from('{"session":"actual-controlled-token"}'),
    )
    const session = await pending
    const held = Array.from({ length: 128 }, () =>
      f.browser.runAutomation('networkBody', { timeoutMs: 0 }),
    )
    f.input.read()
    const reason = new Error('post-open owner while control lane full')
    owner.abort(reason)
    await expect(session.pause()).rejects.toBe(reason)
    let closed = false
    const closing = session.close().then(() => {
      closed = true
    })
    await Promise.resolve()
    expect(f.input.read()).toBeNull()
    expect(closed).toBe(false)
    // One actual control response makes room for the retained unsignaled close.
    f.emit({ type: 'response', id: 2, ok: true }, Buffer.from('{}'))
    await vi.waitFor(() => expect(f.input.readableLength).toBeGreaterThan(0))
    const packet = f.input.read() as UntrustedInput
    const text = Buffer.isBuffer(packet) ? packet.toString('utf8') : ''
    expect(text).toContain('"action":"close"')
    expect(text).toContain('"session":"actual-controlled-token"')
    expect(closed).toBe(false)
    f.emit({ type: 'response', id: 130, ok: true }, Buffer.from('{}'))
    await closing
    for (let id = 3; id <= 129; ++id) f.emit({ type: 'response', id, ok: true }, Buffer.from('{}'))
    await Promise.all(held)
    await f.browser.close()
  })

  it('cancels a queued open under the Page constructor signal without native admission', async () => {
    const f = await launch()
    const held = Array.from({ length: 128 }, () =>
      f.browser.runAutomation('networkBody', { timeoutMs: 0 }),
    )
    f.input.read()
    const owner = new AbortController(),
      reason = new Error('original queued debugger owner')
    const pending = new NativePage(f.browser, owner.signal).createDebuggerSession({ timeout: 0 })
    const rejected = expect(pending).rejects.toBe(reason)
    expect(f.input.read()).toBeNull()
    owner.abort(reason)
    await rejected
    expect(f.input.read()).toBeNull()
    for (let id = 1; id <= 128; ++id) f.emit({ type: 'response', id, ok: true }, Buffer.from('{}'))
    await Promise.all(held)
    expect(f.input.read()).toBeNull()
    await f.browser.close()
  })
})
