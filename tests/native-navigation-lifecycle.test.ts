// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  pendingNativeNavigationCount,
  prepareNativeNavigationClose,
  runNativeNavigation,
} from '../src/native-navigation-lifecycle.js'

describe('native navigation lifecycle', () => {
  it('preserves actual failure for awaited callers during explicit teardown', async () => {
    const owner = {}
    let reject!: (error: Error) => void
    const operation = new Promise<string>((_resolve, fail) => {
      reject = fail
    })
    const result = runNativeNavigation(owner, () => operation)
    prepareNativeNavigationClose(owner)
    expect(pendingNativeNavigationCount(owner)).toBe(1)
    const actual = new Error('actual native page close reason')
    reject(actual)
    await expect(result).rejects.toBe(actual)
    expect(pendingNativeNavigationCount(owner)).toBe(0)
  })

  it('admits navigations past the old 64 and retains pending operations until actual settlement', async () => {
    const owner = {}
    const rejectors: ((error: Error) => void)[] = []
    let issued = 0
    const operations = Array.from({ length: 64 }, () =>
      runNativeNavigation(owner, () => {
        ++issued
        return new Promise<void>((_resolve, reject) => {
          rejectors.push(reject)
        })
      }),
    )
    await runNativeNavigation(owner, () => {
      ++issued
      return Promise.resolve()
    })
    expect(issued).toBe(65)
    prepareNativeNavigationClose(owner)
    expect(pendingNativeNavigationCount(owner)).toBe(64)
    for (const reject of rejectors) reject(new Error('actual closure'))
    const outcomes = await Promise.allSettled(operations)
    expect(outcomes.every((outcome) => outcome.status === 'rejected')).toBe(true)
    expect(pendingNativeNavigationCount(owner)).toBe(0)
  })
})
