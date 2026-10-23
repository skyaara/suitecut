// SPDX-License-Identifier: MIT
import { expect, it, vi } from 'vitest'
import * as z from 'zod'

import { type NativeBrowser } from '../src/native-browser.js'
import {
  waitNativeEventPredicate,
  type NativeEventPredicateSnapshot,
} from '../src/native-event-predicate.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture(mode: 'candidates' | 'timeout' = 'candidates') {
  const listeners = new Set<(value: NativeEventPredicateSnapshot) => void>()
  const lifetime = new AbortController()
  let candidate = 0,
    clientToken = ''
  let complete: ((value: UntrustedInput) => void) | undefined
  const emit = () => {
    ++candidate
    for (const listener of listeners)
      listener({
        id: 7,
        generation: 8,
        token: candidate,
        clientToken,
        event: 'console',
        sequence: 42 + candidate,
        value: { text: candidate === 1 ? 'first' : 'accepted' },
      })
  }
  const runAutomation = vi.fn(
    (
      command: string,
      params?: Record<string, UntrustedInput>,
      signal?: AbortSignal,
    ): Promise<UntrustedInput> => {
      if (command === 'waitEventPredicate') {
        clientToken = z.string().parse(params?.clientToken)
        if (mode === 'timeout') {
          emit()
          return Promise.reject(
            signal?.reason instanceof Error
              ? signal.reason
              : new Error('Native event predicate Timeout'),
          )
        }
        return new Promise((resolve, reject) => {
          complete = resolve
          signal?.addEventListener(
            'abort',
            () => reject(signal.reason instanceof Error ? signal.reason : new Error('Aborted')),
            { once: true },
          )
          emit()
        })
      }
      if (command === 'continueEventPredicate') {
        if (params?.matches === true) complete?.({ text: 'accepted' })
        else emit()
      }
      return Promise.resolve({})
    },
  )
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: lifetime.signal,
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
    eventSequence: () => 42,
    onEventPredicate(listener) {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },
  }
  return { source, runAutomation, listeners }
}

it('uses one native deadline/cursor across rejected candidates and returns the accepted actual object', async () => {
  const { source, runAutomation, listeners } = fixture()
  const schema = z.object({ text: z.string() })
  let accepted: z.infer<typeof schema> | undefined
  const result = await waitNativeEventPredicate(
    source,
    'console',
    (value) => schema.parse(value),
    (value) => {
      accepted = value
      return value.text === 'accepted'
    },
    { timeout: 137 },
  )
  expect(result).toBe(accepted)
  expect(result.text).toBe('accepted')
  expect(
    runAutomation.mock.calls.filter(([command]) => command === 'waitEventPredicate'),
  ).toHaveLength(1)
  expect(runAutomation).toHaveBeenNthCalledWith(
    1,
    'waitEventPredicate',
    expect.objectContaining({ timeoutMs: 137, afterSequence: 42 }),
    expect.any(AbortSignal),
  )
  expect(
    runAutomation.mock.calls.filter(([command]) => command === 'continueEventPredicate'),
  ).toHaveLength(2)
  expect(listeners.size).toBe(0)
})

it('preserves the original abort cause while a host predicate remains physically pending', async () => {
  const { source, listeners } = fixture()
  const controller = new AbortController()
  let settle: ((value: boolean) => void) | undefined
  const waiting = waitNativeEventPredicate(
    source,
    'console',
    (value) => value,
    () =>
      new Promise<boolean>((resolve) => {
        settle = resolve
      }),
    { signal: controller.signal },
  )
  await Promise.resolve()
  await Promise.resolve()
  const reason = new Error('caller original cause')
  controller.abort(reason)
  await expect(waiting).rejects.toBe(reason)
  expect(listeners.size).toBe(0)
  if (!settle) throw new Error('Missing physical predicate')
  settle(false)
  await Promise.resolve()
  await Promise.resolve()
})

it('bounds unresolved physical callbacks across closed native waits and other browser sources', async () => {
  const { source } = fixture('timeout')
  const finish: ((value: boolean) => void)[] = []
  const callback = () =>
    new Promise<boolean>((resolve) => {
      finish.push(resolve)
    })
  for (let i = 0; i < 64; ++i)
    await expect(
      waitNativeEventPredicate(source, 'console', (value) => value, callback, { timeout: 1 }),
    ).rejects.toThrow('Timeout')
  expect(finish).toHaveLength(64)
  const second = fixture('timeout')
  await expect(
    waitNativeEventPredicate(second.source, 'console', (value) => value, callback, { timeout: 1 }),
  ).rejects.toThrow('physical host callback capacity')
  expect(finish).toHaveLength(64)
  for (const resolve of finish) resolve(false)
  await Promise.resolve()
  await Promise.resolve()
  await Promise.resolve()
})
