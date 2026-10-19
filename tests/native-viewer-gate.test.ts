// SPDX-License-Identifier: MIT
import { afterEach, describe, expect, it, vi } from 'vitest'

import {
  assertNativeViewerAdmission,
  NativeViewerGate,
  runNativeViewerRequest,
} from '../src/native-viewer-gate.js'
import { type UntrustedInput } from '../src/untrusted.js'

function deferred<T = void>() {
  let resolve!: (value: T) => void
  let reject!: (reason: UntrustedInput) => void
  const promise = new Promise<T>((accept, fail) => {
    resolve = accept
    reject = fail
  })
  return { promise, resolve, reject }
}
const turn = () =>
  new Promise<void>((resolve) => {
    setImmediate(resolve)
  })
const gates: NativeViewerGate[] = []
function fixture() {
  const process = new AbortController()
  const gate = new NativeViewerGate(process.signal)
  gates.push(gate)
  const admit = (command = 'evaluate', fields = {}, continuation = false) =>
    assertNativeViewerAdmission(process.signal, command, fields, continuation)
  return { process, gate, admit }
}
afterEach(() => {
  for (const gate of gates.splice(0)) gate.dispose()
})

describe('native viewer process control gate', () => {
  it('pauses every alias of one process while leaving another process independent', async () => {
    const a = fixture(),
      b = fixture()
    expect(a.admit).not.toThrow()
    a.gate.transition('human')
    expect(a.admit).toThrow(/paused for human control/)
    expect(() => assertNativeViewerAdmission(a.process.signal, 'navigate', {}, false)).toThrow(
      /paused for human control/,
    )
    expect(b.admit).not.toThrow()
    await expect(
      b.gate.runAgent(() => {
        b.admit()
        return Promise.resolve(42)
      }),
    ).resolves.toBe(42)
  })

  it('refuses two viewers for the same live helper and releases ownership on dispose', () => {
    const f = fixture()
    expect(() => new NativeViewerGate(f.process.signal)).toThrow(/already has a viewer/)
    f.gate.dispose()
    const replacement = new NativeViewerGate(f.process.signal)
    gates.push(replacement)
    expect(replacement.mode).toBe('agent')
  })

  it('allows an active viewer lease but rejects descendants after its callback returns', async () => {
    const f = fixture(),
      resume = deferred()
    let stale!: Promise<void>
    f.gate.transition('human')
    await f.gate.runViewer(() => {
      f.admit()
      stale = resume.promise.then(() => {
        f.admit()
      })
      return Promise.resolve()
    })
    const rejected = expect(stale).rejects.toThrow(/control lease expired/)
    resume.resolve()
    await rejected
    expect(f.admit).toThrow(/paused for human control/)
  })

  it('invalidates a still-running viewer lease when the ownership generation changes', async () => {
    const f = fixture(),
      entered = deferred(),
      resume = deferred()
    f.gate.transition('human')
    const action = f.gate.runViewer(async () => {
      f.admit()
      entered.resolve()
      await resume.promise
      f.admit()
    })
    await entered.promise
    f.gate.transition('releasing')
    const rejected = expect(action).rejects.toThrow(/control lease expired/)
    resume.resolve()
    await rejected
  })

  it('waits for an admitted agent step to finish before takeover can drain', async () => {
    const f = fixture(),
      entered = deferred(),
      finish = deferred()
    const step = f.gate.runAgent(async () => {
      entered.resolve()
      await finish.promise
      f.admit() // Previously admitted work can finish during taking-over.
      return 7
    })
    await entered.promise
    f.gate.transition('taking-over')
    expect(f.admit).toThrow(/paused for human control/)
    const drained = vi.fn()
    const draining = f.gate.drain(new AbortController().signal).then(drained)
    await turn()
    expect(drained).not.toHaveBeenCalled()
    finish.resolve()
    await expect(step).resolves.toBe(7)
    await draining
    expect(drained).toHaveBeenCalledOnce()
  })

  it('holds a new agent step through human and release modes until agent control returns', async () => {
    const f = fixture(),
      work = vi.fn(() => Promise.resolve(19))
    f.gate.transition('taking-over')
    const step = f.gate.runAgent(work)
    f.gate.transition('human')
    await turn()
    expect(work).not.toHaveBeenCalled()
    f.gate.transition('releasing')
    await turn()
    expect(work).not.toHaveBeenCalled()
    f.gate.transition('agent')
    await expect(step).resolves.toBe(19)
    expect(work).toHaveBeenCalledOnce()
  })

  it('does not let a completed agent step leave an asynchronous takeover bypass', async () => {
    const f = fixture(),
      resume = deferred()
    let stale!: Promise<void>
    await f.gate.runAgent(() => {
      stale = resume.promise.then(() => {
        f.admit()
      })
      return Promise.resolve()
    })
    f.gate.transition('taking-over')
    const rejected = expect(stale).rejects.toThrow(/paused for human control/)
    resume.resolve()
    await rejected
    await f.gate.drain(new AbortController().signal)
  })

  it('retains SDK requests from queue admission through their actual completion', async () => {
    const f = fixture(),
      queue = deferred(),
      response = deferred<number>()
    const dispatched = vi.fn()
    const request = runNativeViewerRequest(f.process.signal, async () => {
      await queue.promise
      dispatched()
      return response.promise
    })
    f.gate.transition('taking-over')
    const drained = vi.fn()
    const draining = f.gate.drain(new AbortController().signal).then(drained)
    await turn()
    expect(dispatched).not.toHaveBeenCalled()
    expect(drained).not.toHaveBeenCalled()
    queue.resolve()
    await turn()
    expect(dispatched).toHaveBeenCalledOnce()
    expect(drained).not.toHaveBeenCalled()
    response.resolve(23)
    await expect(request).resolves.toBe(23)
    await draining
    expect(drained).toHaveBeenCalledOnce()
  })

  it('releases request ownership after rejection so takeover does not wait forever', async () => {
    const f = fixture(),
      response = deferred()
    const request = runNativeViewerRequest(f.process.signal, () => response.promise)
    const failed = expect(request).rejects.toThrow('native command failed')
    f.gate.transition('taking-over')
    const draining = f.gate.drain(new AbortController().signal)
    response.reject(new Error('native command failed'))
    await failed
    await draining
  })

  it('exempts viewer requests only for the process whose viewer lease is held', async () => {
    const a = fixture(),
      b = fixture(),
      ownResponse = deferred(),
      siblingResponse = deferred()
    a.gate.transition('human')
    let ownRequest!: Promise<void>, siblingRequest!: Promise<void>
    await a.gate.runViewer(() => {
      ownRequest = runNativeViewerRequest(a.process.signal, () => ownResponse.promise)
      siblingRequest = runNativeViewerRequest(b.process.signal, () => siblingResponse.promise)
      expect(() => b.admit()).not.toThrow()
      return Promise.resolve()
    })
    await a.gate.drain(new AbortController().signal)
    const siblingDrained = vi.fn()
    const draining = b.gate.drain(new AbortController().signal).then(siblingDrained)
    await turn()
    expect(siblingDrained).not.toHaveBeenCalled()
    ownResponse.resolve()
    await ownRequest
    expect(siblingDrained).not.toHaveBeenCalled()
    siblingResponse.resolve()
    await siblingRequest
    await draining
  })

  it('permits actual callback continuations but blocks authored destructive continuations', () => {
    const f = fixture()
    f.gate.transition('human')
    expect(() => f.admit('routeCallback', { action: 'fulfill' }, true)).not.toThrow()
    expect(() => f.admit('context', { action: 'workerNetworkBody' }, true)).not.toThrow()
    expect(() => f.admit('handle', { action: 'dispose' }, true)).not.toThrow()
    expect(() => f.admit('bindings', { action: 'resolve' }, true)).not.toThrow()
    expect(() =>
      f.admit('context', { action: 'screenshotInput', params: { action: 'discard' } }, true),
    ).not.toThrow()
    expect(() => f.admit('routeCallback', { action: 'fulfill' }, false)).toThrow(/paused/)
    expect(() => f.admit('evaluate', {}, true)).toThrow(/paused/)
    expect(() =>
      f.admit('context', { action: 'screenshotInput', params: { action: 'click' } }, true),
    ).toThrow(/paused/)
    expect(() => f.admit('handle', { action: 'evaluate' }, true)).toThrow(/paused/)
    expect(() => f.admit('recoverPage', {}, true)).toThrow(/paused/)
    expect(() => f.admit('context', { action: 'close' }, true)).toThrow(/paused/)
    expect(() => f.admit('context', { action: 'closePage' }, true)).toThrow(/paused/)
  })

  it('rejects waiting steps and drains when the actual helper process aborts', async () => {
    const f = fixture(),
      pending = deferred()
    const request = runNativeViewerRequest(f.process.signal, () => pending.promise)
    f.gate.transition('human')
    const work = vi.fn(() => Promise.resolve())
    const reason = new Error('helper exited')
    const stepRejected = expect(f.gate.runAgent(work)).rejects.toBe(reason)
    const drainRejected = expect(f.gate.drain(new AbortController().signal)).rejects.toBe(reason)
    f.process.abort(reason)
    await stepRejected
    await drainRejected
    expect(work).not.toHaveBeenCalled()
    pending.resolve()
    await request
    await expect(f.gate.runAgent(work)).rejects.toBe(reason)
  })

  it('rejects waiting agent steps on dispose without invoking their work', async () => {
    const f = fixture(),
      work = vi.fn(() => Promise.resolve()),
      pending = deferred()
    const request = runNativeViewerRequest(f.process.signal, () => pending.promise)
    f.gate.transition('human')
    const rejected = expect(f.gate.runAgent(work)).rejects.toThrow(/viewer is closed/)
    const drainRejected = expect(f.gate.drain(new AbortController().signal)).rejects.toThrow(
      /viewer is closed/,
    )
    f.gate.dispose()
    await rejected
    await drainRejected
    expect(work).not.toHaveBeenCalled()
    await expect(f.gate.runAgent(work)).rejects.toThrow(/viewer is closed/)
    pending.resolve()
    await request
  })

  it('supports cancellation of one waiting step without unblocking another', async () => {
    const f = fixture(),
      cancelled = new AbortController(),
      reason = new Error('step cancelled')
    f.gate.transition('human')
    const cancelledWork = vi.fn(() => Promise.resolve()),
      remainingWork = vi.fn(() => Promise.resolve(1))
    const rejected = expect(f.gate.runAgent(cancelledWork, cancelled.signal)).rejects.toBe(reason)
    const remaining = f.gate.runAgent(remainingWork)
    cancelled.abort(reason)
    await rejected
    expect(cancelledWork).not.toHaveBeenCalled()
    expect(remainingWork).not.toHaveBeenCalled()
    f.gate.transition('agent')
    await expect(remaining).resolves.toBe(1)
  })

  it('rejects nested steps instead of deadlocking control handover', async () => {
    const f = fixture()
    await f.gate.runAgent(async () => {
      await expect(f.gate.runAgent(() => Promise.resolve())).rejects.toThrow(/cannot be nested/)
    })
    await f.gate.runViewer(async () => {
      await expect(f.gate.runAgent(() => Promise.resolve())).rejects.toThrow(/cannot be nested/)
    })
    await f.gate.drain(new AbortController().signal)
  })
})
