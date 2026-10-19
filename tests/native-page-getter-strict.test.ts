// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativeFrame, NativePage } from '../src/native-page.js'
import { createNativeValueCodec } from '../src/native-values.js'
import { type UntrustedInput } from '../src/untrusted.js'

const encoded = (value: UntrustedInput) => ({
  result: { type: 'object', value: createNativeValueCodec().encode(value) },
})
function fixture() {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>().mockResolvedValue(encoded('FIRST'))
  const source: NativeBrowser = {
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
    frameInfos: () => [{ id: 'main', parentId: '', name: '', url: 'about:blank' }],
  }
  return { page: new NativePage(source), runAutomation }
}

describe('native Page getter strictness', () => {
  it('sends actual first-match ownership only for Page primitive queries', async () => {
    const { page, runAutomation } = fixture()
    expect(await page.textContent('span')).toBe('FIRST')
    expect(runAutomation).toHaveBeenLastCalledWith(
      'queryEvaluate',
      expect.objectContaining({
        strict: false,
        dom: true,
        domGetter: true,
        query: [{ engine: 'css', selector: 'span' }],
      }),
      expect.any(AbortSignal),
    )
    expect(await page.getAttribute('span', 'data-value', { strict: true, timeout: 0 })).toBe(
      'FIRST',
    )
    expect(runAutomation).toHaveBeenLastCalledWith(
      'queryEvaluate',
      expect.objectContaining({ strict: true, timeoutMs: 0 }),
      expect.any(AbortSignal),
    )
    runAutomation.mockResolvedValue('FIRST')
    expect(await page.inputValue('input', { strict: false })).toBe('FIRST')
    expect(runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'inputValue',
        strict: false,
        query: [{ engine: 'css', selector: 'input' }],
      }),
      expect.any(AbortSignal),
    )
  })
  it('retains strict locator and exact fixed-handle identity', async () => {
    const { page, runAutomation } = fixture()
    expect(await page.locator('span').textContent()).toBe('FIRST')
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({ strict: true })
    expect(await new NativeElementHandle(page, 17).textContent()).toBe('FIRST')
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      nativeHandle: 17,
      strict: true,
      query: [],
    })
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('nativeRootHandle')
    await expect(page.locator('span').textContent({ strict: false } as never)).rejects.toThrow()
  })
  it('keeps visibility immediate and native first-match policy distinct from locator strictness', async () => {
    const { page, runAutomation } = fixture()
    runAutomation.mockResolvedValue({ result: { type: 'boolean', value: false } })
    expect(await page.isVisible('span')).toBe(false)
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      dom: true,
      domGetter: true,
      visibility: true,
      visibilitySnapshot: true,
      immediate: true,
      strict: false,
    })
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('all')
    expect(await page.isHidden('span', { strict: true })).toBe(true)
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({ immediate: true, strict: true })
    expect(await page.locator('span').isVisible()).toBe(false)
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({ immediate: true, strict: true })
  })
  it('pins lazy visibility to the explicit native snapshot mode without changing author or retained-node observations', async () => {
    const { page, runAutomation } = fixture()
    runAutomation.mockResolvedValue({ result: { type: 'boolean', value: false } })
    expect(
      await page
        .frameLocator('#parent')
        .frameLocator('#absent')
        .locator('button')
        .isHidden({ timeout: 81 }),
    ).toBe(true)
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      visibilitySnapshot: true,
      immediate: true,
      strict: true,
      timeoutMs: 81,
      framePath: [
        [{ engine: 'css', selector: '#parent' }],
        [{ engine: 'css', selector: '#absent' }],
      ],
    })
    runAutomation.mockResolvedValue({ result: { type: 'object', value: { visible: true } } })
    expect(await new NativeElementHandle(page, 17).isVisible()).toBe(true)
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({ nativeHandle: 17 })
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('visibilitySnapshot')
    runAutomation.mockResolvedValue(encoded('REAL'))
    expect(await page.textContent('span')).toBe('REAL')
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('visibilitySnapshot')
  })
  it('preserves actual child target, timeout and cancellation through Page facade delegation', async () => {
    const { page, runAutomation } = fixture(),
      abort = new AbortController()
    const frame = new NativeFrame(page, {
      id: 'child',
      parentId: 'main',
      name: '',
      url: 'about:blank',
    })
    expect(
      await frame.innerText('span', { strict: true, timeout: 712, signal: abort.signal }),
    ).toBe('FIRST')
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      frame: 'child',
      strict: true,
      timeoutMs: 712,
    })
    expect(runAutomation.mock.lastCall?.[2]).toBeInstanceOf(AbortSignal)
    runAutomation.mockResolvedValue({
      result: { type: 'object', value: { enabled: false, editable: false } },
    })
    expect(await frame.isDisabled('input')).toBe(true)
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      frame: 'child',
      strict: false,
      elementState: true,
    })
  })
  it('rejects malformed Page options as promises before allocating native work', async () => {
    const { page, runAutomation } = fixture()
    await expect(page.textContent('span', { strict: 'false' } as never)).rejects.toThrow()
    await expect(page.getAttribute('span', 'id', { timeout: -1 })).rejects.toThrow()
    await expect(page.isVisible('span', { force: true } as never)).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('forwards immediate Page.$ strictness to genuine native handles and rejects non-string selectors before native admission', async () => {
    const { page, runAutomation } = fixture()
    runAutomation.mockResolvedValue({
      handle: 31,
      preview: 'JSHandle@<span>Actual</span>',
      node: true,
      guid: 'actual-31',
    })
    const first = await page.$('span')
    expect(first?.id).toBe(31)
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({ strict: false })
    const strict = await page.$('span', { strict: true })
    expect(strict?.id).toBe(31)
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({ strict: true })
    runAutomation.mockClear()
    await expect(page.$(null as never)).rejects.toThrow('selector: expected string, got object')
    await expect(page.$('span', { strict: 'true' } as never)).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('does not change public author evaluation or all-match aggregate contracts', async () => {
    const { page, runAutomation } = fixture(),
      object = { __suitecutNativeUTF16Scalar: 'd800' }
    runAutomation.mockResolvedValue(encoded(object))
    expect(await page.locator('span').evaluate(() => object)).toEqual(object)
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('strict')
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('domGetter')
    runAutomation.mockResolvedValue(encoded(['First\ud800', 'Second\udfff']))
    expect(await page.locator('span').allTextContents()).toEqual(['First\ud800', 'Second\udfff'])
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      all: true,
      strict: true,
      domGetter: true,
    })
  })
})

describe('native diagnostic expression provenance', () => {
  it('keeps user descriptions as labels instead of executable selector aliases', async () => {
    const { page, runAutomation } = fixture()
    await page.locator('span').describe("getByRole('wrong')").textContent()
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      diagnosticExpressionJSON: JSON.stringify("locator('span')"),
      query: [{ engine: 'css', selector: 'span' }],
    })
  })
  it('keeps isolated string units and real frame scope inside explicit JSON data', async () => {
    const { page, runAutomation } = fixture()
    const selector = '#actual\ud800'
    await page.frameLocator('iframe').locator(selector).textContent()
    const packet = runAutomation.mock.lastCall?.[1]
    expect(packet).toHaveProperty(
      'diagnosticExpressionJSON',
      JSON.stringify("locator('iframe').contentFrame().locator('" + selector + "')"),
    )
    expect(packet).toHaveProperty('framePath', [[{ engine: 'css', selector: 'iframe' }]])
  })
  it('does not accept a caller-supplied diagnostic label as the construction expression', async () => {
    const { page, runAutomation } = fixture()
    await page
      .getByRole('heading', { level: 2 })
      .describe('label only')
      .runExpectation({
        function: 'function(){}',
        dom: true,
        strictDiagnostics: true,
        diagnosticExpressionJSON: JSON.stringify('fake API'),
      })
    expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
      diagnosticExpressionJSON: JSON.stringify("getByRole('heading')"),
      strictDiagnostics: true,
      query: [
        { engine: 'css', selector: ':scope' },
        { engine: 'role', role: 'heading', level: 2 },
      ],
    })
  })
})

it('bounds diagnostic packet data without changing the actual large native selector', async () => {
  const { page, runAutomation } = fixture(),
    selector = 'a'.repeat(20_000)
  await page.locator(selector).textContent()
  expect(runAutomation.mock.lastCall?.[1]).toMatchObject({
    diagnosticExpressionJSON: JSON.stringify(''),
    query: [{ engine: 'css', selector }],
  })
})
