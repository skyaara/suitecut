// SPDX-License-Identifier: MIT
import { AsyncLocalStorage } from 'node:async_hooks'

import { describe, expect, it } from 'vitest'

import { NativeLocatorHandlerContexts } from '../src/native-locator-handler-contexts.js'
import { type NativeLocatorHandlerStage } from '../src/native-protocol.js'

const event = (stage: NativeLocatorHandlerStage['stage']): NativeLocatorHandlerStage => ({
  stage,
  id: 8,
  generation: 3,
  token: 5,
  handlerId: 7,
  clientToken: 'actual-client',
})
describe('native locator handler actual action context', () => {
  it('uses actual action admission ALS, retains after public pending deletion, and waits both native receipts', async () => {
    const context = new AsyncLocalStorage<string>(),
      actual = new NativeLocatorHandlerContexts()
    let observed: string | undefined
    actual.subscribe(1, (value) => {
      if (value.stage === 'locatorHandlerRun') observed = context.getStore()
    })
    const lifetime = context.run('real triggering action', () => actual.capture(8, 1))!
    let settled = false
    void lifetime.then(() => {
      settled = true
    })
    context.run('unrelated receiver', () => actual.receive(1, event('locatorHandlerRun')))
    expect(observed).toBe('real triggering action')
    actual.response(8)
    await Promise.resolve()
    expect(settled).toBe(false)
    actual.receive(1, event('locatorHandlerRetired'))
    await lifetime
    expect(settled).toBe(true)
  })
  it('takes no snapshot or lifetime when no callback receiver is registered', () => {
    expect(new NativeLocatorHandlerContexts().capture(8, 1)).toBeUndefined()
  })
  it('rejects missing/replayed/foreign native invocation identities before authored callbacks', () => {
    const actual = new NativeLocatorHandlerContexts()
    let calls = 0
    actual.subscribe(1, () => {
      ++calls
    })
    expect(() => actual.receive(1, event('locatorHandlerRun'))).toThrow('admitted action')
    void actual.capture(8, 1)
    expect(() => actual.receive(2, event('locatorHandlerRun'))).toThrow('admitted action')
    actual.receive(1, event('locatorHandlerRun'))
    expect(() => actual.receive(1, event('locatorHandlerRun'))).toThrow('replayed')
    expect(calls).toBe(1)
    actual.closed()
  })
})
