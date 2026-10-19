// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  hasNativeAssertionDeadline,
  nativeAssertionDeadlineError,
  nativeAssertionDeadlineKind,
} from '../src/native-assertion-deadline.js'
import { type NativeBrowser } from '../src/native-browser.js'
import { NativeAssertionError, expectNative } from '../src/native-expect.js'
import {
  closeNativeMatcherInvocation,
  createNativeMatcherInvocation,
  getNativeAssertionDeadlineFailure,
  getNativeMatcherFailureKind,
  nativeMatcherAssertionDeadline,
  nativeMatcherFailure,
  runNativeMatcherInvocation,
  takeNativeMatcherFailure,
  type NativeMatcherFailureReceipt,
} from '../src/native-matcher-failure.js'
import { createNativePage } from '../src/native-page.js'
import {
  installNativeSoftAssertionBridge,
  runNativeSoftAssertion,
  type NativeSoftAssertionSettlement,
} from '../src/native-soft-assertions.js'
import { type UntrustedInput } from '../src/untrusted.js'

const fault = {
  type: 'native-assertion-deadline',
  version: 1,
  operationId: 7,
  generation: '19',
  matcher: 'toHaveText',
  deadlineMs: 40,
  observed: false,
}
const source = () =>
  nativeAssertionDeadlineError('Actual native assertion deadline', fault, {
    id: 7,
    matcher: 'toHaveText',
  })
function pageWithFailure(error: Error, packets: Record<string, UntrustedInput>[]) {
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
    runAutomation: (_command, params = {}) => {
      packets.push(params)
      return Promise.reject(error)
    },
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
  }
  return createNativePage(browser)
}
describe('native-owned assertion deadline receipts', () => {
  it('validates the exact pending native command/kind and no-observation schema', () => {
    expect(hasNativeAssertionDeadline(source(), 'toHaveText')).toBe(true)
    expect(() =>
      nativeAssertionDeadlineError('x', fault, { id: 8, matcher: 'toHaveText' }),
    ).toThrow('pending command')
    expect(() =>
      nativeAssertionDeadlineError('x', fault, { id: 7, matcher: 'toHaveValue' }),
    ).toThrow('pending command')
    for (const extra of [
      { observed: true },
      { generation: '0' },
      { deadlineMs: 0 },
      { actual: 'invented' },
    ])
      expect(() =>
        nativeAssertionDeadlineError('x', { ...fault, ...extra }, { id: 7, matcher: 'toHaveText' }),
      ).toThrow()
  })
  it('never recognizes timeout text, fabricated Errors or unsupported matcher families', () => {
    expect(hasNativeAssertionDeadline(new Error('Native operation timed out'), 'toHaveText')).toBe(
      false,
    )
    expect(() =>
      nativeMatcherAssertionDeadline(new Error('x'), new Error('native-assertion-deadline')),
    ).toThrow('source receipt')
    expect(nativeAssertionDeadlineKind('toHaveText', ['x'])).toBeUndefined()
    expect(nativeAssertionDeadlineKind('toHaveJSProperty', undefined)).toBeUndefined()
    expect(nativeAssertionDeadlineKind('toHaveAccessibleName', undefined)).toBeUndefined()
    expect(nativeAssertionDeadlineKind('toBeVisible', undefined)).toBe('toBeVisible')
  })
  it('retains the exact original Error and distinguishes mismatches from deadlines', () => {
    const ticket = createNativeMatcherInvocation(),
      error = new NativeAssertionError('No value admitted')
    runNativeMatcherInvocation(ticket, () => nativeMatcherAssertionDeadline(error, source()))
    const receipt = takeNativeMatcherFailure(ticket, error)
    expect(receipt).toBeDefined()
    expect(getNativeMatcherFailureKind(receipt!)).toBe('assertionDeadline')
    expect(getNativeAssertionDeadlineFailure(receipt!)).toBe(error)
    expect(takeNativeMatcherFailure(ticket, error)).toBeUndefined()
    const mismatch = runNativeMatcherInvocation(ticket, () =>
      nativeMatcherFailure(new Error('Actual mismatch')),
    )
    const mismatched = takeNativeMatcherFailure(ticket, mismatch)
    expect(getNativeMatcherFailureKind(mismatched!)).toBe('mismatch')
    expect(() => getNativeAssertionDeadlineFailure(mismatched!)).toThrow(
      'not an assertion deadline',
    )
    expect(() => getNativeAssertionDeadlineFailure({} as NativeMatcherFailureReceipt)).toThrow(
      'receipt',
    )
    closeNativeMatcherInvocation(ticket)
  })
  it('consumes only once and never transfers an issued receipt between invocation tickets', () => {
    const a = createNativeMatcherInvocation(),
      b = createNativeMatcherInvocation(),
      native = source(),
      error = new Error('real deadline')
    runNativeMatcherInvocation(a, () => nativeMatcherAssertionDeadline(error, native))
    expect(takeNativeMatcherFailure(b, error)).toBeUndefined()
    expect(() =>
      runNativeMatcherInvocation(b, () =>
        nativeMatcherAssertionDeadline(new Error('duplicate'), native),
      ),
    ).toThrow('source receipt')
    expect(getNativeAssertionDeadlineFailure(takeNativeMatcherFailure(a, error)!)).toBe(error)
    closeNativeMatcherInvocation(a)
    closeNativeMatcherInvocation(b)
  })
  it('keeps .not/custom messages without inventing Received/count/preview', async () => {
    const packets: Record<string, UntrustedInput>[] = [],
      page = pageWithFailure(source(), packets)
    const ticket = createNativeMatcherInvocation()
    const error = await runNativeMatcherInvocation(ticket, () =>
      expectNative(page.locator('#status'), 'Authored context')
        .not.toHaveText('expected', { timeout: 40 })
        .then(
          () => undefined,
          (value: UntrustedInput) => value,
        ),
    )
    expect(error).toBeInstanceOf(NativeAssertionError)
    expect((error as Error).message).toContain('Authored context')
    expect((error as Error).message).toContain('.not.toHaveText')
    expect((error as Error).message).toContain('without an admitted value')
    expect((error as Error).message).not.toMatch(/Received|count:|preview:/u)
    expect(packets[0]?.nativeAssertionKind).toBe('toHaveText')
    expect(getNativeAssertionDeadlineFailure(takeNativeMatcherFailure(ticket, error)!)).toBe(error)
    closeNativeMatcherInvocation(ticket)
  })
  it('settles the distinct receipt only after the actual native settlement ACK', async () => {
    let acknowledge!: () => void
    let settlement: NativeSoftAssertionSettlement | undefined
    installNativeSoftAssertionBridge(() =>
      Promise.resolve({
        signal: new AbortController().signal,
        timeoutMs: 40,
        settle: (value) => {
          settlement = value
          return new Promise<void>((resolve) => {
            acknowledge = resolve
          })
        },
      }),
    )
    const original = new NativeAssertionError('Actual native-owned deadline')
    let resolved = false
    const promise = runNativeSoftAssertion(() => {
      throw nativeMatcherAssertionDeadline(original, source())
    }).then(() => {
      resolved = true
    })
    await Promise.resolve()
    await Promise.resolve()
    await Promise.resolve()
    expect(settlement?.kind).toBe('assertionDeadline')
    if (settlement?.kind !== 'assertionDeadline')
      throw new Error('Actual settlement did not arrive')
    expect(getNativeAssertionDeadlineFailure(settlement.receipt)).toBe(original)
    expect(resolved).toBe(false)
    acknowledge()
    await promise
    expect(resolved).toBe(true)
  })
  it('leaves raw transport/abort/context/schema errors untagged', async () => {
    for (const message of [
      'Native operation timed out',
      'Actual abort',
      'Actual context lost',
      'Invalid native assertion observation',
    ]) {
      const ticket = createNativeMatcherInvocation(),
        page = pageWithFailure(new Error(message), [])
      const error = await runNativeMatcherInvocation(ticket, () =>
        expectNative(page.locator('p'))
          .toHaveText('x')
          .then(
            () => undefined,
            (value: UntrustedInput) => value,
          ),
      )
      expect(takeNativeMatcherFailure(ticket, error)).toBeUndefined()
      closeNativeMatcherInvocation(ticket)
    }
  })
})
