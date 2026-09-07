import { setImmediate } from 'node:timers/promises'

import { describe, expect, it } from 'vitest'

import { raceWithAbort } from '../src/abort.js'

describe('abort races', () => {
  it('returns the operation when no signal is present', async () => {
    await expect(raceWithAbort(Promise.resolve('done'))).resolves.toBe('done')
  })

  it('preserves an Error used as the abort reason', async () => {
    const controller = new AbortController()
    const reason = new Error('stop recording')
    const operation = new Promise<never>(() => undefined)

    controller.abort(reason)

    await expect(raceWithAbort(operation, controller.signal)).rejects.toBe(reason)
  })

  it('observes an abandoned operation when the signal is already aborted', async () => {
    const controller = new AbortController()
    const sourceError = new Error('late operation failure')
    const operation = Promise.resolve().then(() => {
      throw sourceError
    })

    controller.abort()

    await expect(raceWithAbort(operation, controller.signal)).rejects.toMatchObject({
      name: 'AbortError',
    })
    await setImmediate()
  })
})
