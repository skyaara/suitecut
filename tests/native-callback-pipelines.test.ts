// SPDX-License-Identifier: MIT
// Controlled producer boundaries, not an actual Source/physical-fence proof.
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { describe, expect, it } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import {
  nativeRetryInvocation,
  nativeRetryIsCleanup,
  nativeRetryOperationSignal,
  nativeRetryRequestInvocation,
  runNativeRetryInvocation,
  runNativeRetryOperation,
  type NativeRetryInvocation,
} from '../src/native-callback-operations.js'
import {
  waitNativeEventPredicate,
  type NativeEventPredicateSnapshot,
} from '../src/native-event-predicate.js'
import { type NativeHTTPLookupEvent } from '../src/native-http-lookup-protocol.js'
import { NativeHTTPLookups } from '../src/native-http-lookup.js'
import { NativeAPIRequestContext } from '../src/native-http.js'
import { nativeInitScriptSource } from '../src/native-init-script.js'
import { NativePage } from '../src/native-page.js'
import { runNativeMatcherUserPromise } from '../src/native-soft-assertions.js'
import {
  captureNativeStorageState,
  restoreNativeStorageState,
} from '../src/native-storage-state.js'
import {
  runNativeURLPredicate,
  type NativeURLPredicateSnapshot,
} from '../src/native-url-predicate.js'
import { type UntrustedInput } from '../src/untrusted.js'

function deferred<T>() {
  let resolve!: (value: T) => void, reject!: (error: UntrustedInput) => void
  const promise = new Promise<T>((a, b) => {
    resolve = a
    reject = b
  })
  return { promise, resolve, reject }
}
async function tick() {
  for (let i = 0; i < 12; ++i) await Promise.resolve()
}
function invocation(): NativeRetryInvocation {
  return {
    owner: { token: '0', scope: '1', generation: '1' },
    assertionID: '11',
    invocationNonce: '1',
    step: '11',
    signal: new AbortController().signal,
    timeoutMs: 100,
    accepting: true,
    retired: false,
    releases: new Map(),
    operations: new Set(),
    operationsDrained: () => undefined,
    producerFailure: () => undefined,
  }
}
function source(
  execute: (command: string, params: Record<string, UntrustedInput>) => Promise<UntrustedInput>,
): NativeBrowser {
  const signal = new AbortController().signal
  return {
    state: 'running',
    url: 'about:blank',
    signal,
    processSignal: signal,
    width: 1,
    height: 1,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'controlled',
    closed: Promise.resolve(),
    failure: new Promise<never>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: (command, params = {}) => execute(command, params),
    runAutomation: (command, params = {}) => execute(command, params),
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
  }
}
const response = {
  apiContextId: 7,
  responseId: '9',
  url: 'https://example.test/',
  status: 200,
  statusText: 'OK',
  headers: [],
}
function lateAdmission(errors: UntrustedInput[]) {
  void Promise.resolve().then(() => {
    try {
      nativeRetryRequestInvocation()
    } catch (error) {
      errors.push(error as UntrustedInput)
    }
  })
}

describe('native retry producer boundary ownership', () => {
  it.each(['get', 'head', 'post', 'put', 'patch', 'delete', 'fetch'] as const)(
    'admits HTTP %s before asynchronous multipart preparation and preserves that real pipeline',
    async (method) => {
      const preparation = deferred<ArrayBuffer>(),
        transport = deferred<UntrustedInput>(),
        actual = invocation()
      let commands = 0
      const form = new FormData()
      form.set('file', new Blob(['x']), 'x.bin')
      const file = form.get('file')
      if (!file || typeof file === 'string') throw new Error('Missing actual form File')
      file.arrayBuffer = () => preparation.promise
      const api = new NativeAPIRequestContext(
        source((_command, params) => {
          if (params.action === 'create')
            return Promise.resolve({ apiContextId: 7, browserContextId: 3 })
          ++commands
          return transport.promise
        }),
      )
      const pending = runNativeRetryInvocation(actual, () =>
        api[method]('https://example.test/', { multipart: form }),
      )
      expect(actual.sourceIntent).toBe(true)
      expect(actual.operations.size).toBe(1)
      expect(commands).toBe(0)
      actual.accepting = false
      preparation.resolve(new Uint8Array([1]).buffer)
      await tick()
      expect(commands).toBe(1)
      expect(actual.operations.size).toBe(1)
      transport.resolve(response)
      await pending
      await tick()
      expect(actual.operations.size).toBe(0)
    },
  )
  it.each(['data', 'form', 'params'] as const)(
    'does not lend HTTP %s conversion hooks a live operation token',
    async (kind) => {
      const held = deferred<UntrustedInput>(),
        actual = invocation(),
        errors: UntrustedInput[] = []
      const api = new NativeAPIRequestContext(
        source((_command, params) =>
          params.action === 'create'
            ? Promise.resolve({ apiContextId: 7, browserContextId: 3 })
            : held.promise,
        ),
      )
      const value =
        kind === 'data'
          ? {
              toJSON() {
                lateAdmission(errors)
                return { real: 1 }
              },
            }
          : kind === 'form'
            ? new (class extends URLSearchParams {
                override toString() {
                  lateAdmission(errors)
                  return super.toString()
                }
              })('a=1')
            : new (class extends URLSearchParams {
                override [Symbol.iterator]() {
                  lateAdmission(errors)
                  return super[Symbol.iterator]()
                }
              })('a=1')
      const pending = runNativeRetryInvocation(actual, () =>
        api.fetch('https://example.test/', { [kind]: value }),
      )
      actual.accepting = false
      await tick()
      expect(errors).toHaveLength(1)
      expect(errors[0]).toBeInstanceOf(Error)
      expect(actual.operations.size).toBe(1)
      held.resolve(response)
      await pending
      await tick()
      expect(actual.operations.size).toBe(0)
    },
  )
  it.each(['address', 'failure'] as const)(
    'suspends lookup authored %s conversion without losing the admitted answer',
    async (kind) => {
      const actual = invocation(),
        held = deferred<void>(),
        errors: UntrustedInput[] = []
      let receive!: (event: NativeHTTPLookupEvent) => void,
        answers = 0
      const lifetime = new AbortController(),
        lookups = NativeHTTPLookups.forTransport({
          signal: lifetime.signal,
          onHTTPLookupMessage: (listener) => {
            receive = listener
            return () => undefined
          },
          resolveLookup: () => {
            ++answers
            return Promise.resolve({})
          },
        })
      const pending = runNativeRetryInvocation(actual, () =>
        runNativeRetryOperation(() =>
          lookups.withLookup(
            () => {
              if (kind === 'failure') {
                // eslint-disable-next-line @typescript-eslint/only-throw-error
                throw {
                  toString() {
                    lateAdmission(errors)
                    return 'authored failure'
                  },
                }
              }
              return [
                {
                  get address() {
                    lateAdmission(errors)
                    return '127.0.0.1'
                  },
                  family: 4,
                },
              ]
            },
            async (key) => {
              receive({
                event: 'request',
                owner: 1,
                generation: 1,
                apiContextId: 7,
                lookupKey: key,
                hostname: 'example.test',
                port: 443,
              })
              await held.promise
            },
          ),
        ),
      )
      actual.accepting = false
      await tick()
      expect(errors).toHaveLength(1)
      expect(answers).toBe(1)
      held.resolve()
      await pending
      lifetime.abort()
      await tick()
      expect(actual.operations.size).toBe(0)
    },
  )
  it('suspends init-script function and argument hooks while preserving real preparation', async () => {
    const actual = invocation(),
      errors: UntrustedInput[] = [],
      held = deferred<void>()
    const script = () => undefined
    script.toString = () => {
      lateAdmission(errors)
      return '()=>undefined'
    }
    const pending = runNativeRetryInvocation(actual, () =>
      runNativeRetryOperation(async () => {
        await nativeInitScriptSource(script, {
          get value() {
            lateAdmission(errors)
            return 1
          },
        })
        await held.promise
      }),
    )
    actual.accepting = false
    await tick()
    expect(errors).toHaveLength(2)
    expect(actual.operations.size).toBe(1)
    held.resolve()
    await pending
  })
  it.each(['addInitScript', 'addScriptTag', 'addStyleTag'] as const)(
    'admits Page %s file preparation before its asynchronous read and suspends input getters',
    async (method) => {
      const actual = invocation(),
        errors: UntrustedInput[] = [],
        page = new NativePage(source(() => Promise.resolve({})))
      const pending = runNativeRetryInvocation(actual, () =>
        page[method]({
          get path() {
            lateAdmission(errors)
            return '/suitecut-controlled-file-that-does-not-exist'
          },
        }),
      )
      void pending.catch(() => undefined)
      expect(actual.operations.size).toBe(1)
      expect(actual.sourceIntent).toBe(true)
      actual.accepting = false
      await tick()
      expect(errors.length).toBeGreaterThan(0)
      await expect(pending).rejects.toMatchObject({ code: 'ENOENT' })
      await tick()
      expect(actual.operations.size).toBe(0)
    },
  )
  it('retains shared storage capture through actual post-command filesystem publication', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-retry-storage-')),
      path = join(directory, 'state.json'),
      actual = invocation()
    try {
      const pending = runNativeRetryInvocation(actual, () =>
        captureNativeStorageState(
          { runContext: () => Promise.resolve({ cookies: [], origins: [] }) },
          7,
          { path },
        ),
      )
      actual.accepting = false
      await tick()
      expect(actual.sourceIntent).toBe(true)
      expect(actual.operations.size).toBe(1)
      await pending
      await tick()
      expect(actual.operations.size).toBe(0)
      expect(JSON.parse(await readFile(path, 'utf8'))).toEqual({ cookies: [], origins: [] })
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })
  it('allows only the real admitted storage-discard continuation after raw close', async () => {
    const abort = new AbortController(),
      actual = { ...invocation(), signal: abort.signal },
      append = deferred<void>()
    let discarded = false
    const pending = runNativeRetryInvocation(actual, () =>
      runNativeRetryOperation(() =>
        restoreNativeStorageState(
          {
            runContext: async (action) => {
              if (action === 'storageUploadBegin') return { uploadId: '1' }
              if (action === 'storageUploadAppend') {
                await append.promise
                throw new Error('ordinary append failure')
              }
              if (action === 'storageUploadDiscard') {
                expect(nativeRetryRequestInvocation()).toBe(actual)
                expect(nativeRetryIsCleanup(actual)).toBe(true)
                expect(nativeRetryOperationSignal(undefined, actual)).toBeUndefined()
                discarded = true
              }
              return {}
            },
          },
          7,
          { cookies: [], origins: [] },
        ),
      ),
    )
    void pending.catch(() => undefined)
    await tick()
    actual.accepting = false
    abort.abort(new Error('native assertion cap'))
    append.resolve()
    await expect(pending).rejects.toThrow('ordinary append failure')
    expect(discarded).toBe(true)
    await tick()
    expect(actual.operations.size).toBe(0)
  })
  it('keeps authored thenable getter and async assimilation outside the continuation token', async () => {
    const actual = invocation(),
      errors: UntrustedInput[] = [],
      gate = deferred<void>()
    const thenable = {
      get then() {
        lateAdmission(errors)
        return (resolve: (value: boolean) => void) => {
          lateAdmission(errors)
          void gate.promise.then(() => resolve(true))
        }
      },
    }
    const pending = runNativeRetryInvocation(actual, () =>
      runNativeRetryOperation(() => runNativeMatcherUserPromise(() => thenable)),
    )
    actual.accepting = false
    await tick()
    expect(errors).toHaveLength(2)
    expect(actual.operations.size).toBe(1)
    gate.resolve()
    await expect(pending).resolves.toBe(true)
    await tick()
    expect(actual.operations.size).toBe(0)
  })
  it('ignores a genuine returned Promise own early then and retains its actual settlement', async () => {
    const actual = invocation(),
      raw = deferred<boolean>()
    let ownThen = 0
    void Object.defineProperty(raw.promise, 'then', {
      get() {
        ++ownThen
        return (resolve: (value: boolean) => void) => resolve(true)
      },
    })
    const pending = runNativeRetryInvocation(actual, () =>
      runNativeRetryOperation(() => runNativeMatcherUserPromise(() => raw.promise)),
    )
    actual.accepting = false
    await tick()
    expect(ownThen).toBe(0)
    expect(actual.operations.size).toBe(1)
    raw.resolve(false)
    await expect(pending).resolves.toBe(false)
    await tick()
    expect(actual.operations.size).toBe(0)
  })
  it.each(['url', 'event'] as const)(
    'retains actual %s predicate raw work after logical cancellation, with user scope but no internal token',
    async (kind) => {
      const actual = invocation(),
        native = deferred<UntrustedInput>(),
        raw = deferred<boolean>(),
        errors: UntrustedInput[] = []
      let emit!: () => void, seen: NativeRetryInvocation | undefined
      const browser = source((_command, params) => {
        if (typeof params.clientToken !== 'string')
          throw new Error('Missing actual candidate token')
        const clientToken = params.clientToken
        emit = () => {
          if (kind === 'url')
            url?.({ id: 1, generation: 1, token: 1, clientToken, url: 'https://example.test/' })
          else
            event?.({
              id: 1,
              generation: 1,
              token: 1,
              clientToken,
              event: 'console',
              sequence: 1,
              value: 'value',
            })
        }
        return native.promise
      })
      let url: ((value: NativeURLPredicateSnapshot) => void) | undefined,
        event: ((value: NativeEventPredicateSnapshot) => void) | undefined
      browser.onURLPredicate = (listener) => {
        url = listener
        return () => {
          url = undefined
        }
      }
      browser.onEventPredicate = (listener) => {
        event = listener
        return () => {
          event = undefined
        }
      }
      const callback = () => {
        seen = nativeRetryInvocation()
        lateAdmission(errors)
        return raw.promise
      }
      const pending = runNativeRetryInvocation(actual, () =>
        kind === 'url'
          ? runNativeURLPredicate(browser, callback)
          : waitNativeEventPredicate(browser, 'console', (value) => value, callback),
      )
      void pending.catch(() => undefined)
      // Real decoder delivery is outside the author's original ALS.
      emit()
      actual.accepting = false
      await tick()
      expect(seen).toBe(actual)
      expect(errors).toHaveLength(1)
      native.reject(new Error('controlled native logical cancellation'))
      await expect(pending).rejects.toThrow('logical cancellation')
      await tick()
      expect(actual.operations.size).toBe(1)
      raw.resolve(false)
      await tick()
      expect(actual.operations.size).toBe(0)
    },
  )
})
