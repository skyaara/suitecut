import { ChildProcess, spawn } from 'node:child_process'
import { getEventListeners } from 'node:events'
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { PassThrough } from 'node:stream'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import * as z from 'zod'

import { hasNativeAssertionDeadline } from '../src/native-assertion-deadline.js'
import { launchNativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

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
  const launch = async (framesPerSecond = 60) => {
    const promise = launchNativeBrowser({
      executablePath: path,
      width: 2,
      height: 2,
      framesPerSecond,
    })
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
  it('reports a genuine native startup error before the ready handshake', async () => {
    const promise = launchNativeBrowser({ executablePath: path })
    const rejected = expect(promise).rejects.toThrow(
      'Native browser: Native persistent profile is already in use',
    )
    await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
    emit({ type: 'error', error: 'Native persistent profile is already in use' })
    await rejected
  })
  it('preserves the selected persistent profile after a failed native launch', async () => {
    const profile = await mkdtemp(join(tmpdir(), 'suitecut-persistent-unit-'))
    try {
      const marker = join(profile, 'original-profile-data')
      await writeFile(marker, 'retained actual bytes')
      vi.mocked(spawn).mockImplementationOnce(() => {
        throw new Error('actual spawn failure')
      })
      await expect(
        launchNativeBrowser({ executablePath: path, userDataDir: profile }),
      ).rejects.toThrow('actual spawn failure')
      expect(await readFile(marker, 'utf8')).toBe('retained actual bytes')
      expect(spawn).toHaveBeenCalledWith(
        path,
        expect.arrayContaining([
          '--suitecut-persistent-profile=1',
          `--suitecut-profile=${profile}`,
        ]),
        expect.any(Object),
      )
    } finally {
      await rm(profile, { recursive: true, force: true })
    }
  })

  it('uses private pipes and delivers physical frames without browser automation', async () => {
    const browser = await launch()
    const received = vi.fn()
    browser.onFrame(received)
    const data = Buffer.alloc(16, 42)
    emit({ type: 'frame', timestampMs: 2, width: 2, height: 2, dropped: 3 }, data)
    expect(received).toHaveBeenCalledWith(expect.objectContaining({ data, droppedFrames: 3 }))
    expect(received).toHaveBeenCalledWith(
      expect.objectContaining({ timestampMs: 2 + browser.nativeClockOffsetMs! }),
    )
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

  it.each([1, 24, 47, 59, 90, 120])('requests %i FPS from the native executable', async (fps) => {
    const browser = await launch(fps)
    expect(browser.framesPerSecond).toBe(fps)
    expect(spawn).toHaveBeenCalledWith(
      path,
      expect.arrayContaining([`--suitecut-fps=${fps}`]),
      expect.anything(),
    )
    await browser.close()
  })

  it('rolls back a frame subscription if its initial cached delivery throws', async () => {
    const browser = await launch()
    const frame = { type: 'frame', timestampMs: 2, width: 2, height: 2, dropped: 0 }
    emit(frame, Buffer.alloc(16, 42))
    const rejected = vi.fn(() => {
      throw new Error('Initial consumer failed')
    })
    expect(() => browser.onFrame(rejected)).toThrow('Initial consumer failed')
    const active = vi.fn()
    browser.onFrame(active)
    emit({ ...frame, timestampMs: 3 }, Buffer.alloc(16, 84))
    expect(rejected).toHaveBeenCalledTimes(1)
    expect(active).toHaveBeenCalledTimes(2)
    expect(browser.state).toBe('running')
    await browser.close()
  })

  it('preserves an existing subscription when a duplicate cached delivery throws', async () => {
    const browser = await launch()
    const listener = vi.fn()
    const unsubscribe = browser.onFrame(listener)
    const frame = { type: 'frame', timestampMs: 2, width: 2, height: 2, dropped: 0 }
    emit(frame, Buffer.alloc(16, 42))
    listener.mockImplementationOnce(() => {
      throw new Error('Duplicate initial delivery failed')
    })
    expect(() => browser.onFrame(listener)).toThrow('Duplicate initial delivery failed')
    emit({ ...frame, timestampMs: 3 }, Buffer.alloc(16, 84))
    expect(listener).toHaveBeenCalledTimes(3)
    unsubscribe()
    emit({ ...frame, timestampMs: 4 }, Buffer.alloc(16, 126))
    expect(listener).toHaveBeenCalledTimes(3)
    await browser.close()
  })

  it('validates worker console predicates and worker events on every packet', async () => {
    const browser = await launch()
    const workers = vi.fn(),
      predicates = vi.fn()
    browser.onContextWorkerMessage!(workers)
    browser.onContextWorkerPredicate!(predicates)
    const worker = {
      contextId: 1,
      workerEvent: {
        event: 'worker',
        sequence: 1,
        worker: { id: 'worker-1', type: 'worker', url: 'https://example.test/worker.js' },
      },
    }
    const predicate = {
      contextId: 1,
      id: 1,
      generation: 1,
      token: 1,
      sequence: 2,
      clientToken: 'client-1',
      event: 'console',
      value: {
        type: 'log',
        text: 'hello',
        location: { url: 'https://example.test/worker.js', lineNumber: 0, columnNumber: 0 },
      },
    }
    emit({ type: 'contextworker' }, Buffer.from(JSON.stringify(worker)))
    emit({ type: 'contextworkerpredicate' }, Buffer.from(JSON.stringify(predicate)))
    expect(workers).toHaveBeenCalledWith(worker)
    expect(predicates).toHaveBeenCalledWith(predicate)
    emit(
      { type: 'contextworkerpredicate' },
      Buffer.from(JSON.stringify({ ...predicate, contextId: -1 })),
    )
    await expect(browser.failure).rejects.toThrow('protocol or subscriber failure')
    expect(predicates).toHaveBeenCalledTimes(1)
    await browser.closed
  })

  it.each([0, -1, 121, 29.97, Number.NaN, Number.POSITIVE_INFINITY])(
    'rejects invalid capture FPS %s before spawning',
    async (fps) => {
      await expect(
        launchNativeBrowser({ executablePath: path, framesPerSecond: fps }),
      ).rejects.toThrow()
      expect(spawn).not.toHaveBeenCalled()
    },
  )

  it.each([undefined, true, false])(
    'preserves or explicitly disables native BackForwardCache with option %s',
    async (backForwardCache) => {
      const promise = launchNativeBrowser({
        executablePath: path,
        ...(backForwardCache === undefined ? {} : { backForwardCache }),
      })
      await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
      const args = vi.mocked(spawn).mock.calls[0]?.[1]
      expect(args?.includes('--disable-features=BackForwardCache')).toBe(backForwardCache === false)
      emit({ type: 'ready', timestampMs: 1, cefVersion: 'test' })
      const browser = await promise
      await browser.close()
    },
  )

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

  it('cancels an operation without closing the source or retaining its abort listener', async () => {
    const browser = await launch()
    const controller = new AbortController()
    const pending = browser.runAutomation(
      'evaluate',
      { expression: 'new Promise(()=>{})' },
      controller.signal,
    )
    const rejected = expect(pending).rejects.toThrow('cancel operation')
    expect(getEventListeners(controller.signal, 'abort')).toHaveLength(1)
    controller.abort(new Error('cancel operation'))
    await rejected
    expect(getEventListeners(controller.signal, 'abort')).toHaveLength(0)
    expect(browser.state).toBe('running')
    // Late completion and the control acknowledgement cannot settle the next request.
    emit({ type: 'response', id: 1, ok: true })
    emit({ type: 'response', id: 2, ok: true })
    const next = browser.runAutomation('evaluate', { expression: '42' })
    emit({ type: 'response', id: 3, ok: true }, Buffer.from('{"result":{"value":42}}'))
    await expect(next).resolves.toEqual({ result: { value: 42 } })
    await browser.close()
    expect(browser.signal.aborted).toBe(true)
  })

  it('removes an operation abort observer after a successful response', async () => {
    const browser = await launch()
    const controller = new AbortController()
    const pending = browser.runAutomation('evaluate', { expression: '1' }, controller.signal)
    expect(getEventListeners(controller.signal, 'abort')).toHaveLength(1)
    emit({ type: 'response', id: 1, ok: true }, Buffer.from('{"result":{"value":1}}'))
    await pending
    expect(getEventListeners(controller.signal, 'abort')).toHaveLength(0)
    controller.abort()
    expect(browser.state).toBe('running')
    await browser.close()
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

  it('fails only the actual child renderer and retains native ownership until the real close event', async () => {
    const browser = await launch()
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-created', contextId: 2, pageId: 2, sequence: 1 })),
    )
    const second = browser.pageSource!(2)
    emit({
      type: 'ready',
      pageId: 2,
      timestampMs: 2,
      cefVersion: 'test',
      mainFrame: { id: 'second-main', parentId: '', name: '', url: 'about:blank' },
    })
    const lost = second.sendDevToolsCommand('Runtime.evaluate')
    const lostCheck = expect(lost).rejects.toThrow('renderer terminated')
    const childFailure = expect(second.failure).rejects.toThrow('renderer terminated')
    const healthy = browser.sendDevToolsCommand('Runtime.evaluate')
    input.read()
    let closed = false
    void second.closed.then(() => {
      closed = true
    })
    emit({ type: 'pagecrash', pageId: 2, error: 'Browser renderer terminated' })
    await Promise.all([lostCheck, childFailure])
    expect(second.state).toBe('failed')
    expect(second.signal.aborted).toBe(true)
    expect(browser.state).toBe('running')
    expect(browser.signal.aborted).toBe(false)
    expect(closed).toBe(false)
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting the mocked process method.
    expect(child.kill).not.toHaveBeenCalled()
    emit({ type: 'response', id: 2, ok: true }, Buffer.from('{"value":42}'))
    await expect(healthy).resolves.toEqual({ value: 42 })
    await expect(second.sendDevToolsCommand('Runtime.evaluate')).rejects.toThrow(
      'renderer terminated',
    )
    expect(input.read()).toBeNull()
    const closing = second.close()
    await vi.waitFor(() => expect(input.readableLength).toBeGreaterThan(0))
    const line = z
      .object({
        id: z.number(),
        command: z.literal('context'),
        params: z.object({ pageId: z.literal(2) }),
      })
      .parse(JSON.parse((input.read() as Buffer).toString()))
    emit({ type: 'response', id: line.id, ok: true }, Buffer.from('{}'))
    expect(closed).toBe(false)
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-closed', contextId: 2, pageId: 2, sequence: 2 })),
    )
    await closing
    expect(closed).toBe(true)
    await browser.close()
  })
  it('creates a fresh renderer epoch only on a genuine native readiness packet while old failure stays rejected', async () => {
    const browser = await launch()
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-created', contextId: 2, pageId: 2, sequence: 1 })),
    )
    const page = browser.pageSource!(2)
    const frame = {
      id: 'recovered-main',
      parentId: '',
      name: '',
      url: 'https://actual.test/first',
      navigationSequence: 1,
    }
    emit({ type: 'ready', pageId: 2, timestampMs: 2, cefVersion: 'test', mainFrame: frame })
    const oldSignal = page.signal
    const oldFailure = page.failure
    const failed = expect(oldFailure).rejects.toThrow('renderer terminated')
    emit({ type: 'pagecrash', pageId: 2, sequence: 2, error: 'Browser renderer terminated' })
    await failed
    expect(page.pageSignal?.aborted).toBe(false)
    const recovery = page.runAutomation(
      'recoverPage',
      { timeoutMs: 1000, waitUntil: 'load', api: 'page.reload' },
      page.pageSignal,
    )
    const command = z
      .object({ id: z.number(), command: z.literal('recoverPage') })
      .parse(JSON.parse((input.read() as Buffer).toString()))
    expect(page.state).toBe('failed')
    expect(page.signal).toBe(oldSignal)
    emit({
      type: 'pagerecovered',
      pageId: 2,
      rendererGeneration: 2,
      mainFrame: { ...frame, url: 'https://actual.test/second', navigationSequence: 2 },
    })
    emit({ type: 'response', pageId: 2, id: command.id, ok: true }, Buffer.from('null'))
    await expect(recovery).resolves.toBeNull()
    expect(page.state).toBe('running')
    expect(page.signal).not.toBe(oldSignal)
    expect(page.signal.aborted).toBe(false)
    expect(oldSignal.aborted).toBe(true)
    expect(page.failure).not.toBe(oldFailure)
    await expect(oldFailure).rejects.toThrow('renderer terminated')
    const nextFailure = expect(page.failure).rejects.toThrow('renderer terminated again')
    emit({ type: 'pagecrash', pageId: 2, sequence: 3, error: 'Browser renderer terminated again' })
    await nextFailure
    expect(page.pageSignal?.aborted).toBe(false)
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-closed', contextId: 2, pageId: 2, sequence: 4 })),
    )
    expect(page.pageSignal?.aborted).toBe(true)
    await browser.close()
  })
  it('keeps generic native errors fatal even when a child page ID is attached', async () => {
    const browser = await launch()
    const failed = expect(browser.failure).rejects.toThrow(
      'Native browser: fatal transport corruption',
    )
    emit({ type: 'error', pageId: 2, error: 'fatal transport corruption' })
    await Promise.all([failed, browser.closed])
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting mocked process termination.
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

  it('delivers genuine child crash listeners and preserves its native predicate wait across renderer failure', async () => {
    const browser = await launch()
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-created', contextId: 2, pageId: 2, sequence: 1 })),
    )
    if (!browser.pageSource) throw new Error('Child source unavailable')
    const source = browser.pageSource(2)
    emit({
      type: 'ready',
      pageId: 2,
      timestampMs: 2,
      cefVersion: 'test',
      mainFrame: { id: 'child', parentId: '', name: '', url: 'about:blank' },
    })
    const page = createNativePage(source)
    const calls: string[] = []
    const duplicate = (actual: typeof page) => {
      expect(actual).toBe(page)
      calls.push('duplicate')
    }
    page.on('crash', duplicate).on('crash', duplicate)
    page.prependListener('crash', () => {
      calls.push('first')
    })
    page.once('crash', () => {
      calls.push('once')
    })
    page.off('crash', duplicate)
    const observed = page.waitForEvent('crash', {
      timeout: 1000,
      predicate: async (actual) => await Promise.resolve(actual === page),
    })
    await vi.waitFor(() => expect(input.readableLength).toBeGreaterThan(0))
    const waiting = z
      .object({
        id: z.number(),
        command: z.literal('waitEventPredicate'),
        clientToken: z.string(),
        event: z.literal('crash'),
      })
      .parse(JSON.parse((input.read() as Buffer).toString()))
    emit(
      {
        type: 'eventpredicate',
        pageId: 2,
        id: waiting.id,
        generation: 1,
        token: 1,
        clientToken: waiting.clientToken,
        event: 'crash',
        sequence: 5,
      },
      Buffer.from('{}'),
    )
    emit({ type: 'pagecrash', pageId: 2, sequence: 5, error: 'Browser renderer terminated' })
    await vi.waitFor(() => expect(input.readableLength).toBeGreaterThan(0))
    const continued = z
      .object({
        id: z.number(),
        command: z.literal('continueEventPredicate'),
        target: z.number(),
        matches: z.literal(true),
      })
      .parse(JSON.parse((input.read() as Buffer).toString()))
    expect(continued.target).toBe(waiting.id)
    emit({ type: 'response', id: continued.id, pageId: 2, ok: true })
    emit({ type: 'response', id: waiting.id, pageId: 2, ok: true }, Buffer.from('{}'))
    await expect(observed).resolves.toBe(page)
    expect(calls).toEqual(['first', 'duplicate', 'once'])
    expect(source.state).toBe('failed')
    expect(source.signal.aborted).toBe(true)
    expect(source.eventSequence?.()).toBe(5)
    expect(browser.state).toBe('running')
    // A listener added after this event observes no fabricated replay.
    const later = vi.fn()
    page.on('crash', later)
    expect(later).not.toHaveBeenCalled()
    const stillWaiting = page.waitForEvent('crash', { timeout: 50 })
    const rejected = expect(stillWaiting).rejects.toThrow(/Timeout/u)
    await vi.waitFor(() => expect(input.readableLength).toBeGreaterThan(0))
    const next = z
      .object({
        id: z.number(),
        afterSequence: z.literal(5),
        command: z.literal('waitEventPredicate'),
      })
      .parse(JSON.parse((input.read() as Buffer).toString()))
    emit({
      type: 'response',
      id: next.id,
      pageId: 2,
      ok: false,
      error: 'Native event predicate Timeout exceeded',
    })
    await rejected
    await browser.close()
  })

  it('finishes renderer-failure cleanup and exposes every thrown crash listener without killing a healthy source', async () => {
    const browser = await launch()
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-created', contextId: 2, pageId: 2, sequence: 1 })),
    )
    if (!browser.pageSource) throw new Error('Actual child source unavailable')
    const source = browser.pageSource(2)
    emit({
      type: 'ready',
      pageId: 2,
      timestampMs: 2,
      cefVersion: 'test',
      mainFrame: { id: 'child', parentId: '', name: '', url: 'about:blank' },
    })
    const page = createNativePage(source)
    const pageError = new Error('public crash listener exception')
    const rawError = new Error('raw crash subscriber exception')
    const seen: string[] = []
    page.on('crash', () => {
      expect(source.signal.aborted).toBe(true)
      seen.push('throw')
      throw pageError
    })
    page.on('crash', () => {
      seen.push('after-throw')
    })
    if (!source.onCrashMessage) throw new Error('Actual crash transport unavailable')
    source.onCrashMessage(() => {
      throw rawError
    })
    const pending = source.sendDevToolsCommand('Runtime.evaluate')
    const rejected = expect(pending).rejects.toThrow('renderer terminated')
    const healthy = browser.sendDevToolsCommand('Runtime.evaluate')
    input.read()
    const scheduled: VoidFunction[] = []
    const schedule = vi.spyOn(globalThis, 'queueMicrotask').mockImplementation((callback) => {
      scheduled.push(callback)
    })
    emit({ type: 'pagecrash', pageId: 2, sequence: 5, error: 'Browser renderer terminated' })
    expect(source.state).toBe('failed')
    expect(source.signal.aborted).toBe(true)
    expect(browser.state).toBe('running')
    expect(seen).toEqual(['throw', 'after-throw'])
    const visible: Error[] = []
    for (let count = 0; scheduled.length && count < 100; ++count) {
      const callback = scheduled.shift()
      try {
        callback?.()
      } catch (reason) {
        if (!(reason instanceof Error))
          throw new Error('Unexpected queued non-Error', { cause: reason })
        visible.push(reason)
      }
    }
    schedule.mockRestore()
    expect(visible).toEqual([pageError, rawError])
    await rejected
    emit({ type: 'response', id: 2, ok: true }, Buffer.from('{"value":42}'))
    await expect(healthy).resolves.toEqual({ value: 42 })
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting the mock.
    expect(child.kill).not.toHaveBeenCalled()
    await browser.close()
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

  it('bounds transport bytes when a stalled reader receives canceled requests', async () => {
    const browser = await launch()
    let rejectedForPressure = false
    for (let i = 0; i < 100; i++) {
      const controller = new AbortController()
      const pending = browser.runAutomation(
        'evaluate',
        { expression: 'x'.repeat(60000) },
        controller.signal,
      )
      const rejected = pending.catch((error: UntrustedInput) => error)
      controller.abort(new Error('cancel transport stress'))
      const error = await rejected
      expect(error).toBeInstanceOf(Error)
      if (error instanceof Error && error.message.includes('transport buffer is full')) {
        rejectedForPressure = true
        break
      }
    }
    expect(rejectedForPressure).toBe(true)
    expect(input.writableLength).toBeLessThan(4 * 1024 * 1024 + 4096)
    expect(browser.state).toBe('running')
    input.resume()
    await browser.close()
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

  it('preserves frame identity and duplicate listener registration across event updates', async () => {
    const browser = await launch()
    const page = createNativePage(browser)
    const listener = vi.fn()
    page.on('framenavigated', listener).on('framenavigated', listener)
    const frame = { id: 'main', parentId: '', name: '', url: 'https://example.test/a' }
    emit({ type: 'event', event: 'framenavigated', sequence: 1, frame })
    const main = page.mainFrame()
    expect(listener).toHaveBeenCalledTimes(2)
    expect(listener).toHaveBeenLastCalledWith(main)
    page.off('framenavigated', listener)
    emit({
      type: 'event',
      event: 'framenavigated',
      sequence: 2,
      frame: { ...frame, url: 'https://example.test/b' },
    })
    expect(listener).toHaveBeenCalledTimes(3)
    expect(page.mainFrame()).toBe(main)
    expect(main.url()).toBe('https://example.test/b')
    expect(page.url()).toBe('https://example.test/b')
    await browser.close()
  })

  it('registers native event waits after the last sequence observed by the caller', async () => {
    const browser = await launch()
    const page = createNativePage(browser)
    const frame = { id: 'main', parentId: '', name: '', url: 'https://example.test/' }
    emit({ type: 'event', event: 'framenavigated', sequence: 7, frame })
    const next = page.waitForEvent('framenavigated')
    const command = input.read() as UntrustedInput
    expect(Buffer.isBuffer(command) ? command.toString('utf8') : null).toContain(
      '"afterSequence":7',
    )
    emit({ type: 'response', id: 1, ok: true }, Buffer.from(JSON.stringify(frame)))
    expect(await next).toBe(page.mainFrame())
    await browser.close()
  })

  it('delivers console messages with their native location and removes one-time listeners', async () => {
    const browser = await launch()
    const page = createNativePage(browser)
    const message = {
      type: 'warning',
      text: 'frame refused',
      location: { url: 'https://example.test/', lineNumber: 3, columnNumber: 7 },
    }
    const listener = vi.fn()
    page.once('console', listener)
    emit({ type: 'console', sequence: 8 }, Buffer.from(JSON.stringify(message)))
    emit({ type: 'console', sequence: 9 }, Buffer.from(JSON.stringify(message)))
    expect(listener).toHaveBeenCalledTimes(1)
    expect(browser.eventSequence?.()).toBe(9)
    expect(listener).toHaveBeenCalledWith(
      expect.objectContaining({ text: expect.any(Function), location: expect.any(Function) }),
    )
    await browser.close()
  })
  it('keeps page subscriptions and globally correlated requests separate, including child readiness', async () => {
    const browser = await launch()
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-created', contextId: 2, pageId: 2, sequence: 1 })),
    )
    const second = browser.pageSource!(2)
    const frame = { id: 'child-main', parentId: '', name: '', url: 'about:blank' }
    const operation = second.runAutomation('evaluate', { expression: '2' })
    expect(input.read()).toBeNull()
    emit({ type: 'ready', pageId: 2, timestampMs: 2, cefVersion: 'test', mainFrame: frame })
    expect(second.nativeClockOffsetMs).toBe(browser.nativeClockOffsetMs)
    await vi.waitFor(() => expect(input.readableLength).toBeGreaterThan(0))
    const line = input.read() as Buffer
    expect(JSON.parse(line.toString())).toMatchObject({ pageId: 2, id: 1, command: 'evaluate' })
    const mainListener = vi.fn(),
      secondListener = vi.fn()
    browser.onFrame(mainListener)
    second.onFrame(secondListener)
    emit(
      { type: 'frame', pageId: 2, timestampMs: 3, width: 2, height: 2, dropped: 0 },
      Buffer.alloc(16),
    )
    expect(secondListener).toHaveBeenCalledTimes(1)
    expect(mainListener).not.toHaveBeenCalled()
    emit({ type: 'response', id: 1, ok: true }, Buffer.from('{"value":2}'))
    await expect(operation).resolves.toEqual({ value: 2 })
    const close = second.close()
    expect(JSON.parse((input.read() as Buffer).toString())).toMatchObject({
      command: 'context',
      params: { pageId: 2 },
    })
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-closed', contextId: 2, pageId: 2, sequence: 2 })),
    )
    emit({ type: 'response', id: 2, ok: true })
    await close
    expect(second.state).toBe('closed')
    expect(second.signal.aborted).toBe(true)
    expect(browser.state).toBe('running')
    expect(browser.signal.aborted).toBe(false)
    expect(input.writableEnded).toBe(false)
    await browser.close()
  })
  it('keeps global context transport alive after closing the default CEF page', async () => {
    const browser = await launch()
    const page = browser.pageSource!(1)
    const closing = page.close()
    input.read()
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-closed', contextId: 1, pageId: 1, sequence: 4 })),
    )
    emit({ type: 'response', id: 1, ok: true })
    await closing
    expect(page.signal.aborted).toBe(true)
    expect(browser.signal.aborted).toBe(false)
    const creation = browser.runContext!('create', { timeoutMs: 200 })
    expect(JSON.parse((input.read() as Buffer).toString())).toMatchObject({
      command: 'context',
      timeoutMs: 200,
      params: { timeoutMs: 200 },
    })
    emit({ type: 'response', id: 2, ok: true }, Buffer.from('{"contextId":2}'))
    await expect(creation).resolves.toEqual({ contextId: 2 })
    await browser.close()
  })
  it('issues an assertion-clock receipt only for the actual tagged pending native matcher command', async () => {
    const browser = await launch()
    const pending = browser.runAutomation('queryEvaluate', {
      dom: true,
      all: true,
      expectation: true,
      nativeAssertionKind: 'toHaveText',
      function: 'function(){}',
    })
    const error = pending.catch((value: UntrustedInput) => value)
    emit(
      { type: 'response', id: 1, ok: false, error: 'Actual native assertion deadline' },
      Buffer.from(
        JSON.stringify({
          type: 'native-assertion-deadline',
          version: 1,
          operationId: 1,
          generation: '19',
          matcher: 'toHaveText',
          deadlineMs: 40,
          observed: false,
        }),
      ),
    )
    expect(hasNativeAssertionDeadline(await error, 'toHaveText')).toBe(true)
    await browser.close()
  })
  it('never issues a deadline receipt from a generic command error payload', async () => {
    const browser = await launch()
    const pending = browser.runAutomation('evaluate', { expression: '1' })
    const error = pending.catch((value: UntrustedInput) => value)
    emit(
      { type: 'response', id: 1, ok: false, error: 'Actual generic timeout' },
      Buffer.from(
        JSON.stringify({
          type: 'native-assertion-deadline',
          version: 1,
          operationId: 1,
          generation: '19',
          matcher: 'toHaveText',
          deadlineMs: 40,
          observed: false,
        }),
      ),
    )
    expect(hasNativeAssertionDeadline(await error, 'toHaveText')).toBe(false)
    expect(((await error) as Error).message).toBe('Actual generic timeout')
    await browser.close()
  })
  it('bounds control input before writing a command rejected by the native framer', async () => {
    const browser = await launch()
    await expect(
      browser.runAutomation('evaluate', { expression: 'x'.repeat(65536) }),
    ).rejects.toThrow('64 KiB')
    expect(input.read()).toBeNull()
    expect(browser.state).toBe('running')
    await browser.close()
  })
})
