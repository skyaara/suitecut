// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  NativeContextWorkerEvents,
  type NativeContextWorkerTransport,
} from '../src/native-context-workers.js'

describe('native context worker subscription lifetime', () => {
  it.each([false, true])(
    'releases empty worker subscriptions when closed before construction: %s',
    (alreadyClosed) => {
      const process = new AbortController()
      const context = new AbortController()
      const subscriptions = new Set<object>()
      const subscribe = () => {
        const identity = {}
        subscriptions.add(identity)
        return () => {
          subscriptions.delete(identity)
        }
      }
      const transport: NativeContextWorkerTransport = {
        signal: process.signal,
        processSignal: process.signal,
        runContext: () => Promise.resolve(undefined),
        contextWorkerInfos: () => [],
        onContextWorkerMessage: subscribe,
        onContextConsole: subscribe,
        onContextWorkerPredicate: subscribe,
      }
      if (alreadyClosed) context.abort()
      new NativeContextWorkerEvents(
        transport,
        1,
        context.signal,
        () => 1000,
        () => false,
        () => {
          throw new Error('Unexpected console event')
        },
      )
      if (!alreadyClosed) {
        expect(subscriptions.size).toBeGreaterThan(0)
        context.abort()
      }
      expect(subscriptions.size).toBe(0)
      expect(process.signal.aborted).toBe(false)
    },
  )
})
