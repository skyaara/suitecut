// SPDX-License-Identifier: MIT
import { readFile } from 'node:fs/promises'
import { Script } from 'node:vm'

import * as ts from 'typescript'
import { describe, expect, it, vi } from 'vitest'

import {
  formatNativeAriaJSON,
  parseNativeAriaJSON,
  type NativeAriaNode,
} from '../src/native-aria.js'
import { type NativeBrowser } from '../src/native-browser.js'
import { NativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

const reference = 'rActualSource1_2_0'

describe('direct structured native semantic projection', () => {
  it('preserves genuine names, state flags, properties and sole text content without YAML parsing', () => {
    const forest: NativeAriaNode[] = [
      {
        role: 'heading',
        name: 'Title',
        states: { level: 1 },
        children: [{ role: 'text', text: 'Title' }],
      },
      {
        role: 'list',
        name: 'List',
        children: [
          { role: 'listitem', states: { level: 1 }, children: [{ role: 'text', text: 'one' }] },
        ],
      },
      {
        role: 'checkbox',
        name: 'Check',
        states: { checked: false, invalid: true },
        invalidToken: 'grammar',
      },
      {
        role: 'link',
        name: 'Link',
        properties: { url: '/real' },
        children: [{ role: 'text', text: 'Link' }],
      },
      {
        role: 'textbox',
        name: 'Input',
        text: 'value',
        properties: { placeholder: 'Actual placeholder' },
      },
    ]
    expect(formatNativeAriaJSON(forest)).toEqual([
      { role: 'heading', name: 'Title', level: 1 },
      { role: 'list', name: 'List', children: [{ role: 'listitem', text: 'one' }] },
      { role: 'checkbox', name: 'Check', checked: false, invalid: 'grammar' },
      { role: 'link', name: 'Link', url: '/real' },
      { role: 'textbox', name: 'Input', text: 'value', placeholder: 'Actual placeholder' },
    ])
  })
  it('keeps top-level text nodes while nested fragments are actual strings in their original order', () => {
    expect(
      formatNativeAriaJSON([
        { role: 'text', text: 'Before' },
        {
          role: 'paragraph',
          children: [
            { role: 'text', text: 'Hello' },
            { role: 'link', name: 'world' },
            { role: 'text', text: 'again' },
          ],
        },
        { role: 'text', text: 'After' },
      ]),
    ).toEqual([
      { role: 'text', text: 'Before' },
      { role: 'paragraph', children: ['Hello', { role: 'link', name: 'world' }, 'again'] },
      { role: 'text', text: 'After' },
    ])
  })
  it('applies depth to the genuine tree and reads boxes from actual supplied native geometry only', () => {
    const forest: NativeAriaNode[] = [
      {
        role: 'list',
        box: [1, 2, 3, 4],
        children: [
          {
            role: 'listitem',
            box: [5, 6, 7, 8],
            children: [{ role: 'button', name: 'One', box: [9, 10, 11, 12] }],
          },
        ],
      },
    ]
    expect(formatNativeAriaJSON(forest, { depth: 1 })).toEqual([
      { role: 'list', children: [{ role: 'listitem' }] },
    ])
    expect(formatNativeAriaJSON(forest, { depth: 0, boxes: true })).toEqual([
      { role: 'list', box: { x: 1, y: 2, width: 3, height: 4 } },
    ])
    expect(() => formatNativeAriaJSON([{ role: 'button' }], { boxes: true })).toThrow(/actual box/u)
    expect(() =>
      formatNativeAriaJSON([{ role: 'button', box: [NaN, 0, 1, 2] }], { boxes: true }),
    ).toThrow(/actual box/u)
  })
  it('emits only genuine safe opaque references on explicit ai projection, preserving defaults', () => {
    expect(formatNativeAriaJSON([{ role: 'button', ref: reference }])).toEqual([{ role: 'button' }])
    expect(formatNativeAriaJSON([{ role: 'button', ref: reference }], { mode: 'ai' })).toEqual([
      { role: 'button', ref: reference },
    ])
    expect(() =>
      formatNativeAriaJSON([{ role: 'button', ref: 'invented]' }], { mode: 'ai' }),
    ).toThrow(/reference token/u)
  })
  it('keeps native input metadata in AI mode without inventing textbox roles or exposing password values', () => {
    const forest: NativeAriaNode[] = [
      {
        role: 'generic',
        name: 'Password',
        ref: reference,
        inputType: 'password',
        readOnly: false,
        editable: true,
        text: 'never-publish-password',
        children: [{ role: 'text', text: 'never-publish-password' }],
      },
      { role: 'generic', name: 'Date', inputType: 'date', readOnly: true, editable: false },
    ]
    const projected = formatNativeAriaJSON(forest, { mode: 'ai' })
    expect(projected).toEqual([
      {
        role: 'generic',
        name: 'Password',
        ref: reference,
        inputType: 'password',
        readOnly: false,
        editable: true,
      },
      { role: 'generic', name: 'Date', inputType: 'date', readOnly: true, editable: false },
    ])
    expect(parseNativeAriaJSON(JSON.stringify(projected))).toEqual(projected)
    expect(
      formatNativeAriaJSON([
        { role: 'generic', inputType: 'date', readOnly: true, editable: false },
      ]),
    ).toEqual([{ role: 'generic' }])
  })
  it('preserves isolated units and ordinary tag-shaped strings while bounding actual serialized bytes and nodes', () => {
    const name = '\ud800',
      text = '{"__suitecutNativeUTF16Scalar":"d800"}'
    expect(
      JSON.parse(JSON.stringify(formatNativeAriaJSON([{ role: 'textbox', name, text }]))),
    ).toEqual([{ role: 'textbox', name, text }])
    expect(() =>
      formatNativeAriaJSON(Array.from({ length: 4097 }, () => ({ role: 'button' }))),
    ).toThrow(/node limit/u)
    expect(() => formatNativeAriaJSON([{ role: 'textbox', text: 'x'.repeat(65536) }])).toThrow(
      /64 KiB/u,
    )
    const root: NativeAriaNode = { role: 'list' }
    let next = root
    for (let i = 0; i < 65; i++) {
      const child: NativeAriaNode = { role: 'list' }
      next.children = [child]
      next = child
    }
    expect(() => formatNativeAriaJSON([root])).toThrow(/depth limit/u)
  })
  it('keeps the exact C++ utility-world formatter identical to the canonical authored function', async () => {
    const source = await readFile(new URL('../src/native-aria.ts', import.meta.url), 'utf8')
    const header = await readFile(
      new URL('../native/shared/native_aria_json.h', import.meta.url),
      'utf8',
    )
    const parsed = ts.createSourceFile('native-aria.ts', source, ts.ScriptTarget.Latest, true)
    const declaration = parsed.statements.find(
      (node) => ts.isFunctionDeclaration(node) && node.name?.text === 'formatNativeAriaJSON',
    )
    if (!declaration || !ts.isFunctionDeclaration(declaration))
      throw new Error('Missing authored native JSON formatter')
    const compiled = ts
      .transpileModule(declaration.getText(parsed), {
        compilerOptions: { target: ts.ScriptTarget.ES2023, module: ts.ModuleKind.ESNext },
      })
      .outputText.replace(/^export /u, '')
      .trim()
    const native = /R"JS\(([\s\S]*)\)JS";/u.exec(header)?.[1]
    expect(native).toBe(compiled)
    const fn = new Script(`(${native})`).runInNewContext({ TextEncoder }) as UntrustedInput
    if (typeof fn !== 'function') throw new Error('Native fixed formatter unavailable')
    expect(
      Reflect.apply(fn, undefined, [[{ role: 'button', name: 'actual' }]]) as UntrustedInput,
    ).toEqual([{ role: 'button', name: 'actual' }])
  })
})

function nativeFixture(snapshot: string) {
  const runAutomation = vi
    .fn<NativeBrowser['runAutomation']>()
    .mockResolvedValue({ snapshotJSON: snapshot })
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
  return { page: new NativePage(source), runAutomation }
}

describe('structured native snapshot transport', () => {
  it('sends actual lazy scope/options/signal once without an author callback or YAML round trip', async () => {
    const { page, runAutomation } = nativeFixture('[{"role":"button","name":"Actual"}]')
    const controller = new AbortController()
    expect(
      await page
        .frameLocator('iframe')
        .locator('button')
        .ariaSnapshotJSON({ boxes: true, depth: 1, timeout: 91, signal: controller.signal }),
    ).toEqual([{ role: 'button', name: 'Actual' }])
    expect(runAutomation).toHaveBeenCalledOnce()
    expect(runAutomation).toHaveBeenLastCalledWith(
      'locator',
      expect.objectContaining({
        action: 'ariaSnapshotJSON',
        query: [{ engine: 'css', selector: 'button' }],
        framePath: [[{ engine: 'css', selector: 'iframe' }]],
        accessibilityBoxes: true,
        snapshotDepth: 1,
        timeoutMs: 91,
      }),
      expect.any(AbortSignal),
    )
    expect(runAutomation.mock.lastCall?.[1]).not.toHaveProperty('function')
  })
  it('uses the native precommit JSON reference serializer only for explicit AI mode', async () => {
    const { page, runAutomation } = nativeFixture(
      JSON.stringify([{ role: 'button', ref: reference }]),
    )
    expect(await page.ariaSnapshotJSON({ mode: 'ai', timeout: 13 })).toEqual([
      { role: 'button', ref: reference },
    ])
    expect(runAutomation).toHaveBeenLastCalledWith(
      'snapshotReferences',
      expect.objectContaining({
        referenceFormat: 'json',
        query: [{ engine: 'css', selector: 'body, frameset' }],
        timeoutMs: 13,
      }),
      expect.any(AbortSignal),
    )
  })
  it('validates actual structured native output without decoding ordinary author tag data', () => {
    const text = '{"__suitecutNativeUTF16Scalar":"d800"}',
      name = '\ud800'
    expect(
      parseNativeAriaJSON(
        JSON.stringify([{ role: 'textbox', name, text, children: ['actual fragment'] }]),
      ),
    ).toEqual([{ role: 'textbox', name, text, children: ['actual fragment'] }])
    for (const malformed of [
      '["top-level string"]',
      '[{"role":"text","ref":"' + reference + '"}]',
      '[{"role":"button","disabled":7}]',
      '[{"role":"button","__suitecutNativeUTF16Scalar":"d800"}]',
    ])
      expect(() => parseNativeAriaJSON(malformed)).toThrow()
  })
  it('preserves the exact aborted reason without starting a native capture', async () => {
    const { page, runAutomation } = nativeFixture('[]')
    const controller = new AbortController()
    controller.abort('actual JSON cancellation')
    await expect(page.ariaSnapshotJSON({ signal: controller.signal })).rejects.toMatchObject({
      name: 'AbortError',
      cause: 'actual JSON cancellation',
    })
    expect(runAutomation).not.toHaveBeenCalled()
  })
  it('rejects unknown or unsupported options before native admission', async () => {
    const { page, runAutomation } = nativeFixture('[]')
    for (const options of [{ mode: 'expect' }, { depth: 65 }, { timeout: -1 }, { force: true }])
      await expect(page.ariaSnapshotJSON(options as never)).rejects.toThrow()
    expect(runAutomation).not.toHaveBeenCalled()
  })
})
