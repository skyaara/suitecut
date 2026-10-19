// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { decodeNativeLocatorNormalization } from '../src/native-locator-normalization.js'
import { createNativePage, NativeFrame } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

const pins = {
  source: 'actualSource',
  frame: 'actualFrame',
  session: '',
  loader: 'actualLoader',
  mainIdentity: 'actualMainUnique',
  utilityIdentity: 'actualUtilityUnique',
  mainContext: 4,
  utilityContext: 5,
  documentGeneration: '7',
  backendNodeId: 913,
}
const packet = (kind = 'id', query: UntrustedInput = [{ engine: 'css', selector: '#actual' }]) => ({
  metadataJSON: JSON.stringify({ kind, query }),
  provenance: { ...pins },
})

describe('native builtin locator normalization packets', () => {
  it('keeps the actual builtin AST and immutable genuine creation provenance', () => {
    const result = decodeNativeLocatorNormalization(packet())
    expect(result).toEqual({
      query: [{ engine: 'css', selector: '#actual' }],
      provenance: { ...pins, kind: 'id' },
    })
    expect(Object.isFrozen(result.provenance)).toBe(true)
  })
  it('keeps actual isolated units, NUL and tag-shaped author text as exact AST data', () => {
    for (const text of ['actual\ud800\0\\"\udfff', '{"__suitecutNativeUTF16Scalar":"d800"}'])
      expect(
        decodeNativeLocatorNormalization(
          packet('testId', [{ engine: 'testId', attribute: 'data-custom', text, exact: true }]),
        ).query,
      ).toEqual([{ engine: 'testId', attribute: 'data-custom', text, exact: true }])
  })
  it('accepts finite role, real attribute and scoped CSS path candidates', () => {
    for (const [kind, query] of [
      ['role', [{ engine: 'role', role: 'button', name: { text: 'Actual', exact: true } }]],
      ['attribute', [{ engine: 'placeholder', text: 'Actual', exact: true }]],
      [
        'path',
        [
          { engine: 'css', selector: 'div:nth-of-type(2)' },
          { engine: 'css', selector: 'button:nth-of-type(3)' },
        ],
      ],
    ] as const)
      expect(decodeNativeLocatorNormalization(packet(kind, query)).query).toEqual(query)
  })
  it('rejects arbitrary author dictionaries, unknown properties and malformed metadata', () => {
    for (const value of [
      { __suitecutNativeUTF16Scalar: 'd800' },
      { ...packet(), fake: true },
      { ...packet(), metadataJSON: '{' },
      { ...packet(), provenance: { ...pins, fake: true } },
    ])
      expect(() => decodeNativeLocatorNormalization(value)).toThrow()
  })
  it('rejects executable/custom/ref/filter plans and engine/kind inconsistencies', () => {
    for (const query of [
      [{ engine: 'custom', name: 'author', selector: 'actual' }],
      [{ engine: 'reference', ref: 'rFake_1_0' }],
      [{ engine: 'filter', hasText: { text: 'actual' } }],
      [{ engine: 'css', selector: 'button' }],
    ])
      expect(() => decodeNativeLocatorNormalization(packet('id', query))).toThrow()
    expect(() =>
      decodeNativeLocatorNormalization(
        packet('role', [
          { engine: 'role', role: 'button', name: { text: 'actual', exact: false } },
        ]),
      ),
    ).toThrow()
  })
  it('rejects invalid actual identity pins rather than inventing a frame/backend', () => {
    for (const changed of [
      { frame: '' },
      { backendNodeId: 0 },
      { mainContext: -1 },
      { documentGeneration: '7.5' },
      { source: 'space is invalid' },
    ])
      expect(() =>
        decodeNativeLocatorNormalization({ ...packet(), provenance: { ...pins, ...changed } }),
      ).toThrow()
  })
  it('bounds encoded UTF8 AST and JSON metadata, not only code unit counts', () => {
    expect(() =>
      decodeNativeLocatorNormalization(
        packet('path', [{ engine: 'css', selector: '中'.repeat(6000) }]),
      ),
    ).toThrow()
    expect(() =>
      decodeNativeLocatorNormalization({ ...packet(), metadataJSON: ' '.repeat(32769) }),
    ).toThrow()
    expect(() =>
      decodeNativeLocatorNormalization(
        packet(
          'path',
          Array.from({ length: 65 }, () => ({ engine: 'css', selector: 'button' })),
        ),
      ),
    ).toThrow()
  })
})

function pageFixture(reply: UntrustedInput = packet()) {
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>(() => Promise.resolve(reply))
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: new AbortController().signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'controlled-test',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    frameInfos: () => [
      { id: 'actualFrame', parentId: '', name: 'Main', url: 'about:blank' },
      { id: 'actualChild', parentId: 'actualFrame', name: 'Child', url: 'about:blank' },
    ],
  }
  return { page: createNativePage(source), runAutomation }
}

describe('normalization SDK command/projection contracts, no browser claim', () => {
  it('sends the original frame path/query/timeout and forwards real cancellation ownership', async () => {
    const { page, runAutomation } = pageFixture()
    const controller = new AbortController()
    const normalized = await page
      .frameLocator('iframe')
      .locator('button')
      .normalize({ timeout: 37, signal: controller.signal })
    expect(runAutomation.mock.calls[0]?.slice(0, 2)).toEqual([
      'normalizeLocator',
      expect.objectContaining({
        query: [{ engine: 'css', selector: 'button' }],
        framePath: [[{ engine: 'css', selector: 'iframe' }]],
        timeoutMs: 37,
      }),
    ])
    expect(normalized.normalization()).toEqual({ ...pins, kind: 'id' })
    expect(normalized.page()).toBe(page)
    expect(normalized.describe('Human label').normalization()).toBe(normalized.normalization())
    expect(normalized.first().normalization()).toBeNull()
    const forwarded = runAutomation.mock.calls[0]?.[2]
    expect(forwarded?.aborted).toBe(false)
    controller.abort()
    expect(forwarded?.aborted).toBe(true)
  })
  it('preserves the current actual Frame projection and its own cancellation signal', async () => {
    const { page } = pageFixture({ ...packet(), provenance: { ...pins, frame: 'actualChild' } })
    const controller = new AbortController()
    const child = new NativeFrame(
      page,
      { id: 'actualChild', parentId: 'actualFrame', name: 'Child', url: 'about:blank' },
      controller.signal,
    )
    const normalized = await child.locator('button').normalize()
    controller.abort(new Error('actual frame projection canceled'))
    await expect(normalized.count()).rejects.toThrow('actual frame projection canceled')
  })
  it('rejects missing native frame identity instead of manufacturing an owner', async () => {
    const { page } = pageFixture({
      ...packet(),
      provenance: { ...pins, frame: 'missingNativeFrame' },
    })
    await expect(page.locator('button').normalize()).rejects.toThrow(
      'frame identity is no longer available',
    )
  })
  it('rejects invalid options before issuing a native command', async () => {
    const { page, runAutomation } = pageFixture()
    await expect(page.locator('button').normalize({ timeout: -1 })).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
