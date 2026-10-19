import { describe, expect, it } from 'vitest'

import {
  nativeRouteCallbackPhysicalCount,
  type NativeRouteCallbackTransport,
} from '../src/native-route-callback.js'
import { invokeNativeRouteURLMatcher } from '../src/native-route-url-matcher-callback.js'
import {
  NativeRouteURLMatcherPolicy,
  type NativeRouteURLMatcher,
} from '../src/native-route-url-matcher.js'

describe('actual authored route URL matcher bridge', () => {
  it('snapshots RegExp source and flags and leaves global/sticky state unchanged', () => {
    for (const flags of ['g', 'y', 'gy']) {
      const regex = new RegExp('https://actual.test/', flags)
      regex.lastIndex = 12
      const policy = new NativeRouteURLMatcherPolicy(regex)
      expect(policy.nativePattern).toBe('**/*')
      const callback = policy.callback()
      expect(callback('https://actual.test/a')).toEqual({ matched: true })
      expect(callback('https://actual.test/b')).toEqual({ matched: true })
      expect(regex.lastIndex).toBe(12)
      expect(policy.matchesRegistration(new RegExp(regex.source, flags))).toBe(true)
      expect(policy.matchesRegistration(/other/)).toBe(false)
      policy.dispose()
    }
  })
  it('passes an actual URL and returns only the author boolean without invoking route code', () => {
    const seen: URL[] = []
    const predicate = (url: URL) => {
      seen.push(url)
      return url.pathname === '/yes'
    }
    const policy = new NativeRouteURLMatcherPolicy(predicate)
    expect(policy.callback()('https://actual.test/yes?q=1')).toEqual({ matched: true })
    expect(policy.callback()('https://actual.test/no')).toEqual({ matched: false })
    expect(seen.map((url) => url.href)).toEqual([
      'https://actual.test/yes?q=1',
      'https://actual.test/no',
    ])
    expect(policy.matchesRegistration(predicate)).toBe(true)
    expect(policy.matchesRegistration(() => true)).toBe(false)
    policy.dispose()
  })
  it('preserves the exact thrown author error and rejects invalid synchronous values', () => {
    const actual = new Error('original author exception')
    const policy = new NativeRouteURLMatcherPolicy(() => {
      throw actual
    })
    expect(policy.callback()('https://actual.test/').error).toBe(actual)
    policy.dispose()
    const invalid = new NativeRouteURLMatcherPolicy((() => 1) as never)
    expect(invalid.callback()('https://actual.test/').error?.message).toContain(
      'synchronous boolean',
    )
    invalid.dispose()
  })
  it('rejects an async result logically while retaining the original running continuation', async () => {
    let release: ((value: boolean) => void) | undefined
    const pending = new Promise<boolean>((resolve) => {
      release = resolve
    })
    const policy = new NativeRouteURLMatcherPolicy((() => pending) as never)
    const result = policy.callback()('https://actual.test/')
    expect(result.error?.message).toContain('synchronous boolean')
    if (!result.continuation || !release) throw new Error('Expected actual async continuation')
    let settled = false
    void result.continuation.then(() => (settled = true))
    policy.dispose()
    await Promise.resolve()
    expect(settled).toBe(false)
    release(false)
    await expect(result.continuation).resolves.toBe(false)
    expect(settled).toBe(true)
  })
  it('reads an actual then getter once and preserves its eventual rejection', async () => {
    let gets = 0,
      calls = 0
    const actual = new Error('actual asynchronous rejection')
    const thenable = {
      get then() {
        ++gets
        return (_resolve: (value: boolean) => void, reject: (error: Error) => void) => {
          ++calls
          reject(actual)
        }
      },
    }
    const policy = new NativeRouteURLMatcherPolicy((() => thenable) as never)
    const result = policy.callback()('https://actual.test/')
    expect(result.error?.message).toContain('synchronous boolean')
    expect(gets).toBe(1)
    await expect(result.continuation).rejects.toBe(actual)
    expect(calls).toBe(1)
    expect(gets).toBe(1)
    policy.dispose()
  })

  it('observes real Promise settlement despite an author-owned lying or throwing then', async () => {
    for (const throws of [false, true]) {
      let release: ((value: boolean) => void) | undefined
      const pending = new Promise<boolean>((resolve) => {
        release = resolve
      })
      void Object.defineProperty(
        pending,
        'then',
        throws
          ? {
              get() {
                throw new Error('Author own then must not be read')
              },
            }
          : {
              value(resolve: (value: boolean) => void) {
                resolve(false)
              },
            },
      )
      const policy = new NativeRouteURLMatcherPolicy((() => pending) as never),
        process = new AbortController(),
        events: string[] = []
      const invocation = invokeNativeRouteURLMatcher(
        {
          signal: process.signal,
          processSignal: process.signal,
          start: () => Promise.resolve(),
          finish: () => {
            events.push('finish')
            return Promise.resolve()
          },
        },
        { invocation: 'actual', token: '1', scope: '1' },
        policy.callback(),
        'https://actual.test/',
        () => {
          events.push('invalid')
          return Promise.resolve()
        },
        () => {
          throw new Error('No rejection expected')
        },
      )
      const failure = expect(invocation).rejects.toThrow('synchronous boolean')
      for (let i = 0; i < 8; ++i) await Promise.resolve()
      expect(events).toEqual(['invalid'])
      expect(nativeRouteCallbackPhysicalCount(process.signal)).toBe(1)
      if (!release) throw new Error('Actual Promise resolver absent')
      release(false)
      await failure
      expect(events).toEqual(['invalid', 'finish'])
      expect(nativeRouteCallbackPhysicalCount(process.signal)).toBe(0)
      policy.dispose()
    }
  })

  it('does not invent settlement when an intrinsic observer is blocked by author species', async () => {
    const pending = new Promise<boolean>(() => undefined),
      actual = new Error('Actual author species getter failed')
    void Object.defineProperty(pending, 'constructor', {
      get() {
        throw actual
      },
    })
    const policy = new NativeRouteURLMatcherPolicy((() => pending) as never),
      result = policy.callback()('https://actual.test/')
    expect(result.error).toBe(actual)
    if (!result.continuation) throw new Error('Unobservable work must remain retained')
    let settled = false
    void result.continuation.then(
      () => {
        settled = true
      },
      () => {
        settled = true
      },
    )
    for (let i = 0; i < 8; ++i) await Promise.resolve()
    expect(settled).toBe(false)
    policy.dispose()
  })
  it('bounds live registration metadata and releases quota exactly once', () => {
    const policies: NativeRouteURLMatcherPolicy[] = []
    try {
      for (let i = 0; i < 1024; ++i) policies.push(new NativeRouteURLMatcherPolicy(() => true))
      expect(() => new NativeRouteURLMatcherPolicy(() => true)).toThrow('capacity')
    } finally {
      for (const policy of policies) {
        policy.dispose()
        policy.dispose()
      }
    }
    const policy = new NativeRouteURLMatcherPolicy('**/*')
    expect(policy.host).toBe(false)
    expect(() => policy.callback()).toThrow('unavailable')
    policy.dispose()
    expect(() => new NativeRouteURLMatcherPolicy({} as NativeRouteURLMatcher)).toThrow('requires')
  })

  it('publishes a true result only after actual start and finish acknowledgments', async () => {
    const scope = new AbortController(),
      order: string[] = []
    const transport: NativeRouteCallbackTransport = {
      signal: scope.signal,
      processSignal: scope.signal,
      start: () => {
        order.push('start')
        return Promise.resolve()
      },
      finish: () => {
        order.push('finish')
        return Promise.resolve()
      },
    }
    const policy = new NativeRouteURLMatcherPolicy(() => {
      order.push('author')
      return true
    })
    await invokeNativeRouteURLMatcher(
      transport,
      { invocation: 'actual', token: '1', scope: '1' },
      policy.callback(),
      'https://actual.test/',
      (value) => {
        expect(value).toBe(true)
        order.push('result')
        return Promise.resolve()
      },
      () => {
        throw new Error('No asynchronous callback expected')
      },
    )
    expect(order).toEqual(['start', 'author', 'finish', 'result'])
    expect(nativeRouteCallbackPhysicalCount(scope.signal)).toBe(0)
    policy.dispose()
  })

  it('aborts invalid async matching promptly but keeps physical work after logical close', async () => {
    const scope = new AbortController(),
      process = new AbortController(),
      events: string[] = []
    let release: ((value: boolean) => void) | undefined
    const pending = new Promise<boolean>((resolve) => {
      release = resolve
    })
    const policy = new NativeRouteURLMatcherPolicy((() => pending) as never)
    const invocation = invokeNativeRouteURLMatcher(
      {
        signal: scope.signal,
        processSignal: process.signal,
        start: () => Promise.resolve(),
        finish: () => {
          events.push('finish')
          return Promise.resolve()
        },
      },
      { invocation: 'actual', token: '1', scope: '1' },
      policy.callback(),
      'https://actual.test/',
      (value) => {
        expect(value).toBeUndefined()
        events.push('invalid')
        return Promise.resolve()
      },
      () => {
        throw new Error('Expected actual resolution')
      },
    )
    const failure = expect(invocation).rejects.toThrow('synchronous boolean')
    await Promise.resolve()
    await Promise.resolve()
    expect(events).toEqual(['invalid'])
    scope.abort()
    policy.dispose()
    expect(nativeRouteCallbackPhysicalCount(process.signal)).toBe(1)
    if (!release) throw new Error('Expected actual author work')
    release(false)
    await failure
    expect(events).toEqual(['invalid', 'finish'])
    expect(nativeRouteCallbackPhysicalCount(process.signal)).toBe(0)
  })

  it('retains async work through native rejection and preserves its separate actual error', async () => {
    const scope = new AbortController(),
      controlError = new Error('actual native pause rejection'),
      authorError = new Error('actual asynchronous continuation rejection'),
      late: Error[] = []
    let reject: ((error: Error) => void) | undefined
    const pending = new Promise<boolean>((_resolve, fail) => {
      reject = fail
    })
    const policy = new NativeRouteURLMatcherPolicy((() => pending) as never)
    const invocation = invokeNativeRouteURLMatcher(
      {
        signal: scope.signal,
        processSignal: scope.signal,
        start: () => Promise.resolve(),
        finish: () => Promise.resolve(),
      },
      { invocation: 'actual', token: '1', scope: '1' },
      policy.callback(),
      'https://actual.test/',
      () => Promise.reject(controlError),
      (error) => late.push(error),
    )
    const failure = expect(invocation).rejects.toBe(controlError)
    await Promise.resolve()
    await Promise.resolve()
    expect(nativeRouteCallbackPhysicalCount(scope.signal)).toBe(1)
    if (!reject) throw new Error('Expected actual pending author work')
    reject(authorError)
    await failure
    expect(late).toEqual([authorError])
    expect(nativeRouteCallbackPhysicalCount(scope.signal)).toBe(0)
    policy.dispose()
  })
})
