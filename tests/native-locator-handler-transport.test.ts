// SPDX-License-Identifier: MIT
import { AsyncLocalStorage } from 'node:async_hooks'
import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { afterEach, describe, expect, it, vi } from 'vitest'

import { launchNativeBrowser } from '../src/native-browser.js'
import { type UntrustedInput } from '../src/untrusted.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))
afterEach(() => vi.restoreAllMocks())
function read(input: PassThrough): Record<string, UntrustedInput> {
  const bytes: UntrustedInput = input.read()
  if (!Buffer.isBuffer(bytes)) throw new Error('Actual SDK did not dispatch a command')
  return JSON.parse(bytes.toString('utf8').trim()) as Record<string, UntrustedInput>
}
describe('native locator handler retained SDK transport', () => {
  it('routes genuine after-close handler stages before stale-child filtering and retains actual action ALS', async () => {
    // Controlled real SDK decoder/streams; no CEF browser or native policy claim.
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
    const emit = (header: object, data = Buffer.alloc(0)): void => {
      const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
      const prefix = Buffer.alloc(4)
      prefix.writeUInt32LE(json.length)
      output.write(Buffer.concat([prefix, json, data]))
    }
    const launching = launchNativeBrowser({
      executablePath: '/controlled/native-browser',
      width: 2,
      height: 2,
    })
    await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
    emit({ type: 'ready', timestampMs: 1, cefVersion: 'controlled' })
    const browser = await launching
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-created', contextId: 2, pageId: 2, sequence: 1 })),
    )
    if (!browser.pageSource) throw new Error('Actual page transport unavailable')
    const page = browser.pageSource(2)
    emit({ type: 'ready', pageId: 2, timestampMs: 2, cefVersion: 'controlled' })
    const current = new AsyncLocalStorage<string>(),
      received: string[] = []
    let owner: string | undefined
    if (!page.onLocatorHandlerStage) throw new Error('Actual handler transport unavailable')
    page.onLocatorHandlerStage((event) => {
      received.push(event.stage)
      if (event.stage === 'locatorHandlerRun') owner = current.getStore()
    })
    const abort = new AbortController()
    const action = current.run('actual action owner', () =>
      page.runAutomation(
        'locator',
        { action: 'click', selector: 'button', timeoutMs: 0 },
        abort.signal,
      ),
    )
    const request = read(input)
    const rejection = expect(action).rejects.toThrow('actual caller cancel')
    const handler = {
      type: 'actionstage',
      pageId: 2,
      id: request.id,
      generation: 3,
      token: 5,
      handlerId: 7,
      clientToken: 'native-client',
    }
    emit({ ...handler, stage: 'locatorHandlerRun' })
    expect(owner).toBe('actual action owner')
    abort.abort(new Error('actual caller cancel'))
    await rejection
    read(input) // The original cancel command was genuinely dispatched.
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-closed', contextId: 2, pageId: 2, sequence: 2 })),
    )
    expect(page.state).toBe('closed')
    emit({ ...handler, stage: 'locatorHandlerAbort' })
    const raw = page.runAutomation('locatorHandlerRaw', {
      target: request.id,
      generation: 3,
      token: 5,
      handlerId: 7,
      clientToken: 'native-client',
      ok: true,
      operationsPending: false,
    })
    const rawRequest = read(input)
    expect(rawRequest).toMatchObject({ pageId: 2, command: 'locatorHandlerRaw' })
    emit({ type: 'response', pageId: 2, id: rawRequest.id, ok: true })
    await raw
    emit({
      type: 'response',
      pageId: 2,
      id: request.id,
      ok: false,
      error: 'actual canceled native action',
    })
    emit({ ...handler, stage: 'locatorHandlerRetired', removed: true })
    expect(received).toEqual(['locatorHandlerRun', 'locatorHandlerAbort', 'locatorHandlerRetired'])
    expect(page.state).toBe('closed')
    expect(browser.state).toBe('running')
    await browser.close()
  })
})
