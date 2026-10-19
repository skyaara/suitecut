// SPDX-License-Identifier: MIT
// Controlled native receipt transport; no native scheduler or Source claim.
import { describe, expect, it } from 'vitest'

import {
  NativeCallbackAssertions,
  NativeToPassOptionsSchema,
} from '../src/native-callback-assertions.js'
import {
  currentNativeRetryInvocation,
  nativeRetryInfrastructureFailure,
  nativeRetryInvocation,
  nativeRetryOperationSignal,
  nativeRetryRequestInvocation,
  observeNativeRetrySource,
  retainNativeRetryOperation,
  runNativeRetryCleanup,
  runNativeRetryOperation,
} from '../src/native-callback-operations.js'
import { NativeCallbackRetryCallbacks } from '../src/native-runner-callback-retries.js'
import { NativeStepCallbacks } from '../src/native-runner-steps.js'
import { runNativeMatcherUserCallback } from '../src/native-soft-assertions.js'
import { type UntrustedInput } from '../src/untrusted.js'

const owner = { token: '0', scope: '4', generation: '7' }
const encode = (value: string) => Buffer.from(value, 'utf16le').toString('hex') || '-'
const deferred = <T>() => {
  let resolve!: (value: T) => void
  let reject!: (value: UntrustedInput) => void
  const promise = new Promise<T>((a, b) => {
    resolve = a
    reject = b
  })
  return { promise, resolve, reject }
}
const tick = async () => {
  await Promise.resolve()
  await Promise.resolve()
  await Promise.resolve()
}
function fixture() {
  const sent: (string | number)[][] = [],
    fatal: Error[] = [],
    signals = new Map<string, AbortController>(),
    retired: string[] = []
  const callbacks = new NativeCallbackRetryCallbacks(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    (error) => fatal.push(error),
    () => undefined,
    (error) =>
      error instanceof Error
        ? error.message
        : typeof error === 'string'
          ? error
          : 'actual raw non-Error',
    {
      current: () => '0',
      runAdmitted: (_owner, id, _parent, _active, callback) => {
        const abort = new AbortController()
        signals.set(id, abort)
        return callback(abort.signal)
      },
      closeAdmitted: () => undefined,
      abortAdmitted: (_owner, id, error) => signals.get(id)?.abort(error),
      retireAdmitted: (_owner, id) => {
        retired.push(id)
      },
      originating: () => undefined,
    },
    { workerIndex: 1, pid: 99 },
  )
  const packet = (kind: string, ...fields: string[]) =>
    callbacks.packet([kind, '1', '0', '4', '7', '11', ...fields])
  callbacks.packet(['TOPASS_CONTEXT', '0', '4', '7', '2', '3', '1', '99', 'body'])
  return {
    callbacks,
    sent,
    fatal,
    signals,
    retired,
    packet,
    begin: (callback: () => UntrustedInput | void) =>
      callbacks.run(
        owner,
        () => true,
        () => callbacks.begin(callback, {}),
      ),
    run: (nonce = '1', step = '11') => packet('TOPASS_RUN', nonce, step, '0', '100'),
  }
}

describe('native callback retry author bridge', () => {
  it('only invokes after exact native admission and never chooses another retry', async () => {
    const f = fixture()
    let calls = 0
    const result = f.begin(() => {
      ++calls
      throw new Error('ordinary')
    })
    void result.catch(() => undefined)
    expect(calls).toBe(0)
    expect(f.sent[0]?.slice(0, 8)).toEqual(['TOPASS_BEGIN', '1', '0', '4', '7', '0', '-', '-'])
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    await tick()
    expect(calls).toBe(1)
    expect(f.sent.at(-1)?.slice(0, 9)).toEqual([
      'TOPASS_RAW',
      '1',
      '0',
      '4',
      '7',
      '11',
      '1',
      '11',
      'E',
    ])
    await tick()
    expect(calls).toBe(1)
    f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
    f.run('2', '12')
    expect(calls).toBe(2)
    f.packet('TOPASS_ITERATION_RETIRED', '2', '12')
    f.packet('TOPASS_RESULT', 'timedOut', encode('exhausted'), '2')
    await expect(result).rejects.toThrow('ordinary')
    f.packet('TOPASS_RETIRED')
    expect(f.callbacks.pending).toBe(0)
  })
  it.each([undefined, 17, 'authored value', new Error('identity')])(
    'preserves actual last thrown value %s selected by native exhaustion',
    async (error) => {
      const f = fixture(),
        result = f.begin(() => {
          // eslint-disable-next-line @typescript-eslint/only-throw-error -- Verify that arbitrary authored thrown values retain their identity.
          throw error
        })
      void result.catch(() => undefined)
      f.packet('TOPASS_ADMITTED', '1', '-')
      f.run()
      f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
      f.packet('TOPASS_RESULT', 'timedOut', encode('native cause'), '1')
      await expect(result).rejects.toBe(error)
      f.packet('TOPASS_RETIRED')
    },
  )
  it('native logical timeout aborts only private signal while retaining raw callback and observer', async () => {
    const f = fixture(),
      raw = deferred<void>()
    let detach = 0
    let actual: ReturnType<typeof nativeRetryInvocation>
    const result = f.begin(() => {
      actual = nativeRetryInvocation()
      observeNativeRetrySource({}, '8', () => () => {
        ++detach
      })
      return raw.promise
    })
    void result.catch(() => undefined)
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    f.packet('TOPASS_RESULT', 'timedOut', encode('deadline'), '0')
    let settled = false
    void result.then(
      () => {
        settled = true
      },
      () => {
        settled = true
      },
    )
    await tick()
    expect(settled).toBe(false)
    expect(actual?.accepting).toBe(false)
    expect(actual?.signal.aborted).toBe(true)
    expect(detach).toBe(0)
    expect(f.callbacks.pending).toBe(1)
    expect(() => f.packet('TOPASS_ITERATION_RETIRED', '1', '11')).toThrow('raw')
    raw.resolve()
    await tick()
    expect(detach).toBe(0)
    f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
    expect(detach).toBe(1)
    f.packet('TOPASS_RETIRED')
    await expect(result).rejects.toThrow('deadline')
  })
  it('retains a real producer failure after author raw pass until native retirement', async () => {
    const f = fixture()
    let fail!: (error: Error) => void,
      detach = 0
    const result = f.begin(() => {
      observeNativeRetrySource({}, '8', (listener) => {
        fail = listener
        return () => {
          ++detach
        }
      })
    })
    void result.catch(() => undefined)
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    await tick()
    const error = new Error('real transport receipt')
    fail(error)
    expect(f.sent.at(-1)?.[0]).toBe('TOPASS_PRODUCER_FAILURE')
    expect(detach).toBe(0)
    f.packet('TOPASS_RESULT', 'infrastructure', encode('native veto'), '1')
    f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
    f.packet('TOPASS_RETIRED')
    await expect(result).rejects.toBe(error)
    expect(detach).toBe(1)
  })
  it('does not trust a genuine Promise own early then', async () => {
    const f = fixture(),
      raw = deferred<void>()
    void Object.defineProperty(raw.promise, 'then', { value: (resolve: () => void) => resolve() })
    const result = f.begin(() => raw.promise)
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    await tick()
    expect(f.sent.some((row) => row[0] === 'TOPASS_RAW')).toBe(false)
    raw.resolve()
    await tick()
    expect(f.sent.filter((row) => row[0] === 'TOPASS_RAW')).toHaveLength(1)
    f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
    f.packet('TOPASS_RESULT', 'passed', '-', '1')
    await result
    f.packet('TOPASS_RETIRED')
  })
  it('reports actual pending operation separately from raw and preserves cleanup after private abort', async () => {
    const f = fixture(),
      stage = deferred<void>(),
      cleanup = deferred<void>()
    let operation!: Promise<void>, cleanupSignal: AbortSignal | undefined
    const result = f.begin(() => {
      operation = runNativeRetryOperation(async () => {
        await stage.promise
        expect(nativeRetryRequestInvocation()).toBeDefined()
        const task = runNativeRetryCleanup(async () => {
          cleanupSignal = nativeRetryOperationSignal(undefined, nativeRetryInvocation()!)
          await cleanup.promise
        })
        retainNativeRetryOperation(task)
        return task
      })
      void operation.catch(() => undefined)
    })
    void result.catch(() => undefined)
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    await tick()
    expect(f.sent.at(-1)?.at(-1)).toBe(1)
    f.packet('TOPASS_RESULT', 'timedOut', encode('deadline'), '0')
    let publiclySettled = false
    void result.then(
      () => {
        publiclySettled = true
      },
      () => {
        publiclySettled = true
      },
    )
    await tick()
    expect(publiclySettled).toBe(false)
    stage.resolve()
    await tick()
    expect(cleanupSignal).toBeUndefined()
    expect(f.sent.some((row) => row[0] === 'TOPASS_OPERATIONS_RETIRED')).toBe(false)
    cleanup.resolve()
    await operation
    await tick()
    expect(f.sent.filter((row) => row[0] === 'TOPASS_OPERATIONS_RETIRED')).toHaveLength(1)
    f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
    f.packet('TOPASS_RETIRED')
    await expect(result).rejects.toThrow('deadline')
  })
  it('does not leak an internal operation token into an authored callback after rawclose', async () => {
    const f = fixture(),
      stage = deferred<void>()
    let late!: Promise<void>
    const result = f.begin(() => {
      late = runNativeRetryOperation(async () => {
        await stage.promise
        runNativeMatcherUserCallback(() => currentNativeRetryInvocation(owner))
      })
      void late.catch(() => undefined)
    })
    void result.catch(() => undefined)
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    await tick()
    stage.resolve()
    await expect(late).rejects.toThrow('no longer admits')
    await tick()
    expect(f.sent.some((row) => row[0] === 'TOPASS_PRODUCER_FAILURE')).toBe(true)
    f.packet('TOPASS_RESULT', 'infrastructure', encode('stale'), '1')
    f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
    f.packet('TOPASS_RETIRED')
    await expect(result).rejects.toThrow('no longer admits')
  })
  it('rejects stale/foreign native runs without invoking callback', () => {
    const f = fixture()
    let calls = 0
    void f
      .begin(() => {
        ++calls
      })
      .catch(() => undefined)
    expect(() =>
      f.callbacks.packet(['TOPASS_ADMITTED', '1', '0', '4', '9', '11', '1', '-']),
    ).toThrow('foreign')
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    expect(() => f.run()).toThrow('overlaps')
    expect(calls).toBe(1)
  })
  it('constructor retains closure only; unsupported context/options never invokes it', async () => {
    let calls = 0
    const assertion = new NativeCallbackAssertions(() => {
      ++calls
    })
    expect(calls).toBe(0)
    await expect(assertion.toPass()).rejects.toThrow('actual native Runner')
    expect(() => assertion.not).toThrow('does not support')
    for (const options of [
      { intervals: [] },
      { timeout: -1 },
      { timeout: Infinity },
      { timeout: 1.2 },
      { intervals: [NaN] },
      { signal: new AbortController().signal },
    ])
      expect(() => NativeToPassOptionsSchema.parse(options)).toThrow()
    expect(
      NativeToPassOptionsSchema.parse({ timeout: 0, intervals: [Number.MAX_SAFE_INTEGER] }),
    ).toEqual({ timeout: 0, intervals: [Number.MAX_SAFE_INTEGER] })
    expect(calls).toBe(0)
  })
  it('private producer latch survives caught failure and ordinary raw return', async () => {
    const f = fixture(),
      error = new Error('authentic producer')
    const result = f.begin(() => {
      nativeRetryInfrastructureFailure(error)
    })
    void result.catch(() => undefined)
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    await tick()
    expect(f.sent.at(-1)?.[8]).toBe('I')
    f.packet('TOPASS_RESULT', 'infrastructure', encode('veto'), '1')
    await expect(result).rejects.toBe(error)
    f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
    f.packet('TOPASS_RETIRED')
  })
  it('uses native managed step ingress without fabricating normal STEP_BEGIN or raw settlement', () => {
    const sent: (string | number)[][] = []
    const steps = new NativeStepCallbacks(
      (...fields) => {
        sent.push(fields)
        return Promise.resolve()
      },
      (error) => {
        throw error
      },
      () => undefined,
    )
    let signal!: AbortSignal
    steps.runAdmitted(
      owner,
      '11',
      '0',
      () => true,
      (actual) => {
        signal = actual
        expect(steps.current(owner)).toBe('11')
      },
    )
    expect(sent).toEqual([])
    expect(steps.pending).toBe(1)
    steps.abortAdmitted(owner, '11', new Error('native assertion deadline'))
    expect(signal.aborted).toBe(true)
    expect(() => steps.retireAdmitted(owner, '11')).toThrow('physical')
    steps.closeAdmitted(owner, '11')
    steps.handle(['STEP_RECEIVED', '11', '0', '4', '7'])
    steps.retireAdmitted(owner, '11')
    expect(steps.pending).toBe(0)
    expect(sent).toEqual([])
  })
  it('retains an early genuine context-retired receipt until actual outer callback release', () => {
    const f = fixture()
    let released = 0
    f.callbacks.packet(['TOPASS_CONTEXT_RETIRED', '0', '4', '7'])
    expect(f.callbacks.idleReady).toBe(false)
    f.callbacks.retireCallback(owner, () => {
      ++released
    })
    expect(released).toBe(1)
    expect(f.callbacks.idleReady).toBe(true)
    expect(() => f.callbacks.packet(['TOPASS_CONTEXT_RETIRED', '0', '4', '7'])).toThrow('physical')
  })

  it('keeps authored thenable assimilation in the actual iteration context', async () => {
    const f = fixture()
    let actual: ReturnType<typeof nativeRetryInvocation>
    const result = f.begin(() => ({
      then(resolve: () => void) {
        actual = nativeRetryInvocation()
        resolve()
      },
    }))
    f.packet('TOPASS_ADMITTED', '1', '-')
    f.run()
    await tick()
    expect(actual?.assertionID).toBe('11')
    expect(actual?.accepting).toBe(false)
    f.packet('TOPASS_ITERATION_RETIRED', '1', '11')
    f.packet('TOPASS_RESULT', 'passed', '-', '1')
    await result
    f.packet('TOPASS_RETIRED')
  })
})
