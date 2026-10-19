// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it } from 'vitest'

import { NativeFixtureTimeoutCallbacks } from '../src/native-runner-fixture-timeouts.js'

const owner = { token: '1', scope: '2', generation: '3' }
const encode = (value: string): string => Buffer.from(value, 'utf16le').toString('hex')
function harness() {
  const packets: (string | number)[][] = [],
    fatal: Error[] = []
  const callbacks = new NativeFixtureTimeoutCallbacks(
    (...fields) => {
      packets.push(fields)
      return Promise.resolve()
    },
    (error) => {
      fatal.push(error)
    },
    () => undefined,
  )
  callbacks.packet(['FIXTURE_CLOCK', '1', '2', '3', 'setup', '1000'])
  const info = callbacks.bind(owner)
  return { callbacks, info, packets, fatal }
}
describe('native fixture timeout author ledger', () => {
  it('accepts exact-owner abort after local READY publication while raw is live, without reopening admission', async () => {
    const { callbacks, info } = harness()
    await callbacks.run(owner, () => callbacks.beforeReady(owner))
    expect(callbacks.idleReady).toBe(true)
    const reason = 'Actual native original slot expired before READY acceptance'
    callbacks.packet(['FIXTURE_ABORT', '1', '2', '3', encode(reason)])
    expect(info.signal.aborted).toBe(true)
    expect((info.signal.reason as Error).message).toBe(reason)
    expect(callbacks.idleReady).toBe(false)
    expect(callbacks.physical).toBe(0)
    expect(callbacks.contextCount).toBe(1)
    expect(() => callbacks.run(owner, () => info.setTimeout(100))).toThrow(
      'Native fixture author context has no actual setup admission',
    )
    expect(() => callbacks.packet(['FIXTURE_ABORT', '1', '2', '4', encode(reason)])).toThrow(
      /Stale/u,
    )
    expect(await callbacks.beforeRawSettlement(owner)).toBeUndefined()
    callbacks.closed(owner)
    expect(() => callbacks.packet(['FIXTURE_ABORT', '1', '2', '3', encode(reason)])).toThrow(
      /Stale/u,
    )
    expect(callbacks.contextCount).toBe(0)
  })

  it('continues late use after exact native cancellation without claiming READY', async () => {
    const { callbacks, packets } = harness()
    let resume!: () => void
    const admitted = new Promise<void>((resolve) => {
      resume = resolve
    })
    let rawSettled = false
    const raw = callbacks.run(owner, async () => {
      await admitted
      const ready = await callbacks.beforeReady(owner)
      expect(ready).toBe(false)
      rawSettled = true // The actual authored code after use can execute.
    })
    callbacks.packet(['FIXTURE_ABORT', '1', '2', '3', encode('actual native setup cancel')])
    resume()
    await raw
    expect(rawSettled).toBe(true)
    expect(packets).toEqual([])
    expect(callbacks.contextCount).toBe(1)
    expect(callbacks.idleReady).toBe(false)
    await expect(callbacks.beforeReady(owner)).rejects.toThrow(/readiness lacks/u)
    await expect(callbacks.beforeReady({ ...owner, generation: '4' })).rejects.toThrow(
      /stale or foreign/u,
    )
    expect(await callbacks.beforeRawSettlement(owner)).toBeUndefined()
    callbacks.closed(owner)
    await expect(callbacks.beforeReady(owner)).rejects.toThrow(/stale or foreign/u)
  })
  it('retains actual amendment ACK before canceled late-use continuation', async () => {
    const { callbacks, info } = harness()
    const mutation = callbacks.run(owner, () => info.setTimeout(100))
    callbacks.packet(['FIXTURE_ABORT', '1', '2', '3', encode('actual native setup cancel')])
    let continued = false
    const raw = callbacks.beforeReady(owner).then((ready) => {
      expect(ready).toBe(false)
      continued = true
    })
    await Promise.resolve()
    expect(continued).toBe(false)
    expect(callbacks.physical).toBe(1)
    callbacks.packet(['FIXTURE_TIMEOUT_RESULT', '1', '1', '2', '3', '1', '100'])
    await Promise.all([mutation, raw])
    expect(continued).toBe(true)
    expect(await callbacks.beforeRawSettlement(owner)).toBeUndefined()
    callbacks.closed(owner)
  })
  it('keeps rejected native amendments hard after genuine setup cancellation', async () => {
    const { callbacks, info } = harness()
    const mutation = callbacks.run(owner, () => info.setTimeout(100))
    const observed = expect(mutation).rejects.toThrow('actual native rejected amendment')
    callbacks.packet(['FIXTURE_ABORT', '1', '2', '3', encode('actual native setup cancel')])
    const ready = callbacks.beforeReady(owner)
    const rejected = expect(ready).rejects.toThrow('actual native rejected amendment')
    callbacks.packet([
      'FIXTURE_TIMEOUT_RESULT',
      '1',
      '1',
      '2',
      '3',
      '0',
      encode('actual native rejected amendment'),
    ])
    await Promise.all([observed, rejected])
    expect((await callbacks.beforeRawSettlement(owner))?.message).toBe(
      'actual native rejected amendment',
    )
    callbacks.closed(owner)
  })
  it('keeps ignored amendments physical until exact ACK before READY', async () => {
    const { callbacks, info, packets, fatal } = harness()
    callbacks.run(owner, () => {
      void info.setTimeout(100)
    })
    let ready = false
    const drained = callbacks.beforeReady(owner).then(() => {
      ready = true
    })
    await Promise.resolve()
    expect(ready).toBe(false)
    expect(callbacks.physical).toBe(1)
    expect(info.timeout).toBe(1000)
    expect(packets).toEqual([['FIXTURE_TIMEOUT', '1', '1', '2', '3', 100]])
    expect(() =>
      callbacks.packet(['FIXTURE_TIMEOUT_RESULT', '1', '1', '2', '4', '1', '100']),
    ).toThrow(/Stale/u)
    expect(callbacks.physical).toBe(1)
    callbacks.packet(['FIXTURE_TIMEOUT_RESULT', '1', '1', '2', '3', '1', '100'])
    await drained
    expect(info.timeout).toBe(100)
    expect(callbacks.idleReady).toBe(true)
    expect(callbacks.contextCount).toBe(1)
    expect(fatal).toEqual([])
    expect(() =>
      callbacks.packet(['FIXTURE_TIMEOUT_RESULT', '1', '1', '2', '3', '1', '100']),
    ).toThrow(/Stale/u)
    expect(await callbacks.beforeRawSettlement(owner)).toBeUndefined()
    callbacks.closed(owner)
  })
  it('retains the actual fixture invocation through use and teardown with fresh native slot', async () => {
    const { callbacks, info } = harness()
    let resume!: () => void
    const release = new Promise<void>((resolve) => {
      resume = resolve
    })
    let requested!: () => void
    const pending = new Promise<void>((resolve) => {
      requested = resolve
    })
    let observedReady!: () => void
    const ready = new Promise<void>((resolve) => {
      observedReady = resolve
    })
    const invocation = callbacks.run(owner, async () => {
      await callbacks.beforeReady(owner)
      observedReady()
      await release
      const amendment = info.setTimeout(200)
      requested()
      await amendment
      return info.timeout
    })
    await ready
    callbacks.packet(['FIXTURE_CLOCK', '1', '2', '3', 'teardown', '1000'])
    resume()
    await pending
    expect(callbacks.idleReady).toBe(false)
    callbacks.packet(['FIXTURE_TIMEOUT_RESULT', '1', '1', '2', '3', '1', '200'])
    expect(await invocation).toBe(200)
    expect(await callbacks.beforeRawSettlement(owner)).toBeUndefined()
    callbacks.closed(owner)
    expect(() => info.setTimeout(100)).toThrow(/no longer active/u)
  })
  it('retains a genuine rejected ignored ACK separately from the authored raw error', async () => {
    const { callbacks, info } = harness()
    callbacks.run(owner, () => {
      void info.setTimeout(200)
    })
    const authored = new Error('original raw author failure')
    const drain = callbacks.beforeRawSettlement(owner)
    callbacks.packet([
      'FIXTURE_TIMEOUT_RESULT',
      '1',
      '1',
      '2',
      '3',
      '0',
      encode('native slot expired'),
    ])
    const amendmentError = await drain
    expect(amendmentError?.message).toBe('native slot expired')
    expect(amendmentError).not.toBe(authored)
    expect(authored.message).toBe('original raw author failure')
    expect(callbacks.idleReady).toBe(false)
    callbacks.closed(owner)
  })
  it('observes genuine native cancel before amendment ACK without inventing ACK order', async () => {
    const { callbacks, info } = harness()
    const mutation = callbacks.run(owner, () => info.setTimeout(25))
    callbacks.packet(['FIXTURE_ABORT', '1', '2', '3', encode('actual native fixture deadline')])
    expect(info.signal.aborted).toBe(true)
    expect(info.signal.reason).toBeInstanceOf(Error)
    expect(callbacks.physical).toBe(1)
    callbacks.packet(['FIXTURE_TIMEOUT_RESULT', '1', '1', '2', '3', '1', '25'])
    await mutation
    expect(info.timeout).toBe(25)
    expect(await callbacks.beforeRawSettlement(owner)).toBeUndefined()
    callbacks.closed(owner)
  })
  it('rejects surviving old author context after actual raw closure', async () => {
    const { callbacks, info } = harness()
    let resume!: () => void
    const wait = new Promise<void>((resolve) => {
      resume = resolve
    })
    const detached = callbacks.run(owner, async () => {
      await wait
      expect(() => info.setTimeout(200)).toThrow(/no longer active/u)
    })
    expect(await callbacks.beforeRawSettlement(owner)).toBeUndefined()
    callbacks.closed(owner)
    resume()
    await detached
    expect(callbacks.physical).toBe(0)
  })
})
