// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import {
  nativeBrowserOperationRequest,
  runNativeBrowserCallback,
  type NativeBrowserCallbackScope,
} from '../src/native-browser-operations.js'
import {
  bindNativeRetryContinuation,
  retainNativeRetryOperation,
  runNativeRetryCleanup,
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from '../src/native-callback-operations.js'

function deferred<T>() {
  let resolve!: (value: T) => void
  const promise = new Promise<T>((accept) => {
    resolve = accept
  })
  return { promise, resolve }
}
function fixture() {
  const abort = new AbortController()
  const transport = {}
  const begin = vi.fn(() => Promise.resolve(91))
  const end = vi.fn(() => Promise.resolve())
  const drained = vi.fn()
  const failed = vi.fn()
  const scope: NativeBrowserCallbackScope = {
    accepting: true,
    retired: false,
    signal: abort.signal,
    transport,
    origin: { page: 1, target: 8, generation: 3, token: 5 },
    operations: new Set(),
    begin,
    end,
    drained,
    failed,
  }
  return { scope, abort, transport, begin, end, drained, failed }
}
const turn = () =>
  new Promise<void>((resolve) => {
    setImmediate(resolve)
  })
describe('native browser callback operation continuations', () => {
  it('preserves the original Promise without a callback scope or native requests', () => {
    const actual = Promise.resolve(3)
    expect(runNativeRetryOperation(() => actual)).toBe(actual)
  })
  it('uses one native token for a nested built-in lifetime and preserves its Promise', async () => {
    const f = fixture(),
      actual = deferred<number>()
    const returned = runNativeBrowserCallback(f.scope, () =>
      runNativeRetryOperation(() => runNativeRetryOperation(() => actual.promise)),
    )
    expect(returned).toBe(actual.promise)
    expect(f.begin).toHaveBeenCalledTimes(1)
    expect(f.scope.operations.size).toBe(1)
    actual.resolve(3)
    await returned
    await turn()
    expect(f.end).toHaveBeenCalledExactlyOnceWith(91)
    expect(f.scope.operations.size).toBe(0)
    expect(f.scope.retired).toBe(false)
  })
  it('retains synchronous completion until real Begin can enqueue End before the front-drained receipt', async () => {
    const f = fixture(),
      admitted = deferred<number>(),
      endACK = deferred<void>()
    f.begin.mockImplementation(() => admitted.promise)
    f.end.mockImplementation(() => endACK.promise)
    expect(runNativeBrowserCallback(f.scope, () => runNativeRetryOperation(() => 7))).toBe(7)
    f.scope.accepting = false
    expect(f.scope.operations.size).toBe(1)
    expect(f.end).not.toHaveBeenCalled()
    admitted.resolve(92)
    await turn()
    expect(f.end).toHaveBeenCalledExactlyOnceWith(92)
    expect(f.scope.operations.size).toBe(0)
    expect(f.drained).toHaveBeenCalledOnce()
    expect(f.scope.retired).toBe(false) // No native physical receipt was supplied.
    endACK.resolve()
    await turn()
  })
  it('retains actual preadmitted tasks and continuations after raw author admission closes', async () => {
    const f = fixture(),
      root = deferred<void>(),
      task = deferred<void>()
    let continuation!: () => Promise<number>
    void runNativeBrowserCallback(f.scope, () =>
      runNativeRetryOperation(() => {
        retainNativeRetryOperation(task.promise)
        continuation = bindNativeRetryContinuation(async () => {
          const actual = nativeBrowserOperationRequest(f.transport)!
          expect(actual.origin).toEqual(f.scope.origin)
          return await actual.token
        })
        return root.promise
      }),
    )
    root.resolve()
    await turn()
    f.scope.accepting = false
    expect(await continuation()).toBe(91)
    expect(f.begin).toHaveBeenCalledTimes(1)
    expect(f.end).not.toHaveBeenCalled()
    task.resolve()
    await turn()
    expect(f.end).toHaveBeenCalledOnce()
    expect(f.drained).toHaveBeenCalledOnce()
  })
  it('suspends the internal waiver across authored async descendants while genuine work remains live', async () => {
    const f = fixture(),
      root = deferred<void>(),
      authored = deferred<void>()
    let late!: () => void
    void runNativeBrowserCallback(f.scope, () =>
      runNativeRetryOperation(() => {
        withoutNativeRetryOperation(() => {
          late = bindNativeRetryContinuation(() => {
            expect(() => runNativeRetryOperation(() => 4)).toThrow('no longer admits')
            authored.resolve()
          })
        })
        return root.promise
      }),
    )
    f.scope.accepting = false
    runNativeBrowserCallback(f.scope, late)
    await authored.promise
    expect(f.begin).toHaveBeenCalledTimes(1)
    root.resolve()
    await turn()
  })
  it('allows only an admitted built-in cleanup to bypass the private cancellation signal', async () => {
    const f = fixture(),
      root = deferred<void>()
    let continuation!: () => void
    void runNativeBrowserCallback(f.scope, () =>
      runNativeRetryOperation(() => {
        continuation = bindNativeRetryContinuation(() => {
          expect(nativeBrowserOperationRequest(f.transport)?.signal?.aborted).toBe(true)
          expect(
            runNativeRetryCleanup(() => nativeBrowserOperationRequest(f.transport)),
          ).toBeUndefined()
        })
        return root.promise
      }),
    )
    f.scope.accepting = false
    f.abort.abort(new Error('actual native abort'))
    continuation()
    root.resolve()
    await turn()
    expect(f.begin).toHaveBeenCalledTimes(1)
  })
  it('rejects a foreign process while retaining the triggering-page identity in same-process requests', async () => {
    const f = fixture()
    runNativeBrowserCallback(f.scope, () =>
      runNativeRetryOperation(() => {
        expect(nativeBrowserOperationRequest(f.transport)?.origin.page).toBe(1)
        expect(() => nativeBrowserOperationRequest({})).toThrow('another browser process')
      }),
    )
    await turn()
  })
  it('observes a genuine Promise intrinsically despite its own early then property', async () => {
    const f = fixture(),
      actual = deferred<void>()
    void Object.defineProperty(actual.promise, 'then', {
      value: (resolve: (value: void) => void) => {
        resolve()
      },
    })
    void runNativeBrowserCallback(f.scope, () => runNativeRetryOperation(() => actual.promise))
    await turn()
    expect(f.end).not.toHaveBeenCalled()
    actual.resolve()
    await turn()
    expect(f.end).toHaveBeenCalledOnce()
  })
  it('preserves a synchronous author exception and closes the real admitted operation', async () => {
    const f = fixture(),
      original = new Error('actual')
    expect(() =>
      runNativeBrowserCallback(f.scope, () =>
        runNativeRetryOperation(() => {
          throw original
        }),
      ),
    ).toThrow(original)
    await turn()
    expect(f.end).toHaveBeenCalledOnce()
    expect(f.failed).not.toHaveBeenCalled()
  })
})
