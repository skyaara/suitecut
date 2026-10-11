import { describe, expect, it } from 'vitest'

import { CDPCommandQueue } from '../src/cdp-command-queue.js'

describe('CDP native command backpressure', () => {
  it('submits a large burst in order without exhausting native command slots', async () => {
    const sent: number[] = []
    let active = 0
    let maximum = 0
    let finish: () => void = () => undefined
    const completed = new Promise<void>((resolve) => {
      finish = resolve
    })
    const errors: string[] = []
    const queue = new CDPCommandQueue(
      async (message) => {
        sent.push((message as { id: number }).id)
        maximum = Math.max(maximum, ++active)
        await new Promise<void>((resolve) => setImmediate(resolve))
        active--
        if (sent.length === 4000 && active === 0) finish()
      },
      (error) => errors.push(error),
    )
    for (let id = 0; id < 4000; id++) queue.enqueue({ id }, 20)
    await completed
    expect(sent).toEqual(Array.from({ length: 4000 }, (_, id) => id))
    expect(maximum).toBe(16)
    expect(errors).toEqual([])
    queue.close()
  })

  it.each(['count', 'bytes'] as const)(
    'counts active commands toward the %s bound',
    async (kind) => {
      const errors: string[] = []
      let release: () => void = () => undefined
      const held = new Promise<void>((resolve) => {
        release = resolve
      })
      const queue = new CDPCommandQueue(
        () => held,
        (error) => errors.push(error),
        1,
        kind === 'count' ? 2 : 100,
        kind === 'bytes' ? 20 : 1000,
      )
      queue.enqueue({ id: 1 }, 10)
      await Promise.resolve()
      queue.enqueue({ id: 2 }, 10)
      queue.enqueue({ id: 3 }, 10)
      expect(errors).toEqual(['CEF CDP command capacity exceeded'])
      release()
    },
  )

  it('drops queued work on close even when an in-flight acknowledgement arrives later', async () => {
    const sent: object[] = []
    let release: () => void = () => undefined
    const held = new Promise<void>((resolve) => {
      release = resolve
    })
    const errors: string[] = []
    const queue = new CDPCommandQueue(
      (message) => {
        sent.push(message)
        return held
      },
      (error) => errors.push(error),
      1,
    )
    queue.enqueue({ id: 1 }, 10)
    queue.enqueue({ id: 2 }, 10)
    await Promise.resolve()
    queue.close()
    release()
    await new Promise<void>((resolve) => setImmediate(resolve))
    expect(sent).toEqual([{ id: 1 }])
    expect(errors).toEqual([])
  })

  it('reports native rejection once and never dispatches queued commands', async () => {
    const sent: object[] = []
    const errors: string[] = []
    const queue = new CDPCommandQueue(
      (message) => {
        sent.push(message)
        return Promise.reject(new Error('native pipe closed'))
      },
      (error) => errors.push(error),
      1,
    )
    queue.enqueue({ id: 1 }, 10)
    queue.enqueue({ id: 2 }, 10)
    await new Promise<void>((resolve) => setImmediate(resolve))
    expect(sent).toEqual([{ id: 1 }])
    expect(errors).toEqual(['native pipe closed'])
  })
})
