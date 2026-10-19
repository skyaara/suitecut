// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { type NativeWorkerControl } from '../src/native-worker-protocol.js'
import { NativeWorker, type NativeWorkerOwner } from '../src/native-worker.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture(debuggerTransport = true, type: 'worker' | 'shared_worker' = 'worker') {
  const lifetime = new AbortController()
  const calls: Record<string, UntrustedInput>[] = []
  const control: NativeWorkerControl = { owner: 'native-source', generation: '7' }
  const send = (params: Record<string, UntrustedInput>) => {
    calls.push(params)
    return Promise.resolve(params.action === 'open' ? { session: 'native-session' } : {})
  }
  const owner: NativeWorkerOwner = {
    source: { signal: lifetime.signal, pageSignal: lifetime.signal } as NativeWorkerOwner['source'],
    runWorker: () => {
      throw new Error('No worker/root fallback allowed')
    },
    ...(debuggerTransport ? { runDebugger: send } : {}),
    defaultTimeout: () => 900,
    isPageHandle: () => false,
    createConsole: () => {
      throw new Error('No console needed')
    },
  }
  return {
    calls,
    lifetime,
    control,
    worker: new NativeWorker(owner, {
      id: 'actual-worker',
      url: 'http://native/worker.js',
      type,
      control,
    }),
  }
}

describe('dedicated worker debugger transport', () => {
  it('uses exact native worker control and original budget, with truthful capabilities', async () => {
    const f = fixture()
    const session = await f.worker.createDebuggerSession({ timeout: 321 })
    expect(f.calls).toEqual([
      { action: 'open', timeoutMs: 321, worker: 'actual-worker', workerControl: f.control },
    ])
    expect(session.capabilities).toEqual({
      pause: false,
      step: false,
      observedPause: true,
      resume: true,
      scriptInventory: true,
      scriptSource: true,
      breakpoints: true,
      pausedEvaluation: true,
    })
    await expect(session.pause()).rejects.toThrow(/unsupported/u)
    expect(f.calls).toHaveLength(1)
    await session.close()
    expect(f.calls.at(-1)).toMatchObject({
      action: 'close',
      session: 'native-session',
      worker: 'actual-worker',
      workerControl: f.control,
    })
  })
  it('does not route broker or shared worker debugger calls to a Source page', () => {
    for (const f of [fixture(false), fixture(true, 'shared_worker')]) {
      expect(() => f.worker.createDebuggerSession()).toThrow(/Source-owned dedicated/u)
      expect(f.calls).toEqual([])
    }
  })
  it('retains old cleanup control and rejects normal calls after a real reattach event', async () => {
    const f = fixture()
    const session = await f.worker.createDebuggerSession()
    f.worker.channelClosedFromNative('Actual Inspector channel detached', 1)
    f.worker.readyFromNative(2, { owner: 'native-source', generation: '8' })
    await expect(session.scripts()).rejects.toThrow()
    for (let i = 0; i < 8; ++i) await Promise.resolve()
    expect(f.calls.filter((row) => row.action === 'close')).toEqual([
      {
        action: 'close',
        session: 'native-session',
        timeoutMs: 0,
        worker: 'actual-worker',
        workerControl: { owner: 'native-source', generation: '7' },
      },
    ])
  })
  it('rejects closed workers and malformed operation options before transport', () => {
    const f = fixture()
    f.worker.closeFromNative()
    expect(() => f.worker.createDebuggerSession()).toThrow(/closed/u)
    expect(f.calls).toHaveLength(0)
  })
})
