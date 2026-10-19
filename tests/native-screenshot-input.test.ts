import { describe, expect, it } from 'vitest'

import { type NativeArtifactRunner } from '../src/native-artifacts.js'
import { runNativeScreenshotInput } from '../src/native-screenshot-input.js'
import { toError, type UntrustedInput } from '../src/untrusted.js'

import { InputFixture } from './native-screenshot-input-fixture.js'

describe('bounded native screenshot input transport', () => {
  it('transfers large Buffer/config payloads in exact ordered chunks below the actual64KiB command cap', async () => {
    const fixture = new InputFixture()
    const bytes = Buffer.alloc(512 * 1024 + 17)
    for (let i = 0; i < bytes.length; ++i) bytes[i] = (i * 73) % 256
    await runNativeScreenshotInput(
      fixture.run,
      fixture.source,
      bytes,
      { style: 'x'.repeat(130001) },
      78,
    )
    expect(
      Buffer.concat(fixture.chunks.filter((c) => c.section === 'png').map((c) => c.bytes)),
    ).toEqual(bytes)
    expect(fixture.config()).toMatchObject({ style: 'x'.repeat(130001) })
    for (const section of ['config', 'png']) {
      let next = 0
      for (const chunk of fixture.chunks.filter((c) => c.section === section)) {
        expect(chunk.offset).toBe(next)
        expect(chunk.bytes.length).toBeLessThanOrEqual(24576)
        next += chunk.bytes.length
      }
    }
    for (const action of fixture.actions)
      expect(
        Buffer.byteLength(
          JSON.stringify({
            command: 'context',
            id: 2147483647,
            pageId: 1,
            version: 1,
            params: action,
          }),
        ) + 1,
      ).toBeLessThan(65536)
  })
  it('captures one immutable Buffer snapshot before asynchronous native admission', async () => {
    const fixture = new InputFixture(),
      bytes = Buffer.alloc(131077, 17)
    const pending = runNativeScreenshotInput(fixture.run, fixture.source, bytes, {}, 0)
    bytes.fill(91)
    await pending
    expect(
      Buffer.concat(fixture.chunks.filter((c) => c.section === 'png').map((c) => c.bytes)),
    ).toEqual(Buffer.alloc(131077, 17))
  })
  it('bounds native config encoding before output allocation while preserving exact JSON string units', async () => {
    const fixture = new InputFixture(),
      text = '\u0000\b\t\n\f\r"\\é中😀\ud800'
    await runNativeScreenshotInput(
      fixture.run,
      fixture.source,
      Buffer.alloc(33),
      { text, ignored: undefined },
      0,
    )
    expect(fixture.config()).toEqual({ text })
    const cyclic: Record<string, UntrustedInput> = {}
    cyclic.self = cyclic
    await expect(
      runNativeScreenshotInput(fixture.run, fixture.source, Buffer.alloc(33), cyclic, 0),
    ).rejects.toThrow('cyclic')
    let called = false
    class Custom {
      toJSON() {
        called = true
        return { text: 'not a native config' }
      }
    }
    await expect(
      runNativeScreenshotInput(
        fixture.run,
        fixture.source,
        Buffer.alloc(33),
        { custom: new Custom() },
        0,
      ),
    ).rejects.toThrow('plain protocol')
    expect(called).toBe(false)
    await expect(
      runNativeScreenshotInput(
        fixture.run,
        fixture.source,
        Buffer.alloc(33),
        { text: '\u0000'.repeat(200000) },
        0,
      ),
    ).rejects.toThrow('exceeds1MiB')
  })
  it('uses native watch expiry to cancel an already queued chunk without an SDK retry timer', async () => {
    const fixture = new InputFixture()
    const original = fixture.source.runContext
    const expired = new Error(
      'Native screenshot assertion original deadline expired during baseline input',
    )
    fixture.source.runContext = (action, params = {}, signal) => {
      if (params.action !== 'append') return original(action, params, signal)
      return new Promise((_resolve, reject) => {
        signal?.addEventListener('abort', () => reject(toError(signal.reason as UntrustedInput)), {
          once: true,
        })
        fixture.failWatch?.(expired)
      })
    }
    await expect(
      runNativeScreenshotInput(fixture.run, fixture.source, Buffer.alloc(33), {}, 120),
    ).rejects.toBe(expired)
    await new Promise((resolve) => setImmediate(resolve))
    expect(fixture.actions.find((a) => a.action === 'discard')).toMatchObject({
      pageId: 7,
    })
    expect(fixture.actions.some((a) => a.action === 'compareCommit')).toBe(false)
  })
  it('cleans a prepared native lease by its client nonce when cancellation loses the preparation reply', async () => {
    const fixture = new InputFixture()
    const controller = new AbortController()
    const run: NativeArtifactRunner = (_params, signal) =>
      new Promise((_resolve, reject) => {
        signal?.addEventListener('abort', () => reject(toError(signal.reason as UntrustedInput)), {
          once: true,
        })
        controller.abort(new Error('lost prepare reply'))
      })
    await expect(
      runNativeScreenshotInput(run, fixture.source, Buffer.alloc(33), {}, 0, controller.signal),
    ).rejects.toThrow('lost prepare reply')
    await new Promise((resolve) => setImmediate(resolve))
    expect(fixture.actions).toHaveLength(1)
    expect(fixture.actions[0]).toMatchObject({
      action: 'discard',
      pageId: 7,

      timeoutMs: 0,
    })
  })
  it('rejects staged host concurrency beyond16 and drains every accepted operation on cancellation', async () => {
    const fixture = new InputFixture()
    const controllers = Array.from({ length: 16 }, () => new AbortController())
    const run: NativeArtifactRunner = (_params, signal) =>
      new Promise((_resolve, reject) =>
        signal?.addEventListener('abort', () => reject(toError(signal.reason as UntrustedInput)), {
          once: true,
        }),
      )
    const pending = controllers.map((controller) =>
      runNativeScreenshotInput(run, fixture.source, Buffer.alloc(33), {}, 0, controller.signal),
    )
    const observed = Promise.allSettled(pending)
    await expect(
      runNativeScreenshotInput(fixture.run, fixture.source, Buffer.alloc(33), {}, 0),
    ).rejects.toThrow('staging capacity')
    for (const controller of controllers) controller.abort(new Error('bounded cleanup'))
    expect((await observed).every((item) => item.status === 'rejected')).toBe(true)
    await new Promise((resolve) => setImmediate(resolve))
    expect(fixture.actions.filter((a) => a.action === 'discard')).toHaveLength(16)
    await runNativeScreenshotInput(fixture.run, fixture.source, Buffer.alloc(33), {}, 0)
  })
  it('reports a late cleanup failure before the next admission on the same actual transport', async () => {
    const fixture = new InputFixture()
    const primary = new Error('prepare rejected')
    const cleanup = new Error('native closing')
    fixture.source.runContext = () => Promise.reject(cleanup)
    await expect(
      runNativeScreenshotInput(
        () => Promise.reject(primary),
        fixture.source,
        Buffer.alloc(33),
        {},
        0,
      ),
    ).rejects.toBe(primary)
    await new Promise((resolve) => setImmediate(resolve))
    const unrelated = new InputFixture()
    await runNativeScreenshotInput(unrelated.run, unrelated.source, Buffer.alloc(33), {}, 0)
    await expect(
      runNativeScreenshotInput(fixture.run, fixture.source, Buffer.alloc(33), {}, 0),
    ).rejects.toMatchObject({
      name: 'AggregateError',
      errors: [
        expect.objectContaining({
          errors: [primary, cleanup],
        }),
      ],
    })
    const recovered = new InputFixture()
    fixture.source.runContext = recovered.source.runContext
    await runNativeScreenshotInput(fixture.run, fixture.source, Buffer.alloc(33), {}, 0)
  })
  it('rejects absent/foreign actual page ownership and oversized config before native preparation', async () => {
    const fixture = new InputFixture()
    await expect(
      runNativeScreenshotInput(fixture.run, { pageId: 0 }, Buffer.alloc(33), {}, 0),
    ).rejects.toThrow('actual page')
    await expect(
      runNativeScreenshotInput(
        fixture.run,
        fixture.source,
        Buffer.alloc(33),
        { style: 'x'.repeat(1048576) },
        0,
      ),
    ).rejects.toThrow('exceeds1MiB')
    expect(fixture.actions).toHaveLength(0)
    await expect(
      runNativeScreenshotInput(
        () => Promise.resolve({ token: 'real', contextId: 3, pageId: 8 }),
        fixture.source,
        Buffer.alloc(33),
        {},
        0,
      ),
    ).rejects.toThrow('different page')
    expect(fixture.actions.filter((a) => a.action === 'discard')).toHaveLength(1)
  })
  it('retains the actual admitted comparison through caller abort until its native retirement reply', async () => {
    let entered!: () => void, release!: (error: Error) => void
    const started = new Promise<void>((resolve) => {
      entered = resolve
    })
    const fixture = new InputFixture(() => {
      entered()
      return new Promise((_resolve, reject) => {
        release = reject
      })
    })
    const controller = new AbortController(),
      reason = new Error('Actual screenshot owner abort')
    const operation = runNativeScreenshotInput(
      fixture.run,
      fixture.source,
      Buffer.alloc(33),
      {},
      0,
      controller.signal,
    )
    let settled = false
    const observed = operation.then(
      () => {
        settled = true
      },
      (error: UntrustedInput) => {
        settled = true
        expect(error).toBe(reason)
      },
    )
    await started
    controller.abort(reason)
    await Promise.resolve()
    expect(settled).toBe(false)
    expect(fixture.actions.some((value) => value.action === 'discard')).toBe(false)
    release(new Error('Actual native comparison cleanup completed'))
    await observed
    await new Promise((resolve) => setImmediate(resolve))
    expect(fixture.actions.filter((value) => value.action === 'discard')).toHaveLength(1)
  })
})
