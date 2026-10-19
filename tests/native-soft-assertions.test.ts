// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { beforeEach, describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { expectNative, NativeAssertionError } from '../src/native-expect.js'
import { NativeAPIRequestContext, NativeAPIResponse } from '../src/native-http.js'
import {
  createNativeMatcherInvocation,
  closeNativeMatcherInvocation,
  runNativeMatcherInvocation,
  nativeMatcherFailure,
  takeNativeMatcherFailure,
  getNativeMatcherFailure,
  type NativeMatcherFailureReceipt,
} from '../src/native-matcher-failure.js'
import { createNativePage } from '../src/native-page.js'
import { nativeScreenshotResult } from '../src/native-screenshot-receipt.js'
import {
  installNativeSoftAssertionBridge,
  nativeSoftOperationOptions,
  runNativeSoftAssertion,
  type NativeSoftAssertionLease,
  type NativeSoftAssertionSettlement,
} from '../src/native-soft-assertions.js'
import { type NativeURLPredicateSnapshot } from '../src/native-url-predicate.js'
import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

import { InputFixture, png, result } from './native-screenshot-input-fixture.js'

let admit: (() => Promise<NativeSoftAssertionLease>) | undefined
installNativeSoftAssertionBridge(() => {
  if (!admit) throw new Error('No actual native callback scope')
  return admit()
})
function fixture(matches = false, actual: UntrustedInput = 'actual') {
  const runAutomation = vi.fn<
    (
      command: string,
      params?: Record<string, UntrustedInput>,
      signal?: AbortSignal,
    ) => Promise<UntrustedInput>
  >(() =>
    Promise.resolve({
      result: { value: { matches, count: 1, actual: createNativeValueCodec().encode(actual) } },
    }),
  )
  const browser: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: new AbortController().signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'controlled',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
  }
  return { browser, page: createNativePage(browser), runAutomation }
}
function lease(timeoutMs = 37) {
  const abort = new AbortController()
  const settlements: NativeSoftAssertionSettlement[] = []
  const settle = vi.fn((result: NativeSoftAssertionSettlement): Promise<void> => {
    settlements.push(result)
    return Promise.resolve()
  })
  const value: NativeSoftAssertionLease = { signal: abort.signal, timeoutMs, settle }
  admit = () => Promise.resolve(value)
  return { abort, value, settle, settlements }
}
async function responseFixture(status = 500, contentType = 'text/plain') {
  const test = fixture()
  test.runAutomation.mockImplementation((_command, params, signal) => {
    signal?.throwIfAborted()
    if (params?.action === 'create')
      return Promise.resolve({ apiContextId: 1, browserContextId: 1 })
    if (params?.action === 'fetch' || params?.action === 'fetchMultipart')
      return Promise.resolve({
        responseId: 'issued-response',
        apiContextId: 1,
        url: 'http://native-response.test/',
        status,
        statusText: status === 200 ? 'OK' : 'Error',
        method: 'GET',
        headers: [{ name: 'Content-Type', value: contentType }],
      })
    if (params?.action === 'body')
      return Promise.resolve({ body: Buffer.from('actual native body').toString('base64') })
    return Promise.resolve({})
  })
  const context = new NativeAPIRequestContext(test.page.source)
  const response = await context.get('http://native-response.test/')
  return { ...test, context, response }
}
function deferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (error: UntrustedInput) => void
  const promise = new Promise<T>((yes, no) => {
    resolve = yes
    reject = no
  })
  return { promise, resolve, reject }
}
function urlCallbackFixture() {
  const test = fixture(),
    listeners = new Set<(value: NativeURLPredicateSnapshot) => void>()
  const replies = new Map<string, ReturnType<typeof deferred<UntrustedInput>>>()
  Object.assign(test.browser, {
    onURLPredicate: (listener: (value: NativeURLPredicateSnapshot) => void) => {
      listeners.add(listener)
      return () => {
        listeners.delete(listener)
      }
    },
  })
  test.runAutomation.mockImplementation((command, params, signal) => {
    if (command === 'urlPredicate') {
      if (typeof params?.clientToken !== 'string') throw Error('Missing real client token')
      const reply = deferred<UntrustedInput>()
      replies.set(params.clientToken, reply)
      const abort = () => reply.reject(signal?.reason as UntrustedInput)
      signal?.addEventListener('abort', abort, { once: true })
      for (const listener of listeners)
        listener({
          id: 1,
          generation: 1,
          token: 1,
          clientToken: params.clientToken,
          url: 'https://actual.test/path',
        })
      return reply.promise.finally(() => signal?.removeEventListener('abort', abort))
    }
    if (command === 'continueURLPredicate') {
      if (typeof params?.clientToken !== 'string') throw Error('Missing continuation token')
      replies
        .get(params.clientToken)
        ?.resolve({ matches: params.matches, url: 'https://actual.test/path' })
      return Promise.resolve({})
    }
    return Promise.resolve({
      result: {
        value: { matches: false, count: 1, actual: createNativeValueCodec().encode('actual') },
      },
    })
  })
  return { ...test, listeners }
}
beforeEach(() => {
  admit = undefined
})

describe('native soft matcher author bridge (controlled transport, no native outcome simulation)', () => {
  it('rejects outside actual callback before any native matcher dispatch', async () => {
    const test = fixture()
    await expect(expectNative.soft(test.page.locator('div')).toHaveText('wanted')).rejects.toThrow(
      'No actual native callback scope',
    )
    expect(test.runAutomation).not.toHaveBeenCalled()
  })
  it('waits for native begin ACK and forwards native budget/signal to actual transport', async () => {
    const test = fixture(true),
      owner = lease(),
      ack = deferred<NativeSoftAssertionLease>()
    admit = () => ack.promise
    const operation = expectNative
      .soft(test.page.locator('div'))
      .toHaveText('actual', { timeout: 0 })
    expect(test.runAutomation).not.toHaveBeenCalled()
    ack.resolve(owner.value)
    await operation
    expect(test.runAutomation).toHaveBeenCalledOnce()
    const call = test.runAutomation.mock.calls[0]
    expect(call?.[0]).toBe('queryEvaluate')
    expect(call?.[1]?.timeoutMs).toBe(37)
    const signal = call?.[2]
    expect(signal).toBeInstanceOf(AbortSignal)
    owner.abort.abort(new Error('actual native owner abort'))
    expect(signal?.reason).toBe(owner.abort.signal.reason)
    expect(owner.settlements).toEqual([{ kind: 'passed' }])
  })
  it('native begin rejection preserves exact error and never dispatches matcher', async () => {
    const test = fixture(),
      failure = new Error('real native begin denial')
    admit = () => Promise.reject(failure)
    await expect(expectNative.soft(test.page.locator('div')).toBeVisible()).rejects.toBe(failure)
    expect(test.runAutomation).not.toHaveBeenCalled()
  })
  it('retains a source-issued actual mismatch Error and waits for its settlement ACK', async () => {
    const test = fixture(),
      owner = lease(),
      ack = deferred<void>()
    owner.settle.mockImplementation((result) => {
      owner.settlements.push(result)
      return ack.promise
    })
    let continued = false
    const operation = expectNative
      .soft(test.page.locator('div'))
      .toHaveText('wanted')
      .then(() => {
        continued = true
      })
    await vi.waitFor(() => expect(owner.settlements).toHaveLength(1))
    const result = owner.settlements[0]
    if (result?.kind !== 'softFailure') throw new Error('Expected exact matcher failure receipt')
    const error = getNativeMatcherFailure(result.receipt)
    expect(error).toBeInstanceOf(NativeAssertionError)
    expect(error.message).toContain('Received: "actual"')
    expect(error.stack).toContain('NativeAssertionError')
    expect(getNativeMatcherFailure(result.receipt)).toBe(error)
    expect(continued).toBe(false)
    ack.resolve()
    await operation
    expect(continued).toBe(true)
    expect(owner.settle).toHaveBeenCalledOnce()
  })
  it('preserves negation and passes without journaling any failure', async () => {
    const test = fixture(false),
      owner = lease(0)
    await expectNative.soft(test.page.locator('div')).not.toBeVisible({ timeout: 19 })
    expect(test.runAutomation.mock.calls[0]?.[1]?.timeoutMs).toBe(19)
    expect(owner.settlements).toEqual([{ kind: 'passed' }])
  })
  it('settlement rejection is hard and is never submitted again as soft', async () => {
    const test = fixture(),
      owner = lease(),
      denied = new Error('actual late native settlement denial')
    owner.settle.mockRejectedValue(denied)
    await expect(expectNative.soft(test.page.locator('div')).toBeVisible()).rejects.toBe(denied)
    expect(owner.settle).toHaveBeenCalledOnce()
  })
  it('argument validation remains hard with the same Error object', async () => {
    const test = fixture(),
      owner = lease()
    const error = await expectNative
      .soft(test.page)
      .toHaveURL(null as UntrustedInput as string)
      .catch((value: UntrustedInput) => value)
    expect(error).toBeInstanceOf(NativeAssertionError)
    expect(owner.settlements).toEqual([{ kind: 'otherFailure', error }])
    expect(test.runAutomation).not.toHaveBeenCalled()
  })
  it('native operation errors remain hard instead of becoming fake mismatches', async () => {
    const test = fixture(),
      owner = lease()
    test.runAutomation.mockRejectedValueOnce(new Error('real transport failure'))
    const error = await expectNative
      .soft(test.page.locator('div'))
      .toHaveText('wanted')
      .catch((value: UntrustedInput) => value)
    expect(error).toBeInstanceOf(NativeAssertionError)
    expect(owner.settlements).toEqual([{ kind: 'otherFailure', error }])
  })
  it('aborted native admission settles actual raw reason without matcher invocation', async () => {
    const test = fixture(),
      owner = lease(),
      reason = new Error('real canceled native owner')
    owner.abort.abort(reason)
    await expect(expectNative.soft(test.page.locator('div')).toHaveText('wanted')).rejects.toBe(
      reason,
    )
    expect(owner.settlements).toEqual([{ kind: 'otherFailure', error: reason }])
    expect(test.runAutomation).not.toHaveBeenCalled()
  })
  it('preserves raw author values and rejects caller-created or prior-invocation Errors', async () => {
    const owner = lease(),
      forged = new NativeAssertionError('author-created class instance')
    await expect(runNativeSoftAssertion(() => Promise.reject(forged))).rejects.toBe(forged)
    const test = fixture()
    await expectNative.soft(test.page.locator('div')).toHaveText('wanted')
    const actual = owner.settlements[1]
    if (actual?.kind !== 'softFailure') throw new Error('Expected issued mismatch')
    const old = getNativeMatcherFailure(actual.receipt)
    await expect(runNativeSoftAssertion(() => Promise.reject(old))).rejects.toBe(old)
    // A genuine author-thrown primitive must remain the exact rejected value.
    // eslint-disable-next-line @typescript-eslint/prefer-promise-reject-errors
    await expect(runNativeSoftAssertion(() => Promise.reject('unchanged primitive'))).rejects.toBe(
      'unchanged primitive',
    )
    expect(owner.settlements[0]).toEqual({ kind: 'otherFailure', error: forged })
    expect(owner.settlements[2]).toEqual({ kind: 'otherFailure', error: old })
    expect(owner.settlements[3]).toEqual({ kind: 'otherFailure', error: 'unchanged primitive' })
  })
  it('marks only actual ARIA mismatch while preserving its original Error class', async () => {
    const test = fixture(false, '- button "actual"'),
      owner = lease()
    await expectNative
      .soft(test.page.locator('body'), 'Actual ARIA context')
      .toMatchAriaSnapshot('- button "wanted"', { timeout: 100 })
    const result = owner.settlements[0]
    if (result?.kind !== 'softFailure') throw new Error('Expected actual ARIA mismatch')
    const error = getNativeMatcherFailure(result.receipt)
    expect(error.constructor).toBe(Error)
    expect(error.message).toMatch(/^Actual ARIA context\n\nexpect/)
    expect(error.message).toContain('toMatchAriaSnapshot')
    expect(test.runAutomation.mock.calls[0]?.[1]?.timeoutMs).toBe(37)
    expect(test.runAutomation.mock.calls[0]?.[2]).toBeInstanceOf(AbortSignal)
  })
  it('covers native URL predicate mismatch with the same admitted budget and signal', async () => {
    const test = fixture(),
      owner = lease(),
      listeners = new Set<(value: NativeURLPredicateSnapshot) => void>()
    Object.assign(test.browser, {
      onURLPredicate: (listener: (value: NativeURLPredicateSnapshot) => void) => {
        listeners.add(listener)
        return () => {
          listeners.delete(listener)
        }
      },
    })
    test.runAutomation.mockImplementation((command, params, signal) => {
      if (command === 'urlPredicate') {
        if (typeof params?.clientToken !== 'string') throw new Error('Missing real client token')
        expect(params.timeoutMs).toBe(37)
        expect(signal).toBeInstanceOf(AbortSignal)
        for (const listener of listeners)
          listener({
            id: 1,
            generation: 1,
            token: 1,
            clientToken: params.clientToken,
            url: 'https://actual.test/path',
          })
        return Promise.resolve({ matches: false, url: 'https://actual.test/path' })
      }
      return Promise.resolve({})
    })
    const predicate = vi.fn(() => false)
    await expectNative.soft(test.page, 'Actual URL context').toHaveURL(predicate, { timeout: 900 })
    expect(predicate).toHaveBeenCalledWith(new URL('https://actual.test/path'))
    const settled = owner.settlements[0]
    if (settled?.kind !== 'softFailure') throw new Error('Expected genuine URL mismatch receipt')
    expect(getNativeMatcherFailure(settled.receipt)).toBeInstanceOf(NativeAssertionError)
    expect(getNativeMatcherFailure(settled.receipt).message).toMatch(
      /^Actual URL context\n\nexpect/,
    )
    expect(listeners.size).toBe(0)
  })
  it('does not soften or clamp a hard assertion inside an asynchronous authored URL callback', async () => {
    const test = urlCallbackFixture(),
      owner = lease()
    let original: UntrustedInput
    const error = await expectNative
      .soft(test.page)
      .toHaveURL(async () => {
        await Promise.resolve()
        try {
          await expectNative(test.page.locator('div')).toHaveText('wanted', { timeout: 901 })
        } catch (value) {
          original = value as UntrustedInput
          throw value
        }
        return false
      })
      .catch((value: UntrustedInput) => value)
    expect(error).toBeInstanceOf(NativeAssertionError)
    expect(error).toBe(original)
    expect(owner.settlements).toEqual([{ kind: 'otherFailure', error: original }])
    expect(
      test.runAutomation.mock.calls.find((call) => call[0] === 'queryEvaluate')?.[1]?.timeoutMs,
    ).toBe(901)
    const nestedSignal = test.runAutomation.mock.calls.find(
      (call) => call[0] === 'queryEvaluate',
    )?.[2]
    expect(nestedSignal).toBeInstanceOf(AbortSignal) // Existing page lifetime remains real.
    owner.abort.abort(new Error('outer soft lease retired'))
    expect(nestedSignal?.aborted).toBe(false)
    expect(test.listeners.size).toBe(0)
  })
  it('keeps concurrent soft tickets independent while authored callback continuations remain hard', async () => {
    const test = urlCallbackFixture(),
      callbackStarted = deferred<void>(),
      resume = deferred<void>()
    const outer = lease(),
      concurrent = lease(83)
    let began = 0,
      original: UntrustedInput
    admit = () => Promise.resolve(began++ === 0 ? outer.value : concurrent.value)
    const pending = expectNative
      .soft(test.page)
      .toHaveURL(async () => {
        callbackStarted.resolve()
        await resume.promise
        try {
          await expectNative(test.page.locator('div')).toHaveText('wanted', { timeout: 902 })
        } catch (value) {
          original = value as UntrustedInput
          throw value
        }
        return false
      })
      .catch((value: UntrustedInput) => value)
    await callbackStarted.promise
    await expectNative.soft(test.page.locator('div')).toHaveText('wanted')
    resume.resolve()
    expect(await pending).toBe(original)
    expect(outer.settlements).toEqual([{ kind: 'otherFailure', error: original }])
    const concurrentResult = concurrent.settlements[0]
    if (concurrentResult?.kind !== 'softFailure')
      throw Error('Missing concurrent original mismatch receipt')
    expect(getNativeMatcherFailure(concurrentResult.receipt)).not.toBe(original)
    const calls = test.runAutomation.mock.calls.filter((call) => call[0] === 'queryEvaluate')
    expect(calls.map((call) => call[1]?.timeoutMs)).toEqual([83, 902])
  })
  it('admits explicit nested soft assertions separately and restores the outer false-predicate mismatch context', async () => {
    const test = urlCallbackFixture(),
      outer = lease(),
      inner = lease(89)
    let began = 0
    admit = () => Promise.resolve(began++ === 0 ? outer.value : inner.value)
    await expectNative.soft(test.page).toHaveURL(async () => {
      await Promise.resolve()
      await expectNative.soft(test.page.locator('div')).toHaveText('wanted')
      return false
    })
    const outerResult = outer.settlements[0],
      innerResult = inner.settlements[0]
    if (outerResult?.kind !== 'softFailure' || innerResult?.kind !== 'softFailure')
      throw Error('Missing separately admitted native mismatch receipts')
    expect(getNativeMatcherFailure(outerResult.receipt).message).toContain('toHaveURL')
    expect(getNativeMatcherFailure(innerResult.receipt).message).toContain('toHaveText')
    expect(getNativeMatcherFailure(outerResult.receipt)).not.toBe(
      getNativeMatcherFailure(innerResult.receipt),
    )
    expect(
      test.runAutomation.mock.calls.find((call) => call[0] === 'queryEvaluate')?.[1]?.timeoutMs,
    ).toBe(89)
  })
  it('covers the real prepared screenshot producer with native budget and mismatch identity', async () => {
    const test = fixture(),
      owner = lease(),
      input = new InputFixture(() =>
        Promise.resolve(
          nativeScreenshotResult(
            {
              ...result(false),
              assertion: {
                type: 'native-screenshot-assertion',
                version: 1,
                operationId: 7,
                generation: '9',
                inputToken: 'nativeScreenshotInput:1',
                deadlineMs: 0,
                outcome: 'observedMismatch',
                observed: true,
              },
            },
            { id: 7, inputToken: 'nativeScreenshotInput:1' },
          ),
        ),
      )
    Object.assign(test.browser, input.source)
    test.runAutomation.mockImplementation((_command, params, signal) =>
      input.run(params ?? {}, signal),
    )
    await expectNative
      .soft(test.page, 'Actual screenshot context')
      .toHaveScreenshot(png, { timeout: 0 })
    expect(input.actions[0]).toMatchObject({ action: 'comparePrepare', timeoutMs: 37 })
    const settled = owner.settlements[0]
    if (settled?.kind !== 'softFailure')
      throw new Error('Expected actual screenshot mismatch receipt')
    expect(getNativeMatcherFailure(settled.receipt)).toBeInstanceOf(NativeAssertionError)
    expect(getNativeMatcherFailure(settled.receipt).message).toMatch(
      /^Actual screenshot context\n\nexpect/,
    )
    expect(getNativeMatcherFailure(settled.receipt).message).toContain('Different pixels: 1/1')
  })
  it('native lease cancellation reaches actual screenshot input dispatch as the same raw error', async () => {
    const test = fixture(),
      owner = lease(),
      input = new InputFixture(),
      reason = new Error('native owner expired at file admission')
    Object.assign(test.browser, input.source)
    test.runAutomation.mockImplementation((_command, params, signal) => {
      if (params?.action === 'comparePrepare') owner.abort.abort(reason)
      return input.run(params ?? {}, signal)
    })
    await expect(expectNative.soft(test.page).toHaveScreenshot(png)).rejects.toBe(reason)
    expect(owner.settlements).toEqual([{ kind: 'otherFailure', error: reason }])
  })
  it('explicitly rejects response soft diagnostics before invoking body or native admission', () => {
    const test = fixture(),
      owner = lease(),
      context = new NativeAPIRequestContext(test.page.source)
    const response = new NativeAPIResponse(context, {
      responseId: 'controlled-response',
      apiContextId: 1,
      url: 'http://controlled/',
      status: 500,
      statusText: 'Error',
      method: 'GET',
      headers: [],
    })
    const body = vi.spyOn(response, 'text')
    expect(() => {
      Reflect.apply(expectNative.soft, undefined, [response])
    }).toThrow('actual native HTTP dispatch result')
    expect(body).not.toHaveBeenCalled()
    expect(test.runAutomation).not.toHaveBeenCalled()
    expect(owner.settle).not.toHaveBeenCalled()
  })
  it('uses only originally issued HTTP status and a fresh lease-bound body for soft response mismatch', async () => {
    const test = await responseFixture(),
      owner = lease()
    await test.response.body() // Public cached bytes belong to a different call.
    const publicBody = vi
      .spyOn(test.response, 'text')
      .mockRejectedValue(new Error('must not read public body cache'))
    for (const getter of ['ok', 'status', 'statusText', 'url', 'requestMethod', 'headers'] as const)
      vi.spyOn(test.response, getter).mockImplementation(() => {
        throw Error('must not trust overridden response metadata')
      })
    await expectNative.soft(test.response, { message: 'Response context' }).toBeOK()
    const bodyCalls = test.runAutomation.mock.calls.filter((call) => call[1]?.action === 'body')
    expect(bodyCalls).toHaveLength(2)
    expect(bodyCalls[1]?.[1]).toMatchObject({
      timeoutMs: 37,
      responseId: 'issued-response',
      apiContextId: 1,
    })
    expect(bodyCalls[1]?.[2]).toBeInstanceOf(AbortSignal)
    expect(publicBody).not.toHaveBeenCalled()
    const settled = owner.settlements[0]
    if (settled?.kind !== 'softFailure') throw Error('Missing native response mismatch receipt')
    const error = getNativeMatcherFailure(settled.receipt)
    expect(error).toBeInstanceOf(NativeAssertionError)
    expect(error.message).toContain('Response context\n\nexpect(response).toBeOK() failed')
    expect(error.message).toContain('← 500 Error')
    expect(error.message).toContain('Response text:\nactual native body')
    expect(getNativeMatcherFailure(settled.receipt)).toBe(error)
  })
  it('preserves response negation and passes native statuses without unnecessary body reads', async () => {
    for (const [status, isNot] of [
      [200, false],
      [500, true],
    ] as const) {
      const test = await responseFixture(status),
        owner = lease()
      const assertion = expectNative.soft(test.response, 'not emitted')
      await (isNot ? assertion.not : assertion).toBeOK()
      expect(owner.settlements).toEqual([{ kind: 'passed' }])
      expect(
        test.runAutomation.mock.calls.filter((call) => call[1]?.action === 'body'),
      ).toHaveLength(0)
    }
    const failed = await responseFixture(200, 'image/png'),
      owner = lease()
    await expectNative.soft(failed.response, 'Negated context').not.toBeOK()
    const result = owner.settlements[0]
    if (result?.kind !== 'softFailure') throw Error('Missing negated native response mismatch')
    expect(getNativeMatcherFailure(result.receipt).message).toContain(
      'Negated context\n\nexpect(response).not.toBeOK() failed',
    )
    expect(
      failed.runAutomation.mock.calls.filter((call) => call[1]?.action === 'body'),
    ).toHaveLength(0)
  })
  it('keeps native HTTP body transport, schema and disposal errors hard and unprefixed', async () => {
    for (const mode of ['transport', 'schema', 'disposed', 'context-closed'] as const) {
      const test = await responseFixture(),
        owner = lease()
      const assertion = expectNative.soft(test.response, 'MISMATCH_ONLY_CONTEXT')
      const original = new Error('actual native HTTP body transport failure')
      if (mode === 'transport') test.runAutomation.mockRejectedValueOnce(original)
      if (mode === 'schema') test.runAutomation.mockResolvedValueOnce({ body: false })
      if (mode === 'disposed') await test.response.dispose()
      if (mode === 'context-closed') test.context.markContextClosed('actual context closure')
      const error = await assertion.toBeOK().catch((value: UntrustedInput) => value)
      expect(error).toBeInstanceOf(Error)
      if (mode === 'transport') expect(error).toBe(original)
      expect((error as Error).message).not.toContain('MISMATCH_ONLY_CONTEXT')
      expect(owner.settlements).toEqual([{ kind: 'otherFailure', error }])
    }
  })
  it('forwards native soft cancellation into its own body command without poisoning cached public bytes', async () => {
    const test = await responseFixture(),
      cached = await test.response.body(),
      owner = lease()
    const original = new Error('actual native soft response cancellation')
    test.runAutomation.mockImplementationOnce((_command, params, signal) => {
      expect(params?.action).toBe('body')
      expect(params?.timeoutMs).toBe(37)
      owner.abort.abort(original)
      expect(signal?.reason).toBe(original)
      return Promise.reject(original)
    })
    await expect(expectNative.soft(test.response).toBeOK()).rejects.toBe(original)
    expect(owner.settlements).toEqual([{ kind: 'otherFailure', error: original }])
    expect(await test.response.body()).toBe(cached)
  })
  it('validates custom message strings before dispatch or native admission', () => {
    const test = fixture()
    for (const invalid of [{ message: 42 }, { message: 'valid', extra: true }, {}])
      expect(() => {
        Reflect.apply(expectNative.soft, undefined, [test.page, invalid])
      }).toThrow()
    expect(() => {
      Reflect.apply(expectNative.soft, undefined, [test.page, 'message', 'extra'])
    }).toThrow('target and optional message')
    expect(test.runAutomation).not.toHaveBeenCalled()
  })
  it('constructs soft custom messages on the exact source-issued mismatch Error including .not', async () => {
    for (const isNot of [false, true]) {
      const test = fixture(isNot),
        owner = lease()
      const message = 'Author context: \ud800\nsecond line'
      const assertion = expectNative.soft(test.page.locator('div'), message)
      await (isNot ? assertion.not : assertion).toHaveText('wanted')
      const settled = owner.settlements[0]
      if (settled?.kind !== 'softFailure') throw Error('Missing actual mismatch receipt')
      const original = getNativeMatcherFailure(settled.receipt)
      expect(original).toBeInstanceOf(NativeAssertionError)
      expect(original.message).toMatch(/^Author context:/)
      expect(original.message).toContain(
        `${message}\n\nexpect(locator).${isNot ? 'not.' : ''}toHaveText`,
      )
      expect(original.message).toContain('"wanted"')
      expect(original.message).toContain('"actual"')
      expect(getNativeMatcherFailure(settled.receipt)).toBe(original)
      expect(test.runAutomation.mock.calls[0]?.[1]?.timeoutMs).toBe(37)
      expect(JSON.stringify(test.runAutomation.mock.calls[0]?.[1])).not.toContain('Author context')
    }
  })
  it('accepts the object form once without changing the original native operation or .not receipt', async () => {
    const test = fixture(true),
      owner = lease()
    const context = { message: 'Object context' }
    const assertion = expectNative.soft(test.page.locator('div'), context).not
    context.message = 'Later author change'
    await assertion.toHaveText('wanted')
    const settled = owner.settlements[0]
    if (settled?.kind !== 'softFailure') throw Error('Missing actual mismatch receipt')
    const original = getNativeMatcherFailure(settled.receipt)
    expect(original.message).toMatch(/^Object context\n\nexpect\(locator\)\.not\./)
    expect(original.message).toContain('"actual"')
    expect(original.message).not.toContain('Later author change')
    expect(getNativeMatcherFailure(settled.receipt)).toBe(original)
    expect(test.runAutomation.mock.calls[0]?.[1]?.timeoutMs).toBe(37)
  })
  it('does not prefix or soften raw transport and invalid-argument Errors with custom context', async () => {
    const test = fixture(),
      owner = lease()
    test.runAutomation.mockRejectedValueOnce(new Error('raw native cancellation'))
    const error = await expectNative
      .soft(test.page.locator('div'), 'CUSTOM_ONLY_MISMATCH')
      .toHaveText('wanted')
      .catch((value: UntrustedInput) => value)
    expect(error).toBeInstanceOf(NativeAssertionError)
    if (!(error instanceof Error)) throw Error('Missing genuine matcher cause Error')
    expect(error.message).toContain('raw native cancellation')
    expect(error.message).not.toContain('CUSTOM_ONLY_MISMATCH')
    expect(error.message).not.toContain('Received:')
    expect(owner.settlements[0]).toEqual({ kind: 'otherFailure', error })
    const invalid = lease()
    const badArgument = await expectNative
      .soft(test.page, 'CUSTOM_ONLY_MISMATCH')
      .toHaveURL(null as UntrustedInput as string)
      .catch((value: UntrustedInput) => value)
    expect(badArgument).toBeInstanceOf(NativeAssertionError)
    expect((badArgument as Error).message).not.toContain('CUSTOM_ONLY_MISMATCH')
    expect(invalid.settlements[0]).toEqual({ kind: 'otherFailure', error: badArgument })
  })
  it('ordinary hard assertion behavior is unchanged and check is not a soft method', async () => {
    const test = fixture(),
      owner = lease()
    await expect(
      expectNative(test.page.locator('div')).toHaveText('wanted', { timeout: 913 }),
    ).rejects.toBeInstanceOf(NativeAssertionError)
    expect(test.runAutomation.mock.calls[0]?.[1]?.timeoutMs).toBe(913)
    expect(owner.settlements).toEqual([])
    expect(Object.hasOwn(expectNative.soft(test.page.locator('div')), 'check')).toBe(false)
  })
})

describe('matcher failure identity controls', () => {
  it('rejects forged, unconsumed, wrong-ticket and duplicate consumption receipts', () => {
    const ticket = createNativeMatcherInvocation(),
      other = createNativeMatcherInvocation()
    const error = runNativeMatcherInvocation(ticket, () =>
      nativeMatcherFailure(new Error('actual failure')),
    )
    expect(takeNativeMatcherFailure(other, error)).toBeUndefined()
    expect(() => getNativeMatcherFailure({} as NativeMatcherFailureReceipt)).toThrow(TypeError)
    const receipt = takeNativeMatcherFailure(ticket, error)
    if (!receipt) throw new Error('Expected actual issued receipt')
    expect(getNativeMatcherFailure(receipt)).toBe(error)
    expect(takeNativeMatcherFailure(ticket, error)).toBeUndefined()
    closeNativeMatcherInvocation(ticket)
    closeNativeMatcherInvocation(other)
  })
  it('cannot mark a late descendant after the actual invocation closed', async () => {
    const ticket = createNativeMatcherInvocation(),
      held = deferred<void>()
    const pending = runNativeMatcherInvocation(ticket, async () => {
      await held.promise
      return nativeMatcherFailure(new Error('too late'))
    })
    closeNativeMatcherInvocation(ticket)
    held.resolve()
    const error = await pending
    expect(takeNativeMatcherFailure(ticket, error)).toBeUndefined()
  })
  it('does not retain the native budget in detached work after actual matcher settlement', async () => {
    const owner = lease(),
      held = deferred<void>()
    let detached: Promise<ReturnType<typeof nativeSoftOperationOptions>> | undefined
    await runNativeSoftAssertion(() => {
      detached = held.promise.then(() => nativeSoftOperationOptions(1000))
      expect(nativeSoftOperationOptions(1000)?.timeout).toBe(37)
      return Promise.resolve()
    })
    held.resolve()
    expect(await detached).toBeUndefined()
    expect(owner.settlements).toEqual([{ kind: 'passed' }])
  })
})
