// SPDX-License-Identifier: MIT
import { beforeEach, describe, expect, it } from 'vitest'

import {
  hasNativeAssertionDeadline,
  nativeAssertionDeadlineError,
} from '../src/native-assertion-deadline.js'
import { type NativeBrowser } from '../src/native-browser.js'
import { expectNative } from '../src/native-expect.js'
import {
  getNativeAssertionDeadlineFailure,
  getNativeMatcherFailure,
  getNativeMatcherFailureKind,
} from '../src/native-matcher-failure.js'
import { createNativePage } from '../src/native-page.js'
import {
  consumeNativeScreenshotMismatch,
  nativeScreenshotResult,
} from '../src/native-screenshot-receipt.js'
import {
  installNativeSoftAssertionBridge,
  type NativeSoftAssertionSettlement,
} from '../src/native-soft-assertions.js'
import { type UntrustedInput } from '../src/untrusted.js'

import { InputFixture, png, result } from './native-screenshot-input-fixture.js'

const pending = { id: 7, inputToken: 'nativeScreenshotInput:1' }
function observed() {
  return {
    ...result(false),
    assertion: {
      type: 'native-screenshot-assertion',
      version: 1,
      operationId: 7,
      generation: '9',
      inputToken: pending.inputToken,
      deadlineMs: 40,
      outcome: 'observedMismatch',
      observed: true,
    },
  }
}
// This host is explicitly a controlled native transport. It proves producer
// receipt boundaries; live capture/restore/file ownership is verified separately.
function page(completion: () => Promise<UntrustedInput>) {
  const fixture = new InputFixture(completion)
  const browser = {
    ...fixture.source,
    state: 'running',
    width: 200,
    height: 120,
    url: 'about:blank',
    pixelFormat: 'bgra',
    framesPerSecond: 30,
    cefVersion: 'controlled transport',
    failure: new Promise<never>(() => undefined),
    closed: Promise.resolve(),
    navigate: () => Promise.reject(new Error('Unsupported controlled navigation')),
    sendDevToolsCommand: () => Promise.reject(new Error('Unsupported controlled DevTools')),
    onAudio: () => () => undefined,
    signal: new AbortController().signal,
    close: () => Promise.resolve(),
    onFrame: () => () => undefined,
    runAutomation: (
      _command: string,
      fields: Parameters<typeof fixture.run>[0],
      signal?: AbortSignal,
    ) => fixture.run(fields, signal),
  } as NativeBrowser
  return createNativePage(browser)
}
const rows: NativeSoftAssertionSettlement[] = []
installNativeSoftAssertionBridge(() =>
  Promise.resolve({
    signal: new AbortController().signal,
    timeoutMs: 100,
    settle: (row) => {
      rows.push(row)
      return Promise.resolve()
    },
  }),
)
beforeEach(() => {
  rows.length = 0
})
describe('native screenshot source observation receipts', () => {
  it('binds a genuine native response to the exact pending ID and prepared input, once', () => {
    const actual = nativeScreenshotResult(observed(), pending)
    expect(Object.isFrozen(actual)).toBe(true)
    expect(Object.isFrozen((actual as ReturnType<typeof observed>).assertion)).toBe(true)
    expect(consumeNativeScreenshotMismatch({ ...observed() })).toBe(false)
    expect(consumeNativeScreenshotMismatch(actual)).toBe(true)
    expect(consumeNativeScreenshotMismatch(actual)).toBe(false)
    expect(() => nativeScreenshotResult(observed(), { ...pending, id: 8 })).toThrow(
      'actual pending',
    )
    expect(() =>
      nativeScreenshotResult(observed(), { ...pending, inputToken: 'old-input' }),
    ).toThrow('actual pending')
  })
  it('rejects unobserved, inconsistent and missing comparison records before issuance', () => {
    const raw = observed()
    for (const value of [
      { ...raw, passed: true },
      { ...raw, actual: undefined },
      { ...raw, differentPixels: undefined },
      { ...raw, totalPixels: 0 },
      { ...raw, assertion: { ...raw.assertion, observed: false } },
      { ...raw, assertion: { ...raw.assertion, generation: '0' } },
    ])
      expect(() => nativeScreenshotResult(value, pending)).toThrow()
    expect(consumeNativeScreenshotMismatch(new Error('Native screenshot mismatch'))).toBe(false)
  })
  it('keeps fabricated parsed mismatch results hard while source-issued .not/custom failures soften', async () => {
    await expect(
      expectNative.soft(page(() => Promise.resolve(observed()))).toHaveScreenshot(png),
    ).rejects.toThrow('Different pixels')
    expect(rows[0]?.kind).toBe('otherFailure')
    await expectNative
      .soft(
        page(() => Promise.resolve(nativeScreenshotResult(observed(), pending))),
        'Actual authored context',
      )
      .not.toHaveScreenshot(png)
    const row = rows[1]
    expect(row?.kind).toBe('softFailure')
    if (row?.kind !== 'softFailure') throw new Error('Expected source-issued mismatch')
    expect(getNativeMatcherFailure(row.receipt).message).toMatch(
      /^Actual authored context\n\nexpect\(page\)\.not\./,
    )
  })
  it('keeps typed no-observation screenshot deadlines distinct and raw IO/abort errors hard', async () => {
    const fault = nativeAssertionDeadlineError(
      'No native pixel comparison admitted',
      {
        type: 'native-assertion-deadline',
        version: 1,
        operationId: 7,
        generation: '9',
        matcher: 'toHaveScreenshot',
        deadlineMs: 40,
        observed: false,
      },
      { id: 7, matcher: 'toHaveScreenshot' },
    )
    expect(hasNativeAssertionDeadline(fault, 'toHaveScreenshot')).toBe(true)
    await expectNative
      .soft(
        page(() => Promise.reject(fault)),
        'Actual expiry',
      )
      .toHaveScreenshot(png)
    const row = rows[0]
    expect(row?.kind).toBe('assertionDeadline')
    if (row?.kind !== 'assertionDeadline') throw new Error('Expected typed assertion deadline')
    expect(getNativeMatcherFailureKind(row.receipt)).toBe('assertionDeadline')
    expect(getNativeAssertionDeadlineFailure(row.receipt).message).toBe(
      'Actual expiry\n\nNo native pixel comparison admitted',
    )
    const io = new Error('Native screenshot file publication failed')
    await expect(
      expectNative.soft(page(() => Promise.reject(io))).toHaveScreenshot(png),
    ).rejects.toBe(io)
    expect(rows[1]).toEqual({ kind: 'otherFailure', error: io })
  })
})
