import { setTimeout as delay } from 'node:timers/promises'

import { describe, expect, it } from 'vitest'

import { CDPDelivery } from '../src/cdp-delivery.js'

describe('CDP transport task ordering', () => {
  it('runs frame-tree promise continuations before the next execution-context event', async () => {
    let frameRegistered = false
    let resolveFrame: () => void = () => undefined
    const frame = new Promise<void>((resolve) => {
      resolveFrame = resolve
    })
    void frame.then(() => {
      frameRegistered = true
    })
    const observed: boolean[] = []
    let resolveObserved: () => void = () => undefined
    const observation = new Promise<void>((resolve) => {
      resolveObserved = resolve
    })
    const delivery = new CDPDelivery(
      (message) => {
        if ('result' in message) resolveFrame()
        else {
          observed.push(frameRegistered)
          resolveObserved()
        }
      },
      () => undefined,
    )
    delivery.enqueue({ result: 'frame tree' }, 20)
    delivery.enqueue({ method: 'Runtime.executionContextCreated' }, 40)
    await observation
    expect(observed).toEqual([true])
    delivery.close('test complete')
  })

  it('preserves a burst of packets and drains final events before disconnecting', async () => {
    const received: object[] = []
    const messages = Array.from({ length: 80 }, (_, id) => ({ id }))
    let resolveClosed: () => void = () => undefined
    const closed = new Promise<void>((resolve) => {
      resolveClosed = resolve
    })
    const delivery = new CDPDelivery(
      (message) => received.push(message),
      () => {
        received.push({ closed: true })
        resolveClosed()
      },
    )
    for (const message of messages) delivery.enqueue(message, 10)
    delivery.end('process exited')
    delivery.enqueue({ ignored: true }, 10)
    await closed
    expect(received).toEqual([...messages, { closed: true }])
  })

  it.each(['messages', 'bytes'] as const)(
    'closes and releases pending packets on %s overflow',
    async (bound) => {
      const received: object[] = []
      const closed: string[] = []
      const delivery = new CDPDelivery(
        (message) => received.push(message),
        (reason) => closed.push(reason),
        bound === 'messages' ? 1 : 10,
        20,
      )
      delivery.enqueue({ id: 1 }, 10)
      delivery.enqueue({ id: 2 }, bound === 'bytes' ? 11 : 10)
      await delay(20)
      expect(received).toEqual([])
      expect(closed).toEqual(['CEF CDP delivery capacity exceeded'])
    },
  )

  it('does not deliver queued packets after explicit close', async () => {
    const received: object[] = []
    const closed: string[] = []
    const delivery = new CDPDelivery(
      (message) => received.push(message),
      (reason) => closed.push(reason),
    )
    delivery.enqueue({ id: 1 }, 10)
    delivery.close('closed')
    delivery.close('duplicate')
    delivery.enqueue({ id: 2 }, 10)
    await delay(20)
    expect(received).toEqual([])
    expect(closed).toEqual(['closed'])
  })

  it('closes once if protocol delivery throws', async () => {
    const closed: string[] = []
    const delivery = new CDPDelivery(
      () => {
        throw new Error('broken handler')
      },
      (reason) => closed.push(reason),
    )
    delivery.enqueue({ id: 1 }, 10)
    delivery.enqueue({ id: 2 }, 10)
    await delay(20)
    expect(closed).toEqual(['CEF CDP delivery failed'])
  })
})
