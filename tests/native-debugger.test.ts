// SPDX-License-Identifier: MIT
import { afterEach, describe, expect, it } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeDebuggerSession, type NativeDebuggerTransport } from '../src/native-debugger.js'
import { NativePage } from '../src/native-page.js'
import { NativeTimeoutError, nativeCommandError } from '../src/native-timeouts.js'
import { type UntrustedInput } from '../src/untrusted.js'

const lifetimes: AbortController[] = []
afterEach(async () => {
  for (const lifetime of lifetimes.splice(0)) lifetime.abort(new Error('fixture owner closed'))
  for (let index = 0; index < 8; ++index) await Promise.resolve()
})

function fixture() {
  const lifetime = new AbortController()
  lifetimes.push(lifetime)
  const calls: { params: Record<string, UntrustedInput>; signal: AbortSignal | undefined }[] = []
  let reply: UntrustedInput = { session: 'actual-native-owner' }
  const transport: NativeDebuggerTransport = {
    cleanupOwner: lifetime,
    cleanupDebugger: () => Promise.resolve({}),
    get signal() {
      return lifetime.signal
    },
    defaultTimeout: () => 500,
    runDebugger: (params, signal) => {
      calls.push({ params, signal })
      return Promise.resolve(reply)
    },
  }
  const pause = (id: string, session = 'actual-native-owner') => ({
    session,
    pause: id,
    reason: 'other',
    callFrames: [
      {
        callFrameId: 'actual-inspector-frame',
        functionName: 'realFunction\ud800',
        location: { scriptId: 'actual-script', lineNumber: 3, columnNumber: 2 },
        scopeChain: [
          { type: 'local', object: { type: 'object', objectId: 'actual-remote-scope' } },
        ],
      },
    ],
    hitBreakpoints: [],
  })
  return {
    lifetime,
    calls,
    transport,
    pause,
    reply: (value: UntrustedInput) => {
      reply = value
    },
  }
}

function pageFixture() {
  const f = fixture()
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: f.lifetime.signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation: (command, params = {}, signal) => {
      expect(command).toBe('debugger')
      return f.transport.runDebugger(params, signal)
    },
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
  }
  return { ...f, source }
}

describe('explicit native debugger SDK projection', () => {
  it('pages actual script metadata with opaque session-bound native cursors', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply({
      scripts: [{ scriptId: 'actual-1', url: 'source\ud800', attestedNative: false }],
      next: 'native-position-1',
    })
    const first = await session.scripts(undefined, { timeout: 0 })
    expect(first.scripts[0]?.url).toBe('source\ud800')
    expect(first.next?.position).toBe(1)
    expect(Object.isFrozen(first.scripts[0])).toBe(true)
    f.reply({ scripts: [{ scriptId: 'actual-native', url: '', attestedNative: true }] })
    const last = await session.scripts(first.next)
    expect(last.next).toBeUndefined()
    expect(last.scripts[0]?.attestedNative).toBe(true)
    expect(f.calls.at(-1)?.params).toMatchObject({ action: 'scripts', cursor: 'native-position-1' })
  })
  it('rejects forged and cross-session cursors without native dispatch', async () => {
    const a = fixture(),
      b = fixture()
    const first = await NativeDebuggerSession.create(a.transport),
      second = await NativeDebuggerSession.create(b.transport)
    a.reply({ scripts: [], next: 'actual-native-cursor' })
    const page = await first.scripts(),
      before = b.calls.length
    await expect(second.scripts(page.next)).rejects.toThrow('another enumeration/session')
    await expect(second.scripts({ position: 0 })).rejects.toThrow('another enumeration/session')
    expect(b.calls).toHaveLength(before)
  })
  it('preserves native stale-inventory failures and validates bounded actual page records', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply({
      scripts: Array.from({ length: 65 }, () => ({
        scriptId: 'id',
        url: '',
        attestedNative: false,
      })),
    })
    await expect(session.scripts()).rejects.toThrow()
    f.transport.runDebugger = () =>
      Promise.reject(new Error('inventory changed; restart enumeration'))
    await expect(session.scripts()).rejects.toThrow('restart enumeration')
  })

  it('forwards the actual NativePage constructor lifetime during pending native open', async () => {
    const f = pageFixture(),
      owner = new AbortController()
    const page = new NativePage(f.source, owner.signal)
    const reason = new Error('original page projection owner')
    let observed: AbortSignal | undefined
    f.transport.runDebugger = (params, signal) => {
      expect(params).toMatchObject({ action: 'open', timeoutMs: 0 })
      observed = signal
      return new Promise((_resolve, reject) => {
        signal?.addEventListener(
          'abort',
          () => {
            const cause = signal.reason as UntrustedInput
            reject(cause instanceof Error ? cause : new Error('Non-Error constructor cancellation'))
          },
          { once: true },
        )
      })
    }
    const pending = page.createDebuggerSession({ timeout: 0 })
    owner.abort(reason)
    await expect(pending).rejects.toBe(reason)
    expect(observed?.aborted).toBe(true)
  })

  it('rejects pre-aborted Page and frame projections before native admission', async () => {
    const f = pageFixture(),
      owner = new AbortController(),
      reason = new Error('preaborted page owner')
    owner.abort(reason)
    await expect(new NativePage(f.source, owner.signal).createDebuggerSession()).rejects.toBe(
      reason,
    )
    await expect(
      new NativePage(f.source, owner.signal, 'actual-child-frame').createDebuggerSession(),
    ).rejects.toBe(reason)
    expect(f.calls).toHaveLength(0)
  })
  it('keeps a frame pause usable after unsupported agent controls and forwards the exact frame', async () => {
    const f = pageFixture(),
      frame = new NativePage(f.source, undefined, 'actual-child-frame')
    const session = await frame.createDebuggerSession()
    expect(f.calls.at(-1)?.params).toMatchObject({ action: 'open', frame: 'actual-child-frame' })
    const admitted = f.calls.length
    await expect(session.pause()).rejects.toThrow('Agent-wide pause and step')
    expect(f.calls).toHaveLength(admitted)
    f.reply(f.pause('actual-frame-pause'))
    const pause = await session.waitForPause()
    for (const action of ['stepOver', 'stepInto', 'stepOut'] as const)
      await expect(session[action](pause)).rejects.toThrow('Agent-wide pause and step')
    expect(f.calls).toHaveLength(admitted + 1)
    f.reply({})
    await session.resume(pause)
    expect(f.calls.at(-1)?.params).toMatchObject({
      action: 'resume',
      frame: 'actual-child-frame',
      pause: 'actual-frame-pause',
    })
    await session.close()
  })
  it('canonical frame cleanup keeps the captured old frame and native session after owner abort', async () => {
    const f = pageFixture(),
      owner = new AbortController()
    const session = await new NativePage(
      f.source,
      owner.signal,
      'old-actual-frame',
    ).createDebuggerSession()
    let settle!: (value: UntrustedInput) => void
    f.transport.runDebugger = (params, signal) => {
      f.calls.push({ params, signal })
      expect(params).toEqual({
        action: 'close',
        session: 'actual-native-owner',
        timeoutMs: 0,
        frame: 'old-actual-frame',
      })
      expect(signal?.aborted).toBe(false)
      return new Promise((resolve) => {
        settle = resolve
      })
    }
    owner.abort(new Error('frame projection canceled'))
    let done = false
    const closing = session.close().then(() => {
      done = true
    })
    for (let index = 0; index < 5; ++index) await Promise.resolve()
    expect(done).toBe(false)
    settle({})
    await closing
    expect(f.calls.filter((call) => call.params.action === 'close')).toHaveLength(1)
  })
  it('retains one canonical native close after an already-open constructor owner aborts', async () => {
    const f = pageFixture(),
      owner = new AbortController()
    const page = new NativePage(f.source, owner.signal)
    const session = await page.createDebuggerSession()
    const reason = new Error('post-open constructor reason')
    let settle!: (value: UntrustedInput) => void
    f.transport.runDebugger = (params, signal) => {
      f.calls.push({ params, signal })
      expect(params).toEqual({ action: 'close', session: 'actual-native-owner', timeoutMs: 0 })
      expect(signal?.aborted).toBe(false)
      return new Promise((resolve) => {
        settle = resolve
      })
    }
    owner.abort(reason)
    await expect(session.pause()).rejects.toBe(reason)
    let closed = false
    const closing = session.close().then(() => {
      closed = true
    })
    await Promise.resolve()
    expect(f.calls.filter((call) => call.params.action === 'close')).toHaveLength(1)
    expect(closed).toBe(false)
    settle({})
    await closing
    await session.close()
    expect(f.calls.filter((call) => call.params.action === 'close')).toHaveLength(1)
  })

  it('waits for the actual page-closed promise when owner cancellation races Source close', async () => {
    const f = pageFixture(),
      owner = new AbortController()
    let state: 'running' | 'closing' = 'running'
    let physicallyClosed!: () => void
    Object.defineProperties(f.source, {
      state: { get: () => state },
      closed: {
        value: new Promise<void>((resolve) => {
          physicallyClosed = resolve
        }),
      },
    })
    const session = await new NativePage(f.source, owner.signal).createDebuggerSession()
    state = 'closing'
    f.transport.runDebugger = () =>
      Promise.reject(new Error('actual channel no longer accepts commands'))
    owner.abort(new Error('constructor caller canceled'))
    let closed = false
    const pending = session.close().then(() => {
      closed = true
    })
    for (let index = 0; index < 5; ++index) await Promise.resolve()
    expect(closed).toBe(false)
    physicallyClosed()
    await pending
    expect(closed).toBe(true)
  })

  it('does not classify an arbitrary close error as a physical page boundary', async () => {
    const f = pageFixture(),
      owner = new AbortController()
    const session = await new NativePage(f.source, owner.signal).createDebuggerSession()
    const failure = new Error('page closed (author-controlled error wording)')
    f.transport.runDebugger = () => Promise.reject(failure)
    owner.abort(new Error('constructor owner'))
    await expect(session.close()).rejects.toBe(failure)
    await expect(new NativePage(f.source).createDebuggerSession()).rejects.toMatchObject({
      errors: [failure],
      message: 'Previous native debugger cleanup failed',
    })
  })

  it('joins normal close when owner abort happens synchronously inside its admission', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    let canonical = 0
    f.transport.cleanupDebugger = () => {
      ++canonical
      return Promise.resolve({})
    }
    f.transport.runDebugger = () => {
      f.lifetime.abort(new Error('synchronous owner abort'))
      return Promise.resolve({})
    }
    await session.close()
    await session.close()
    expect(canonical).toBe(0)
  })

  it('captures an owner abort between the actual open reply and listener registration', async () => {
    const f = pageFixture(),
      owner = new AbortController()
    const reason = new Error('open-result lifetime race')
    f.transport.runDebugger = (params, signal) => {
      f.calls.push({ params, signal })
      if (params.action === 'open') {
        owner.abort(reason)
        return Promise.resolve({ session: 'actual-native-owner' })
      }
      expect(signal?.aborted).toBe(false)
      return Promise.resolve({})
    }
    await expect(new NativePage(f.source, owner.signal).createDebuggerSession()).rejects.toBe(
      reason,
    )
    await Promise.resolve()
    expect(f.calls.filter((call) => call.params.action === 'close')).toHaveLength(1)
  })

  it('joins an accepted ordinary close after owner abort without duplicate cleanup', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    let settle!: (value: UntrustedInput) => void
    f.transport.runDebugger = () =>
      new Promise((resolve) => {
        settle = resolve
      })
    let canonical = 0
    f.transport.cleanupDebugger = () => {
      ++canonical
      return Promise.resolve({})
    }
    const first = session.close({ timeout: 0 })
    f.lifetime.abort(new Error('owner canceled while native close physically held'))
    const joined = session.close()
    await Promise.resolve()
    expect(canonical).toBe(0)
    settle({})
    await Promise.all([first, joined])
    expect(canonical).toBe(0)
  })

  it('retains canonical cleanup after pre-admission close cancellation', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    let settle!: (value: UntrustedInput) => void
    let canonical = 0
    f.transport.cleanupDebugger = () => {
      ++canonical
      return new Promise((resolve) => {
        settle = resolve
      })
    }
    const caller = new AbortController(),
      reason = new Error('original close caller')
    caller.abort(reason)
    await expect(session.close({ signal: caller.signal })).rejects.toBe(reason)
    const joined = session.close()
    expect(canonical).toBe(1)
    settle({})
    await joined
    expect(f.calls).toHaveLength(1)
  })

  it('retains failed cleanup and reports its real error before another same-source open', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    const failure = new Error('actual canonical close receipt failed')
    f.transport.cleanupDebugger = () => Promise.reject(failure)
    f.lifetime.abort(new Error('constructor owner canceled'))
    await expect(session.close()).rejects.toBe(failure)
    const next = new AbortController()
    lifetimes.push(next)
    const retry: NativeDebuggerTransport = { ...f.transport, signal: next.signal }
    const count = f.calls.length
    await expect(NativeDebuggerSession.create(retry)).rejects.toMatchObject({
      errors: [failure],
      message: 'Previous native debugger cleanup failed',
    })
    expect(f.calls).toHaveLength(count)
    f.transport.cleanupDebugger = () => Promise.resolve({})
  })

  it('bounds retained cleanup subscriptions and pending native cleanup receipts to sixteen', async () => {
    const fixtures = Array.from({ length: 16 }, fixture)
    const sessions = await Promise.all(
      fixtures.map((f) => NativeDebuggerSession.create(f.transport)),
    )
    const receipts: (() => void)[] = []
    for (const f of fixtures) {
      f.transport.cleanupDebugger = () =>
        new Promise((resolve) => {
          receipts.push(() => resolve({}))
        })
      f.lifetime.abort(new Error('constructor cancellation'))
    }
    await Promise.resolve()
    const extra = fixture()
    await expect(NativeDebuggerSession.create(extra.transport)).rejects.toThrow('Too many retained')
    expect(extra.calls).toHaveLength(0)
    expect(receipts).toHaveLength(16)
    for (const receipt of receipts) receipt()
    await Promise.all(sessions.map((session) => session.close()))
    const admitted = await NativeDebuggerSession.create(extra.transport)
    extra.reply({})
    await admitted.close()
  })

  it('preserves native original-budget timeout classes without relabeling physical cleanup failure', () => {
    for (const text of [
      'Native Debugger acquisition exceeded its original deadline',
      'Native Debugger pause exceeded its original document deadline',
      'Native debugger original deadline expired during open',
    ])
      expect(nativeCommandError(text)).toBeInstanceOf(NativeTimeoutError)
    expect(
      nativeCommandError('Native Debugger physical cleanup exceeded its watchdog'),
    ).not.toBeInstanceOf(NativeTimeoutError)
    expect(
      nativeCommandError('author Debugger pause exceeded its original document deadline'),
    ).not.toBeInstanceOf(NativeTimeoutError)
  })
  it('forwards zero-disabled budgets and real pause metadata without implementing a protocol client', async () => {
    const f = fixture()
    const session = await NativeDebuggerSession.create(f.transport, { timeout: 0 })
    expect(f.calls[0]?.params).toEqual({ action: 'open', timeoutMs: 0 })
    f.reply(f.pause('native-pause-1'))
    const pause = await session.pause({ timeout: 0 })
    expect(f.calls[1]?.params).toEqual({
      action: 'pause',
      session: 'actual-native-owner',
      timeoutMs: 0,
    })
    expect(pause.callFrames[0]?.functionName).toBe('realFunction\ud800')
    expect(pause.callFrames[0]?.location).toEqual({
      scriptId: 'actual-script',
      lineNumber: 3,
      columnNumber: 2,
    })
    expect(pause).not.toHaveProperty('pause')
  })

  it('rejects forged/cross-session pauses before sending a native command', async () => {
    const f = fixture()
    const session = await NativeDebuggerSession.create(f.transport)
    const other = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('native-pause-1'))
    const pause = await session.waitForPause()
    const count = f.calls.length
    await expect(other.resume(pause)).rejects.toThrow('another session')
    await expect(session.resume({ ...pause })).rejects.toThrow('another session')
    expect(f.calls).toHaveLength(count)
  })

  it('consumes old pause control before awaiting its actual continuation reply', async () => {
    const f = fixture()
    const session = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('native-pause-1'))
    const pause = await session.pause()
    let release!: (input: UntrustedInput) => void
    f.transport.runDebugger = (params, signal) => {
      f.calls.push({ params, signal })
      return new Promise((resolve) => {
        release = resolve
      })
    }
    const pending = session.stepInto(pause)
    await expect(session.resume(pause)).rejects.toThrow('stale')
    expect(f.calls.at(-1)?.params).toEqual({
      action: 'stepInto',
      session: 'actual-native-owner',
      pause: 'native-pause-1',
      timeoutMs: 500,
    })
    release(f.pause('native-pause-2'))
    const next = await pending
    expect(next.callFrames).toEqual(pause.callFrames)
    f.transport.runDebugger = () => Promise.resolve({})
    await session.resume(next)
  })

  it('preserves a usable pause after pre-aborted continuation admission', async () => {
    const f = fixture()
    const session = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('native-pause-1'))
    const pause = await session.waitForPause()
    const cancel = new AbortController()
    const reason = new Error('original caller reason')
    cancel.abort(reason)
    const count = f.calls.length
    await expect(session.resume(pause, { signal: cancel.signal })).rejects.toBe(reason)
    expect(f.calls).toHaveLength(count)
    f.reply({})
    await session.resume(pause)
  })

  it('forwards both actual Page lifetime and per-call signals without SDK retries', async () => {
    const f = fixture()
    const session = await NativeDebuggerSession.create(f.transport)
    const cancel = new AbortController()
    f.reply({ source: 'actual source\udfff' })
    expect(
      await session.scriptSource('actual-script', { timeout: 75, signal: cancel.signal }),
    ).toBe('actual source\udfff')
    const sent = f.calls.at(-1)
    expect(sent?.params).toEqual({
      action: 'scriptSource',
      session: 'actual-native-owner',
      scriptId: 'actual-script',
      timeoutMs: 75,
    })
    expect(sent?.signal?.aborted).toBe(false)
    const reason = new Error('real source lifetime ended')
    f.lifetime.abort(reason)
    expect(sent?.signal?.reason).toBe(reason)
    const count = f.calls.length
    await expect(session.pause()).rejects.toBe(reason)
    expect(f.calls).toHaveLength(count)
  })

  it('forwards close once then prevents controls while actual native cleanup is pending', async () => {
    const f = fixture()
    const session = await NativeDebuggerSession.create(f.transport)
    let release!: (input: UntrustedInput) => void
    f.transport.runDebugger = (params, signal) => {
      f.calls.push({ params, signal })
      return new Promise((resolve) => {
        release = resolve
      })
    }
    const close = session.close({ timeout: 0 })
    await expect(session.pause()).rejects.toThrow('closing')
    expect(f.calls.at(-1)?.params).toEqual({
      action: 'close',
      session: 'actual-native-owner',
      timeoutMs: 0,
    })
    release({})
    await close
    await expect(session.waitForPause()).rejects.toThrow('closed')
  })

  it('rejects native cross-owner/unknown metadata and arbitrary remote by-value fields', async () => {
    const f = fixture()
    const session = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('wrong', 'different-actual-owner'))
    await expect(session.pause()).rejects.toThrow('another session')
    const packet = f.pause('native-pause-1')
    f.reply({
      ...packet,
      callFrames: packet.callFrames.map((item) => ({
        ...item,
        this: { type: 'object', value: { injected: true } },
      })),
    })
    await expect(session.waitForPause()).rejects.toThrow()
  })

  it('projects actual resolved breakpoint locations and sends only its own opaque removal token', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply({
      breakpoint: 'native-owned-breakpoint',
      location: { scriptId: 'actual-script', lineNumber: 5, columnNumber: 2 },
    })
    const breakpoint = await session.setBreakpoint({ scriptId: 'actual-script', lineNumber: 4 })
    expect(breakpoint).toEqual({
      location: { scriptId: 'actual-script', lineNumber: 5, columnNumber: 2 },
    })
    expect(Object.isFrozen(breakpoint.location)).toBe(true)
    expect(f.calls.at(-1)?.params).toEqual({
      action: 'setBreakpoint',
      session: 'actual-native-owner',
      location: { scriptId: 'actual-script', lineNumber: 4 },
      timeoutMs: 500,
    })
    f.reply({})
    await session.removeBreakpoint(breakpoint)
    expect(f.calls.at(-1)?.params).toEqual({
      action: 'removeBreakpoint',
      session: 'actual-native-owner',
      breakpoint: 'native-owned-breakpoint',
      timeoutMs: 500,
    })
    const count = f.calls.length
    await expect(session.removeBreakpoint(breakpoint)).rejects.toThrow('stale')
    expect(f.calls).toHaveLength(count)
  })

  it('rejects forged or cross-session breakpoint handles before native removal', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport),
      other = await NativeDebuggerSession.create(f.transport)
    f.reply({
      breakpoint: 'native-owned-breakpoint',
      location: { scriptId: 'actual-script', lineNumber: 5 },
    })
    const breakpoint = await session.setBreakpoint({ scriptId: 'actual-script', lineNumber: 4 })
    const count = f.calls.length
    await expect(other.removeBreakpoint(breakpoint)).rejects.toThrow('another session')
    await expect(session.removeBreakpoint({ ...breakpoint })).rejects.toThrow('another session')
    expect(f.calls).toHaveLength(count)
  })

  it('preserves a genuine breakpoint after pre-aborted removal and validates requested locations', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply({
      breakpoint: 'native-owned-breakpoint',
      location: { scriptId: 'actual-script', lineNumber: 5 },
    })
    const breakpoint = await session.setBreakpoint({ scriptId: 'actual-script', lineNumber: 4 })
    const caller = new AbortController(),
      reason = new Error('original removal owner')
    caller.abort(reason)
    const count = f.calls.length
    await expect(session.removeBreakpoint(breakpoint, { signal: caller.signal })).rejects.toBe(
      reason,
    )
    await expect(
      session.setBreakpoint({ scriptId: 'actual-script', lineNumber: -1 }),
    ).rejects.toThrow()
    expect(f.calls).toHaveLength(count)
    f.reply({})
    await session.removeBreakpoint(breakpoint, { timeout: 0 })
    expect(f.calls.at(-1)?.params.timeoutMs).toBe(0)
  })

  it('does not adopt a native breakpoint location from a different script', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply({
      breakpoint: 'native-owned-breakpoint',
      location: { scriptId: 'foreign-script', lineNumber: 5 },
    })
    await expect(
      session.setBreakpoint({ scriptId: 'actual-script', lineNumber: 4 }),
    ).rejects.toThrow('another script')
  })

  it('projects actual paused primitive values and forwards captured frame identity with direct source', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('actual-pause'))
    const pause = await session.waitForPause(),
      frame = pause.callFrames[0]
    expect(frame).toBeDefined()
    if (!frame) throw new Error('fixture frame missing')
    const cases = [
      [{ type: 'undefined' }, undefined],
      [{ type: 'null' }, null],
      [{ type: 'string', value: 'real scalar\ud800' }, 'real scalar\ud800'],
      [{ type: 'boolean', value: true }, true],
      [{ type: 'number', value: 42 }, 42],
      [{ type: 'specialNumber', value: 'NaN' }, Number.NaN],
      [{ type: 'specialNumber', value: '-0' }, -0],
      [{ type: 'specialNumber', value: 'Infinity' }, Number.POSITIVE_INFINITY],
      [{ type: 'specialNumber', value: '-Infinity' }, Number.NEGATIVE_INFINITY],
      [{ type: 'bigint', value: '-9007199254740993' }, -9007199254740993n],
    ] as const
    // Mutating public metadata cannot replace the actual captured frame ID.
    frame.callFrameId = 'forged-after-observation'
    for (const [wire, expected] of cases) {
      f.reply({ kind: 'value', value: wire })
      const result = await session.evaluateOnCallFrame(pause, frame, 'actualLocal + 1')
      expect(result).toEqual({ kind: 'value', value: expected })
      expect(f.calls.at(-1)?.params).toEqual({
        action: 'evaluateOnCallFrame',
        session: 'actual-native-owner',
        pause: 'actual-pause',
        callFrameId: 'actual-inspector-frame',
        expressionJSON: JSON.stringify('actualLocal + 1'),
        timeoutMs: 500,
      })
    }
  })

  it('preserves genuine exception receipts and thrown primitive units without inventing remote handles', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('actual-pause'))
    const pause = await session.waitForPause(),
      frame = pause.callFrames[0]
    if (!frame) throw new Error('fixture frame missing')
    f.reply({
      kind: 'exception',
      exception: {
        text: 'Uncaught',
        description: 'Error: actual\udfff',
        lineNumber: 3,
        columnNumber: 2,
        scriptId: 'actual-script',
        thrown: { type: 'string', value: 'actual\ud800' },
      },
    })
    expect(await session.evaluateOnCallFrame(pause, frame, 'throw actualException')).toEqual({
      kind: 'exception',
      exception: {
        text: 'Uncaught',
        description: 'Error: actual\udfff',
        lineNumber: 3,
        columnNumber: 2,
        scriptId: 'actual-script',
        thrown: 'actual\ud800',
      },
    })
    f.reply({
      kind: 'exception',
      exception: {
        text: 'Uncaught',
        lineNumber: 0,
        columnNumber: 0,
        thrown: { type: 'undefined' },
      },
    })
    const result = await session.evaluateOnCallFrame(pause, frame, 'throw undefined')
    expect(result.kind === 'exception' && Object.hasOwn(result.exception, 'thrown')).toBe(true)
  })

  it('rejects forged, foreign or consumed paused-frame membership before RPC', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('actual-pause'))
    const pause = await session.waitForPause(),
      frame = pause.callFrames[0]
    if (!frame) throw new Error('fixture frame missing')
    const count = f.calls.length
    await expect(session.evaluateOnCallFrame({ ...pause }, frame, '1')).rejects.toThrow(
      'actual pause and frame',
    )
    await expect(session.evaluateOnCallFrame(pause, { ...frame }, '1')).rejects.toThrow(
      'actual pause and frame',
    )
    expect(f.calls).toHaveLength(count)
    f.reply({})
    await session.resume(pause)
    const afterResume = f.calls.length
    await expect(session.evaluateOnCallFrame(pause, frame, '1')).rejects.toThrow(
      'actual pause and frame',
    )
    expect(f.calls).toHaveLength(afterResume)
  })

  it('keeps a usable pause after rejected source or pre-aborted evaluation admission', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('actual-pause'))
    const pause = await session.waitForPause(),
      frame = pause.callFrames[0]
    if (!frame) throw new Error('fixture frame missing')
    const caller = new AbortController(),
      reason = new Error('original paused evaluation reason')
    caller.abort(reason)
    const count = f.calls.length
    await expect(
      session.evaluateOnCallFrame(pause, frame, '1', { signal: caller.signal }),
    ).rejects.toBe(reason)
    for (const invalid of ['', 'x'.repeat(8193), "'\ud800'"])
      await expect(session.evaluateOnCallFrame(pause, frame, invalid)).rejects.toThrow()
    expect(f.calls).toHaveLength(count)
    f.reply({ kind: 'value', value: { type: 'number', value: 1 } })
    await session.evaluateOnCallFrame(pause, frame, '1', { timeout: 0 })
    expect(f.calls.at(-1)?.params.timeoutMs).toBe(0)
    f.reply({})
    await session.resume(pause)
  })

  it('rejects remote-object or malformed result metadata instead of adopting by-value author objects', async () => {
    const f = fixture(),
      session = await NativeDebuggerSession.create(f.transport)
    f.reply(f.pause('actual-pause'))
    const pause = await session.waitForPause(),
      frame = pause.callFrames[0]
    if (!frame) throw new Error('fixture frame missing')
    f.reply({
      kind: 'value',
      value: { type: 'object', value: { __suitecutNativeUTF16Scalar: 'd800' } },
    })
    await expect(session.evaluateOnCallFrame(pause, frame, '({})')).rejects.toThrow()
    f.reply({ kind: 'value', value: { type: 'bigint', value: '1e6' } })
    await expect(session.evaluateOnCallFrame(pause, frame, '1n')).rejects.toThrow()
  })

  it('rejects invalid options and oversized source before any extra native command', async () => {
    const f = fixture()
    const session = await NativeDebuggerSession.create(f.transport)
    const count = f.calls.length
    await expect(session.pause({ timeout: -1 })).rejects.toThrow()
    await expect(session.scriptSource('')).rejects.toThrow()
    expect(f.calls).toHaveLength(count)
    f.reply({ source: 'x'.repeat(262_145) })
    await expect(session.scriptSource('actual-script')).rejects.toThrow()
  })
})
