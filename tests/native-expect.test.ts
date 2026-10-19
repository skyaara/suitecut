import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativeMatchers, expectNative } from '../src/native-expect.js'
import { NativeAPIRequestContext, NativeAPIResponse } from '../src/native-http.js'
import { createNativePage } from '../src/native-page.js'
import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture(
  matches: boolean,
  actual: UntrustedInput = 'observed',
  observation: { count?: number; preview?: string } = {},
  closedReason?: string,
) {
  const runAutomation = vi.fn((): Promise<UntrustedInput> =>
    Promise.resolve({
      ...(closedReason === undefined ? {} : { closedReason }),
      result: {
        value: { matches, actual: createNativeValueCodec().encode(actual), ...observation },
      },
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
    cefVersion: 'test',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
  }
  return { page: createNativePage(browser), runAutomation }
}

describe('authored native assertion adapter', () => {
  it('constructs optional author messages only around genuine hard mismatches and preserves .not', async () => {
    for (const isNot of [false, true]) {
      const test = fixture(isNot, 'observed', { count: 1 })
      const assertion = expectNative(test.page.locator('p'), 'Author context')
      const error = await (isNot ? assertion.not : assertion)
        .toHaveText('wanted')
        .catch((value: UntrustedInput) => value)
      if (!(error instanceof Error)) throw new Error('Expected actual mismatch Error')
      expect(error.message).toMatch(/^Author context\n\nexpect\(locator\)/)
      expect(error.message).toContain(`${isNot ? 'not.' : ''}toHaveText`)
      expect(error.message).toContain('"observed"')
      expect(error.message).toContain('"wanted"')
      expect(JSON.stringify(test.runAutomation.mock.calls[0])).not.toContain('Author context')
    }
  })
  it('passes author messages through page-title delegation without altering successful matchers', async () => {
    const failed = fixture(false)
    await expect(expectNative(failed.page, 'Page context').toHaveTitle('wanted')).rejects.toThrow(
      'Page context\n\nexpect(page).toHaveTitle',
    )
    const passed = fixture(true)
    await expectNative(passed.page, 'Never emitted').toHaveTitle('observed')
  })
  it('accepts the object message form for genuine hard mismatch and validates it before dispatch', async () => {
    const failed = fixture(true)
    await expect(
      expectNative(failed.page, { message: 'Object page context' }).not.toHaveTitle('observed'),
    ).rejects.toThrow('Object page context\n\nexpect(page).not.toHaveTitle')
    const never = fixture(false)
    for (const invalid of [{ message: 42 }, { message: 'valid', extra: true }, {}])
      expect(() => {
        Reflect.apply(expectNative, undefined, [never.page, invalid])
      }).toThrow()
    expect(never.runAutomation).not.toHaveBeenCalled()
  })
  it('does not prefix native transport, raw exception or invalid argument failures', async () => {
    const failed = fixture(false)
    failed.runAutomation.mockRejectedValueOnce(new Error('actual transport failure'))
    const raw = await expectNative(failed.page.locator('p'), 'NEVER_PREFIX')
      .toHaveText('wanted')
      .catch((value: UntrustedInput) => value)
    if (!(raw instanceof Error)) throw new Error('Missing real transport cause')
    expect(raw.message).toContain('actual transport failure')
    expect(raw.message).not.toContain('NEVER_PREFIX')
    expect(raw.message).not.toContain('Received:')
    failed.runAutomation.mockResolvedValueOnce({
      exceptionDetails: { text: 'Actual engine exception' },
    })
    const exception = await expectNative(failed.page, 'NEVER_PREFIX')
      .toHaveTitle('wanted')
      .catch((value: UntrustedInput) => value)
    expect((exception as Error).message).toContain('Actual engine exception')
    expect((exception as Error).message).not.toContain('NEVER_PREFIX')
    const invalid = await expectNative(failed.page, 'NEVER_PREFIX')
      .toHaveURL(null as UntrustedInput as string)
      .catch((value: UntrustedInput) => value)
    expect((invalid as Error).message).not.toContain('NEVER_PREFIX')
    expect(() => {
      Reflect.apply(expectNative, undefined, [failed.page, 42])
    }).toThrow()
  })
  it('keeps genuine last observation on closure and never invents a Received value when no observation exists', async () => {
    const retained = fixture(
      false,
      'actual prior value',
      { count: 1 },
      'Native page is closed: actual reason',
    )
    const error = await expectNative(retained.page.locator('div'))
      .toHaveText('wanted')
      .catch((value) => value as Error)
    if (!(error instanceof Error)) throw new Error('Expected retained closure failure')
    expect(error.message).toContain('Received: "actual prior value"')
    expect(error.message).toContain('actual reason')
    const immediate = fixture(false)
    immediate.runAutomation.mockRejectedValueOnce(
      new Error('Native page is closed: actual caller reason'),
    )
    const noValue = await expectNative(immediate.page.locator('div'))
      .toHaveText('wanted')
      .catch((value) => value as Error)
    if (!(noValue instanceof Error)) throw new Error('Expected immediate closure failure')
    expect(noValue.message).toContain('expect(locator).toHaveText(expected) failed')
    expect(noValue.message).toContain('Expected: "wanted"')
    expect(noValue.message).toContain('actual caller reason')
    expect(noValue.message).not.toContain('Received:')
  })
  it('reports actual native strictness and selector causes with matcher context', async () => {
    const strict = fixture(false)
    strict.runAutomation.mockResolvedValueOnce({
      exceptionDetails: {
        text: 'Uncaught',
        exception: {
          description:
            "Error: strict mode violation: locator('div') resolved to 2 elements:\n    1) <div>a</div> aka getByText('a')\n    2) <div>b</div> aka getByText('b')\n    at native observer",
        },
      },
    })
    const error = await expectNative(strict.page.locator('div'))
      .toHaveText('foo')
      .catch((value) => value as Error)
    if (!(error instanceof Error)) throw new Error('Expected native strictness failure')
    expect(error.message).toContain(
      "Locator: locator('div')\nExpected: \"foo\"\nError: strict mode violation: locator('div') resolved to 2 elements:",
    )
    expect(error.message).toContain("2) <div>b</div> aka getByText('b')\n\nCall log:")
    const invalid = fixture(false)
    invalid.runAutomation.mockResolvedValueOnce({
      exceptionDetails: {
        text: 'Uncaught',
        exception: {
          description:
            'Error: Unexpected token "#" while parsing css selector "##". Did you mean to CSS.escape it?\n    at native CSS parser',
        },
      },
    })
    const failed = await expectNative(invalid.page.locator('##'))
      .toBeVisible()
      .catch((value) => value as Error)
    if (!(failed instanceof Error)) throw new Error('Expected native selector failure')
    expect(failed.message).toContain('Locator: ##\nExpected: visible\nError: Unexpected token "#"')
    expect(failed.message).not.toContain('Received:')
  })
  it('rejects invalid URL expectations with the public argument diagnostic before native work', async () => {
    const { page, runAutomation } = fixture(true)
    const matcher = createNativeMatchers().toHaveURL
    if (!matcher) throw new Error('Native URL matcher missing')
    const failure = await matcher.call({ isNot: false }, page, {})
    expect(failure.message()).toBe(
      'expect(page).toHaveURL(expected) failed\n\nError: expected value must be a string or regular expression\nExpected has type:  object\nExpected has value: {}\n',
    )
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('preserves positive and negative failures from native observations', async () => {
    const mismatch = fixture(false)
    await expect(
      expectNative(mismatch.page.locator('input')).toHaveValue('expected'),
    ).rejects.toThrow('toHaveValue(expected) failed')
    await expectNative(mismatch.page.locator('input')).not.toHaveValue('expected')
    const matched = fixture(true)
    await expectNative(matched.page.locator('input')).toHaveValue('observed')
    await expect(
      expectNative(matched.page.locator('input')).not.toHaveValue('observed'),
    ).rejects.toThrow('not.toHaveValue(expected) failed')
  })

  it('passes configured matcher deadlines through to the native engine', async () => {
    const { page, runAutomation } = fixture(true)
    const matcher = createNativeMatchers().toHaveText
    if (!matcher) throw new Error('Native text matcher missing')
    expect(
      await matcher.call({ isNot: false, timeout: 137 }, page.locator('p'), 'observed'),
    ).toMatchObject({ pass: true })
    expect(runAutomation).toHaveBeenLastCalledWith(
      'queryEvaluate',
      expect.objectContaining({ timeoutMs: 137 }),
      expect.any(AbortSignal),
    )
    await matcher.call({ isNot: false, timeout: 137 }, page.locator('p'), 'observed', {
      timeout: 29,
    })
    expect(runAutomation).toHaveBeenLastCalledWith(
      'queryEvaluate',
      expect.objectContaining({ timeoutMs: 29 }),
      expect.any(AbortSignal),
    )
  })

  it('returns the runner negation state without converting assertion failures to success', async () => {
    const { page } = fixture(true)
    const matcher = createNativeMatchers().toHaveText
    if (!matcher) throw new Error('Native text matcher missing')
    const result = await matcher.call({ isNot: true }, page.locator('p'), 'observed', {
      timeout: 1,
    })
    expect(result.pass).toBe(true)
    expect(result.message()).toContain('not.toHaveText(expected) failed')
  })

  it('reports actual values and native observation metadata without inventing retry counts', async () => {
    const { page } = fixture(false, 'actual text', { count: 1, preview: '<p>actual text</p>' })
    const error = await expectNative(page.locator('p'))
      .toHaveText('expected text', { timeout: 137 })
      .catch((value) => value as Error)
    if (!(error instanceof Error)) throw new Error('Expected assertion failure')
    expect(error.message).toContain(
      'Expected: "expected text"\nReceived: "actual text"\nTimeout:  137ms',
    )
    expect(error.message).toContain('locator resolved to <p>actual text</p>')
    expect(error.message).not.toContain('2 ×')
    const missing = fixture(false, undefined, { count: 0 })
    const missingError = await expectNative(missing.page.locator('p'))
      .toHaveText('text', { timeout: 1 })
      .catch((value) => value as Error)
    if (!(missingError instanceof Error)) throw new Error('Expected missing locator failure')
    expect(missingError.message).toContain('Error: element(s) not found')
    expect(missingError.message).not.toContain('Received:')
    expect(missingError.message).not.toContain('locator resolved to')
  })

  it('diffs actual array observations without mutating regex state and limits diagnostic work', async () => {
    const { page } = fixture(false, ['Text 1', 'Text 3'], { count: 2 })
    const pattern = /Text \d/g
    pattern.lastIndex = 17
    const failure = await expectNative(page.locator('div'))
      .toHaveText(['Text 1', pattern, 'Extra'], { timeout: 1 })
      .catch((value) => value as Error)
    if (!(failure instanceof Error)) throw new Error('Expected mismatch')
    expect(failure.message).toContain("Locator: locator('div')\nTimeout: 1ms\n")
    expect(failure.message).toContain('- Expected  - 1\n+ Received  + 0')
    expect(failure.message).toContain('    "Text 3",\n-   "Extra",')
    expect(pattern.lastIndex).toBe(17)
    const empty = fixture(true, [], { count: 0 })
    const negatedFailure = await expectNative(empty.page.locator('p'))
      .not.toHaveText([], { timeout: 1 })
      .catch((value) => value as Error)
    if (!(negatedFailure instanceof Error)) throw new Error('Expected negated empty mismatch')
    expect(negatedFailure.message).toContain("Locator: locator('p')\nTimeout:  1ms\n")
    for (const matcher of ['toHaveClass', 'toContainClass'] as const) {
      const classes = fixture(false, ['foo', 'bar'], { count: 2 })
      const classFailure = await expectNative(classes.page.locator('div'))
        [matcher](['foo', 'baz'], { timeout: 1 })
        .catch((value) => value as Error)
      if (!(classFailure instanceof Error)) throw new Error('Expected class array mismatch')
      expect(classFailure.message).toContain("Locator: locator('div')\nTimeout: 1ms\n")
    }
    const large = fixture(
      false,
      Array.from({ length: 100 }, () => 'actual'),
      { count: 100 },
    )
    const largeFailure = await expectNative(large.page.locator('div'))
      .toHaveText(
        Array.from({ length: 100 }, () => 'expected'),
        { timeout: 1 },
      )
      .catch((value) => value as Error)
    if (!(largeFailure instanceof Error)) throw new Error('Expected array mismatch')
    expect(largeFailure.message).toContain('remaining items omitted')
    expect(largeFailure.message.length).toBeLessThan(4096)
  })

  it('rejects invalid class token arguments before sending native work', () => {
    const { page, runAutomation } = fixture(true)
    const matcher = createNativeMatchers().toContainClass
    if (!matcher) throw new Error('Native class matcher missing')
    expect(() => matcher.call({ isNot: false }, page.locator('p'), /token/)).toThrow(
      'cannot be a RegExp',
    )
    expect(() => matcher.call({ isNot: false }, page.locator('p'), ['token', /token/])).toThrow(
      'cannot contain RegExp',
    )
    expect(runAutomation).not.toHaveBeenCalled()
  })

  it('compares real API response status and includes only textual failure bodies', async () => {
    const { page } = fixture(true)
    const context = new NativeAPIRequestContext(page.source)
    const response = new NativeAPIResponse(context, {
      responseId: 'fixture-response',
      apiContextId: 1,
      url: 'http://fixture/',
      status: 404,
      statusText: 'Not Found',
      method: 'POST',
      headers: [{ name: 'Content-Type', value: 'text/plain' }],
    })
    const text = vi.spyOn(response, 'text').mockResolvedValue('actual response body')
    await expectNative(response).not.toBeOK()
    expect(text).not.toHaveBeenCalled()
    await expect(expectNative(response, 'HTTP context').toBeOK()).rejects.toThrow(
      'HTTP context\n\nexpect(response).toBeOK() failed\n\n→ POST http://fixture/\n← 404 Not Found\n\nResponse text:\nactual response body',
    )
    const binary = new NativeAPIResponse(context, {
      responseId: 'fixture-binary',
      apiContextId: 1,
      url: 'http://fixture/',
      status: 500,
      statusText: 'Error',
      method: 'HEAD',
      headers: [{ name: 'Content-Type', value: 'image/png' }],
    })
    const binaryText = vi.spyOn(binary, 'text')
    await expect(expectNative(binary).toBeOK()).rejects.toThrow('← 500 Error')
    expect(binaryText).not.toHaveBeenCalled()
  })

  it('rejects an unresolved promise as an API response matcher target', async () => {
    const matcher = createNativeMatchers().toBeOK
    if (!matcher) throw new Error('Native HTTP matcher missing')
    await expect(matcher.call({ isNot: false }, Promise.resolve({}))).rejects.toThrow(
      'toBeOK can be only used with APIResponse object',
    )
  })
})
