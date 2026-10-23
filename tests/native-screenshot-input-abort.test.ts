// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { type NativeArtifactRunner } from '../src/native-artifacts.js'
import { runNativeScreenshotInput } from '../src/native-screenshot-input.js'
import { type UntrustedInput } from '../src/untrusted.js'

import { InputFixture } from './native-screenshot-input-fixture.js'

const turn = () => new Promise((resolve) => setImmediate(resolve))

describe('public screenshot failure with physically owned cleanup', () => {
  it('returns all16 original abort reasons while retaining every cleanup slot through real receipts', async () => {
    const fixtures = Array.from({ length: 16 }, () => new InputFixture())
    const controllers = fixtures.map(() => new AbortController())
    const releases: (() => void)[] = []
    const reasons = fixtures.map((_, i) => new Error(`original cancel ${i}`))
    const pending = fixtures.map((fixture, i) => {
      fixture.source.runContext = (_action, params = {}) => {
        expect(params).toMatchObject({ action: 'discard', pageId: 7, timeoutMs: 0 })
        return new Promise((resolve) => releases.push(() => resolve({})))
      }
      const controller = controllers[i]!
      const run: NativeArtifactRunner = (_params, signal) =>
        new Promise((_resolve, reject) => {
          signal!.addEventListener(
            'abort',
            () =>
              reject(
                signal!.reason instanceof Error
                  ? signal!.reason
                  : new Error('Controlled signal was not an Error'),
              ),
            { once: true },
          )
        })
      return runNativeScreenshotInput(
        run,
        fixture.source,
        Buffer.alloc(33),
        {},
        0,
        controller.signal,
      ).catch((error: UntrustedInput) => error)
    })
    controllers.forEach((controller, i) => controller.abort(reasons[i]))
    expect(await Promise.all(pending)).toEqual(reasons)
    await turn()
    expect(releases).toHaveLength(16)
    const overflow = new InputFixture()
    await expect(
      runNativeScreenshotInput(overflow.run, overflow.source, Buffer.alloc(33), {}, 0),
    ).rejects.toThrow('staging capacity')
    expect(overflow.actions).toHaveLength(0)
    for (const release of releases) release()
    await turn()
    await runNativeScreenshotInput(overflow.run, overflow.source, Buffer.alloc(33), {}, 0)
  })

  it('publishes a genuine native deadline failure while discard remains pending', async () => {
    const fixture = new InputFixture()
    const expired = new Error(
      'Native screenshot assertion original deadline expired during baseline input',
    )
    let release!: () => void
    fixture.source.runContext = (_action, params = {}) => {
      expect(params.action).toBe('discard')
      return new Promise((resolve) => {
        release = () => resolve({})
      })
    }
    const result = runNativeScreenshotInput(
      () => Promise.reject(expired),
      fixture.source,
      Buffer.alloc(33),
      {},
      45,
    )
    await expect(result).rejects.toBe(expired)
    await turn()
    release()
    await turn()
  })

  it('bounds unreported late cleanup failures in the same16-slot budget and consumes them only on their source', async () => {
    const fixtures = Array.from({ length: 16 }, () => new InputFixture())
    const failures = fixtures.map((fixture, i) => {
      fixture.source.runContext = () => Promise.reject(new Error(`late cleanup ${i}`))
      return runNativeScreenshotInput(
        () => Promise.reject(new Error(`prepare ${i}`)),
        fixture.source,
        Buffer.alloc(33),
        {},
        0,
      ).catch((error: UntrustedInput) => error)
    })
    await Promise.all(failures)
    await turn()
    const unrelated = new InputFixture()
    await expect(
      runNativeScreenshotInput(unrelated.run, unrelated.source, Buffer.alloc(33), {}, 0),
    ).rejects.toThrow('staging capacity')
    expect(unrelated.actions).toHaveLength(0)
    for (const fixture of fixtures) {
      await expect(
        runNativeScreenshotInput(fixture.run, fixture.source, Buffer.alloc(33), {}, 0),
      ).rejects.toThrow('Earlier native screenshot input cleanup failed')
    }
    await runNativeScreenshotInput(unrelated.run, unrelated.source, Buffer.alloc(33), {}, 0)
  })

  it('does not publish successful comparison until genuine discard acknowledgment', async () => {
    const fixture = new InputFixture()
    const original = fixture.source.runContext
    let release!: () => void
    fixture.source.runContext = (action, params = {}, signal) =>
      params.action === 'discard'
        ? new Promise((resolve) => {
            release = () => resolve({})
          })
        : original(action, params, signal)
    let resolved = false
    const pending = runNativeScreenshotInput(
      fixture.run,
      fixture.source,
      Buffer.alloc(33),
      {},
      0,
    ).then((value) => {
      resolved = true
      return value
    })
    await turn()
    expect(resolved).toBe(false)
    release()
    await pending
    expect(resolved).toBe(true)
  })
})
