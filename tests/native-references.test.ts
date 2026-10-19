// SPDX-License-Identifier: MIT
import { readFile } from 'node:fs/promises'

import * as ts from 'typescript'
import { describe, expect, it, vi } from 'vitest'

import { formatNativeAriaTree, parseNativeReferenceForest } from '../src/native-aria.js'
import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage, NativeFrame } from '../src/native-page.js'
import { nativeQueryDescription, nativeReferenceQuery } from '../src/native-query.js'
import { type UntrustedInput } from '../src/untrusted.js'

const reference = 'rActualNativeNamespace_123_0'
function pageFixture(forest: UntrustedInput = [{ role: 'button', name: 'Go', ref: reference }]) {
  const runAutomation = vi.fn(
    (_command: string, _params?: Record<string, UntrustedInput>): Promise<UntrustedInput> =>
      Promise.resolve({
        yamlJSON: JSON.stringify(
          formatNativeAriaTree(parseNativeReferenceForest(JSON.stringify(forest)), {
            mode: 'ai',
            ...(_params?.accessibilityBoxes === true ? { boxes: true } : {}),
          }),
        ),
      }),
  )
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
  }
  return { page: createNativePage(source), runAutomation }
}

describe('native snapshot references: typed packet and host schema contracts', () => {
  it('keeps the fixed native precommit formatter identical to its canonical authored SDK source', async () => {
    const source = await readFile(new URL('../src/native-aria.ts', import.meta.url), 'utf8')
    const header = await readFile(
      new URL('../native/browser/native_aria_formatter.h', import.meta.url),
      'utf8',
    )
    const parsed = ts.createSourceFile('native-aria.ts', source, ts.ScriptTarget.Latest, true)
    const declaration = parsed.statements.find(
      (node) => ts.isFunctionDeclaration(node) && node.name?.text === 'formatNativeAriaTree',
    )
    if (!declaration || !ts.isFunctionDeclaration(declaration))
      throw new Error('Missing canonical authored formatter')
    const compiled = ts
      .transpileModule(declaration.getText(parsed), {
        compilerOptions: { target: ts.ScriptTarget.ES2023, module: ts.ModuleKind.ESNext },
      })
      .outputText.replace(/^export /u, '')
      .trim()
    const native = /R"JS\(([\s\S]*)\)JS";/u.exec(header)?.[1]
    expect(native).toBe(compiled)
  })

  it('encodes only validated opaque tokens and describes their actual API construction', () => {
    expect(nativeReferenceQuery(reference)).toEqual({ engine: 'reference', ref: reference })
    expect(nativeQueryDescription([nativeReferenceQuery(reference)])).toBe(
      `getByRef('${reference}')`,
    )
    for (const invalid of [
      'button',
      'rName_0_0',
      'rName_1_01',
      'rName_1_0] injected',
      'rName\ud800_1_0',
    ])
      expect(() => nativeReferenceQuery(invalid)).toThrow()
  })
  it('keeps a Page reference as native AST data without guessing a frame or CSS selector', async () => {
    const { page, runAutomation } = pageFixture()
    await page.getByRef(reference).runExpectation({ function: 'function(){return true;}' })
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      query: [{ engine: 'reference', ref: reference }],
    })
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('frame')
  })
  it('preserves a genuine Frame identity and an actual lazy FrameLocator path', async () => {
    const { page, runAutomation } = pageFixture()
    const frame = new NativeFrame(page, {
      id: 'actual-frame',
      parentId: 'actual-parent',
      name: 'Actual',
      url: 'about:blank',
    })
    await frame.getByRef(reference).runExpectation({ function: 'function(){return true;}' })
    expect(runAutomation.mock.calls[0]?.[1]).toHaveProperty('frame', 'actual-frame')
    await page
      .frameLocator('iframe')
      .getByRef(reference)
      .runExpectation({ function: 'function(){return true;}' })
    expect(runAutomation.mock.calls[1]?.[1]).toMatchObject({
      framePath: [[{ engine: 'css', selector: 'iframe' }]],
      query: [{ engine: 'reference', ref: reference }],
    })
  })
  it('preserves prefixed CSS scope as native AST rather than guessing the reference frame', async () => {
    const { page, runAutomation } = pageFixture()
    await page
      .locator('#container')
      .getByRef(reference)
      .runExpectation({ function: 'function(){return true;}' })
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      query: [
        { engine: 'css', selector: '#container' },
        { engine: 'reference', ref: reference },
      ],
    })
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('frame')
  })
  it('keeps a nested native frame path with the complete reference leaf', async () => {
    const { page, runAutomation } = pageFixture()
    await page
      .frameLocator('#outer')
      .frameLocator('#inner')
      .getByRef(reference)
      .runExpectation({ function: 'function(){return true;}' })
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      framePath: [[{ engine: 'css', selector: '#outer' }], [{ engine: 'css', selector: '#inner' }]],
      query: [{ engine: 'reference', ref: reference }],
    })
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('frame')
  })
  it('AI mode requests the fixed native capture command with original timeout and boxes', async () => {
    const { page, runAutomation } = pageFixture([
      { role: 'button', name: 'Go', ref: reference, box: [1, 2, 3, 4] },
    ])
    expect(
      await page.locator('body').ariaSnapshot({ mode: 'ai', timeout: 37, boxes: true }),
    ).toContain(`[ref=${reference}]`)
    expect(runAutomation.mock.calls[0]?.[0]).toBe('snapshotReferences')
    expect(runAutomation.mock.calls[0]?.[1]).toMatchObject({
      timeoutMs: 37,
      accessibilityBoxes: true,
    })
    expect(runAutomation.mock.calls[0]?.[1]).not.toHaveProperty('function')
  })
  it('default YAML omits reference metadata while AI YAML prints validated tokens', () => {
    const forest = parseNativeReferenceForest(
      JSON.stringify([{ role: 'button', name: 'Go', ref: reference }]),
    )
    expect(formatNativeAriaTree(forest)).toBe('- button "Go"')
    expect(formatNativeAriaTree(forest, { mode: 'ai' })).toBe(`- button "Go" [ref=${reference}]`)
  })
  it('rejects invalid or text-owned tokens before YAML interpolation', () => {
    for (const node of [
      { role: 'button', ref: `${reference}] injected` },
      { role: 'text', text: 'actual', ref: reference },
    ])
      expect(() => parseNativeReferenceForest(JSON.stringify([node]))).toThrow()
    expect(() =>
      formatNativeAriaTree([{ role: 'button', ref: `${reference}\n- injected` }], { mode: 'ai' }),
    ).toThrow()
  })
  it('preserves real isolated UTF16 and ordinary tag-shaped text without indiscriminate decoding', () => {
    const name = 'Actual\ud800\u0000\\"\udfff'
    const tag = '{"__suitecutNativeUTF16Scalar":"d800"}'
    const forest = parseNativeReferenceForest(
      JSON.stringify([{ role: 'button', name, text: tag, ref: reference }]),
    )
    expect(forest[0]?.name).toBe(name)
    expect(forest[0]?.text).toBe(tag)
  })
  it('rejects unsupported native forest fields instead of treating arbitrary objects as metadata', () => {
    expect(() =>
      parseNativeReferenceForest('[{"role":"button","__suitecutNativeUTF16Scalar":"d800"}]'),
    ).toThrow()
    expect(() =>
      parseNativeReferenceForest('[{"role":"button","properties":{"ref":"author"}}]'),
    ).toThrow()
  })
  it('enforces actual tree depth and node budgets', () => {
    let nested: UntrustedInput = { role: 'button', ref: reference }
    for (let depth = 0; depth < 66; ++depth) nested = { role: 'group', children: [nested] }
    expect(() => parseNativeReferenceForest(JSON.stringify([nested]))).toThrow(/depth/u)
    expect(() =>
      parseNativeReferenceForest(
        JSON.stringify(Array.from({ length: 4097 }, () => ({ role: 'text', text: 'a' }))),
      ),
    ).toThrow()
  })
  it('rejects byte overflow and unknown public capture modes', async () => {
    expect(() =>
      parseNativeReferenceForest(JSON.stringify([{ role: 'text', text: '\u0800'.repeat(23000) }])),
    ).toThrow(/64 KiB/u)
    const { page, runAutomation } = pageFixture()
    await expect(page.locator('body').ariaSnapshot({ mode: 'ai', timeout: -1 })).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
