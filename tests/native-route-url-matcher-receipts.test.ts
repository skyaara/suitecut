// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeContextRouteInvocations } from '../src/native-context-route-invocations.js'
import {
  nativeRouteCommandFailed,
  nativeRouteHandlerInvoked,
} from '../src/native-route-handler-observations.js'

describe('matcher callbacks in actual route removal selection', () => {
  it('waits on the exact original callback receipt, excludes future calls and removes canceled edges', async () => {
    const book = new NativeContextRouteInvocations()
    let release = () => undefined as void
    const pending = new Promise<void>((resolve) => {
      release = resolve
    })
    const callback = book.resolveCallback('actual-policy', () => pending)
    const physical = callback()
    nativeRouteHandlerInvoked(callback, physical)
    const wait = book.snapshot(new Set(['actual-policy']))
    let settled = false
    void wait.settled.then(() => (settled = true))
    for (let index = 0; index < 1000; ++index) {
      const canceled = book.snapshot(new Set(['actual-policy']))
      canceled.cancel()
    }
    expect(book.waitObservationEdges()).toBe(1)
    let releaseFuture = () => undefined as void
    const future = book.resolveCallback(
      'actual-policy',
      () =>
        new Promise<void>((resolve) => {
          releaseFuture = resolve
        }),
    )
    const futurePhysical = future()
    nativeRouteHandlerInvoked(future, futurePhysical)
    await Promise.resolve()
    expect(settled).toBe(false)
    release()
    await physical
    await wait.settled
    expect(book.pending()).toBe(1)
    expect(book.waitObservationEdges()).toBe(0)
    releaseFuture()
    await futurePhysical
    await Promise.resolve()
    expect(book.pending()).toBe(0)
  })

  it('suppresses only selected user errors after actual mutation success, preserving command failures', async () => {
    const book = new NativeContextRouteInvocations(),
      original = new Error('actual predicate failure'),
      control = nativeRouteCommandFailed(new Error('real native command failure'))
    let release = () => undefined as void
    const pending = new Promise<void>((resolve) => {
      release = resolve
    })
    const selected = book.resolveCallback('selected', async () => {
      await pending
      throw original
    })
    const command = book.resolveCallback('selected', async () => {
      await pending
      throw control
    })
    const other = book.resolveCallback('not-selected', async () => {
      await pending
      throw original
    })
    const selectedPhysical = selected(),
      commandPhysical = command(),
      otherPhysical = other()
    nativeRouteHandlerInvoked(selected, selectedPhysical)
    nativeRouteHandlerInvoked(command, commandPhysical)
    nativeRouteHandlerInvoked(other, otherPhysical)
    const nativeFailure = expect(commandPhysical).rejects.toBe(control),
      unrelatedFailure = expect(otherPhysical).rejects.toBe(original)
    book.ignore(new Set(['selected']), Promise.resolve(true))
    await Promise.resolve()
    release()
    await expect(selectedPhysical).resolves.toBeUndefined()
    await nativeFailure
    await unrelatedFailure
    expect(book.pending()).toBe(0)
    expect(book.observationEdges()).toBe(0)
  })
})
