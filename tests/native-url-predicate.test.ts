import { expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import {
  runNativeURLPredicate,
  type NativeURLPredicateSnapshot,
} from '../src/native-url-predicate.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const listeners = new Set<(snapshot: NativeURLPredicateSnapshot) => void>()
  const controller = new AbortController()
  let sequence = 0
  const runAutomation = vi.fn(
    (_command: string, params?: Record<string, UntrustedInput>, signal?: AbortSignal) => {
      if (_command === 'urlPredicate') {
        if (typeof params?.clientToken !== 'string') throw new Error('Missing client token')
        ++sequence
        for (const listener of listeners)
          listener({
            id: sequence,
            generation: sequence,
            token: sequence,
            clientToken: params.clientToken,
            url: 'https://native.test/path',
          })
        if (signal?.aborted)
          return Promise.reject(
            signal.reason instanceof Error ? signal.reason : new Error('Aborted native request'),
          )
        return Promise.resolve({ matches: false, url: 'https://native.test/path' })
      }
      return Promise.resolve({})
    },
  )
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: controller.signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'fixture',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    onURLPredicate(listener) {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },
  }
  return { source, runAutomation, listeners }
}

it('executes the actual host closure with a URL and releases subscriptions after native completion', async () => {
  const { source, listeners } = fixture()
  const closedOver = 'native.test'
  const predicate = vi.fn((url: URL) => url.hostname === closedOver)
  expect(await runNativeURLPredicate(source, predicate, { timeout: 1 })).toEqual({
    matches: false,
    url: 'https://native.test/path',
  })
  expect(predicate).toHaveBeenCalledWith(new URL('https://native.test/path'))
  expect(listeners.size).toBe(0)
})

it('retains physical capacity for cancelled unresolved callbacks and recovers only after actual settlement', async () => {
  const { source, listeners } = fixture()
  const resolvers: ((value: boolean) => void)[] = []
  const neverSettled = () =>
    new Promise<boolean>((resolve) => {
      resolvers.push(resolve)
    })
  for (let i = 0; i < 64; ++i) await runNativeURLPredicate(source, neverSettled, { timeout: 1 })
  expect(resolvers).toHaveLength(64)
  const independent = fixture()
  await expect(
    runNativeURLPredicate(independent.source, neverSettled, { timeout: 1 }),
  ).rejects.toThrow('physical callback capacity')
  expect(resolvers).toHaveLength(64)
  await expect(runNativeURLPredicate(source, neverSettled, { timeout: 1 })).rejects.toThrow(
    'physical callback capacity',
  )
  expect(resolvers).toHaveLength(64)
  const resolve = resolvers[0]
  if (!resolve) throw new Error('Missing physical callback')
  resolve(true)
  await Promise.resolve()
  await Promise.resolve()
  await runNativeURLPredicate(source, neverSettled, { timeout: 1 })
  expect(resolvers).toHaveLength(65)
  for (const finish of resolvers) finish(false)
  await Promise.resolve()
  await Promise.resolve()
  expect(listeners.size).toBe(0)
})
