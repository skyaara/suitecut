// SPDX-License-Identifier: MIT
import { Script } from 'node:vm'

import { describe, expect, it, vi } from 'vitest'
import { parseDocument } from 'yaml'
import * as z from 'zod'

import {
  formatNativeAriaDifference,
  formatNativeAriaTree,
  matchNativeAriaTree,
  normalizeNativeAriaTemplate,
  observeNativeAria,
  parseNativeAriaTemplate,
  type NativeAriaNode,
} from '../src/native-aria.js'
import { type NativeBrowser } from '../src/native-browser.js'
import { createNativePage } from '../src/native-page.js'
import { type UntrustedInput } from '../src/untrusted.js'

describe('authored ARIA declarative grammar and semantic comparison', () => {
  it('serializes safe expected data into one native scheduler request with its original deadline', async () => {
    const actual: NativeAriaNode[] = [{ role: 'link', name: 'Link', properties: { url: '/docs/' } }]
    const runAutomation = vi.fn(
      (_command: string, params?: Record<string, UntrustedInput>): Promise<UntrustedInput> => {
        const declaration = z.string().parse(params?.function)
        const evaluator = new Script(`(${declaration})`).runInNewContext() as UntrustedInput
        if (typeof evaluator !== 'function') throw new Error('Expected authored renderer evaluator')
        const result = Reflect.apply(evaluator, undefined, [
          [{}],
          undefined,
          actual,
        ]) as UntrustedInput
        return Promise.resolve({ result: { value: result } })
      },
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
    const locator = createNativePage(source).locator('a')
    const result = await observeNativeAria(
      locator,
      { timeout: 37 },
      parseNativeAriaTemplate('- link:\n  - /__proto__: /.*/'),
    )
    expect(result.matches).toBe(false)
    expect(result.actual).toBe('- link "Link":\n  - /url: /docs/')
    expect(runAutomation).toHaveBeenCalledTimes(1)
    expect(runAutomation).toHaveBeenCalledWith(
      'queryEvaluate',
      expect.objectContaining({
        accessibilitySnapshot: true,
        expectation: true,
        dom: true,
        timeoutMs: 37,
        all: true,
      }),
      expect.any(AbortSignal),
    )
    actual.splice(0, actual.length, { role: 'region', text: 'before\ud800after' })
    expect((await observeNativeAria(locator)).actual).toBe('- region: before\ud800after')
    runAutomation.mockImplementationOnce(() =>
      Promise.resolve({
        result: {
          value: {
            matches: true,
            actual: ['utf16', 'd800'.repeat(65537)],
            count: 1,
          },
        },
      }),
    )
    await expect(observeNativeAria(locator)).rejects.toThrow()
    runAutomation.mockImplementationOnce(() =>
      Promise.resolve({
        result: {
          value: {
            matches: true,
            actual: ['utf16', 'D800'],
            count: 1,
          },
        },
      }),
    )
    await expect(observeNativeAria(locator)).rejects.toThrow()
  })
  it('preserves quoted names, regex character classes and actual mixed/number states', () => {
    const pattern = parseNativeAriaTemplate(`
      - heading /Issues 1[/]2/ [level = 2]
      - 'button "Click '' me" [pressed=mixed]'
    `)
    const actual: NativeAriaNode[] = [
      { role: 'heading', name: 'Issues 1/2', states: { level: 2 } },
      { role: 'button', name: "Click ' me", states: { pressed: 'mixed' } },
    ]
    expect(matchNativeAriaTree(actual, pattern)).toBe(true)
    expect(matchNativeAriaTree(actual, pattern)).toBe(true)
    expect(
      matchNativeAriaTree([{ role: 'heading', name: 'Issues 1/2', states: { level: 3 } }], pattern),
    ).toBe(false)
  })

  it('keeps quoted text literal and normalizes rendered text template whitespace', () => {
    const text: NativeAriaNode[] = [
      { role: 'paragraph', children: [{ role: 'text', text: 'Line 1 Line 2' }] },
    ]
    expect(
      matchNativeAriaTree(text, parseNativeAriaTemplate('- paragraph: |\n    Line 1\n    Line 2')),
    ).toBe(true)
    expect(
      matchNativeAriaTree(
        [{ role: 'button', name: 'helloworld' }],
        parseNativeAriaTemplate('- button "  he\u00adlloworld\u200b   "'),
      ),
    ).toBe(true)
    const actual: NativeAriaNode[] = [{ role: 'text', text: 'hello' }]
    expect(matchNativeAriaTree(actual, parseNativeAriaTemplate('- text: /hello/'))).toBe(true)
    expect(matchNativeAriaTree(actual, parseNativeAriaTemplate('- text: "/hello/"'))).toBe(false)
    expect(
      matchNativeAriaTree(
        [{ role: 'text', text: '/hello/' }],
        parseNativeAriaTemplate('- text: "/hello/"'),
      ),
    ).toBe(true)
  })

  it('distinguishes ordered subsets, exact children and inherited deep equality', () => {
    const tree: NativeAriaNode[] = [
      {
        role: 'list',
        children: ['one', 'two', 'three'].map((text) => ({
          role: 'listitem',
          children: [{ role: 'text', text }],
        })),
      },
    ]
    const before = JSON.stringify(tree)
    expect(
      matchNativeAriaTree(
        tree,
        parseNativeAriaTemplate('- list:\n  - listitem: one\n  - listitem: three'),
      ),
    ).toBe(true)
    expect(
      matchNativeAriaTree(
        tree,
        parseNativeAriaTemplate(
          '- list:\n  - /children: equal\n  - listitem: one\n  - listitem: three',
        ),
      ),
    ).toBe(false)
    expect(
      matchNativeAriaTree(
        tree,
        parseNativeAriaTemplate('- /children: deep-equal\n- list:\n  - listitem: one'),
      ),
    ).toBe(false)
    expect(
      matchNativeAriaTree(
        tree,
        parseNativeAriaTemplate('- list:\n  - listitem: one\n  - listitem: one'),
      ),
    ).toBe(false)
    expect(
      matchNativeAriaTree(
        tree,
        parseNativeAriaTemplate(
          '- /children: deep-equal\n- list:\n  - listitem: one\n  - listitem: two\n  - listitem: three',
        ),
      ),
    ).toBe(true)
    expect(JSON.stringify(tree)).toBe(before)
  })

  it('matches actual URL properties without inheriting object prototype fields', () => {
    const link: NativeAriaNode[] = [{ role: 'link', name: 'Link', properties: { url: '/docs/' } }]
    expect(matchNativeAriaTree(link, parseNativeAriaTemplate('- link:\n  - /url: /doc.*/'))).toBe(
      true,
    )
    expect(matchNativeAriaTree(link, parseNativeAriaTemplate('- link:\n  - /url: /[a/'))).toBe(
      false,
    )
    expect(
      matchNativeAriaTree(
        [{ role: 'link', properties: {} }],
        parseNativeAriaTemplate('- link:\n  - /url: /.*/'),
      ),
    ).toBe(false)
    expect(
      matchNativeAriaTree(
        [{ role: 'link', properties: { url: '/auth?r=/' } }],
        parseNativeAriaTemplate('- link:\n  - /url: /auth?r=/'),
      ),
    ).toBe(true)
    const reserved = parseNativeAriaTemplate('- link:\n  - /__proto__: /.*/')
    expect(Object.hasOwn(reserved.children[0]?.properties ?? {}, '__proto__')).toBe(true)
    expect(matchNativeAriaTree(link, reserved)).toBe(false)
  })

  it('formats actual names, text and raw href with no generated regex or duplicate text', () => {
    const nodes: NativeAriaNode[] = [
      {
        role: 'heading',
        name: 'hello',
        states: { level: 1 },
        children: [{ role: 'text', text: 'hello' }],
      },
      {
        role: 'list',
        children: [{ role: 'listitem', children: [{ role: 'text', text: 'Transaction: done' }] }],
      },
      { role: 'link', name: 'Link', properties: { url: 'about:blank' } },
    ]
    expect(formatNativeAriaTree(nodes)).toBe(
      '- heading "hello" [level=1]\n- list:\n  - listitem: "Transaction: done"\n- link "Link":\n  - /url: about:blank',
    )
    expect(formatNativeAriaDifference('- heading "wrong"', '- heading "hello" [level=1]')).toBe(
      '- Expected  - 1\n+ Received  + 1\n\n- - heading "wrong"\n+ - heading "hello" [level=1]',
    )
    expect(normalizeNativeAriaTemplate('    - heading "one"\n- heading "two"')).toBe(
      '    - heading "one"\n- heading "two"',
    )
  })

  it('bounds YAML name previews while preserving complete names for matching and child text', () => {
    const name = 'a'.repeat(10000)
    const nodes: NativeAriaNode[] = [
      {
        role: 'link',
        name,
        properties: { url: 'about:blank' },
        children: [{ role: 'region', text: name }],
      },
    ]
    const before = JSON.stringify(nodes)
    expect(formatNativeAriaTree(nodes)).toBe(`- link:\n  - /url: about:blank\n  - region: ${name}`)
    expect(
      matchNativeAriaTree(nodes, parseNativeAriaTemplate(`- link ${JSON.stringify(name)}`)),
    ).toBe(true)
    expect(formatNativeAriaTree([{ role: 'button', name: 'b'.repeat(1000) }])).toContain(
      'b'.repeat(1000),
    )
    expect(formatNativeAriaTree([{ role: 'button', name: 'b'.repeat(1001) }])).toBe('- button')
    expect(JSON.stringify(nodes)).toBe(before)
  })

  it('emits valid YAML for actual text with properties and only heading levels', () => {
    const nodes: NativeAriaNode[] = [
      {
        role: 'list',
        children: [
          { role: 'listitem', states: { level: 1 }, children: [{ role: 'text', text: 'one:' }] },
          {
            role: 'listitem',
            states: { level: 1 },
            children: [{ role: 'list', children: [{ role: 'listitem', text: 'nested' }] }],
          },
        ],
      },
      { role: 'textbox', name: 'User', text: 'current value', properties: { placeholder: 'Name' } },
    ]
    const before = JSON.stringify(nodes)
    const formatted = formatNativeAriaTree(nodes)
    expect(parseDocument(formatted).errors).toEqual([])
    expect(formatted).toContain('- listitem: "one:"')
    expect(formatted).toContain(
      '- textbox "User":\n  - /placeholder: Name\n  - text: current value',
    )
    expect(formatted).not.toContain('[level=1]')
    expect(formatNativeAriaTree(nodes, { depth: 1 })).toContain('  - listitem\n- textbox')
    expect(JSON.stringify(nodes)).toBe(before)
  })

  it('preserves actual invalid states and requires actual finite box geometry', () => {
    const nodes: NativeAriaNode[] = [
      { role: 'textbox', name: 'Bio', states: { invalid: 'grammar' }, box: [10, 20, 80, 40] },
    ]
    expect(
      matchNativeAriaTree(nodes, parseNativeAriaTemplate('- textbox "Bio" [invalid=grammar]')),
    ).toBe(true)
    expect(matchNativeAriaTree(nodes, parseNativeAriaTemplate('- textbox "Bio" [invalid]'))).toBe(
      false,
    )
    expect(
      matchNativeAriaTree(
        [{ role: 'textbox' }],
        parseNativeAriaTemplate('- textbox [invalid=false]'),
      ),
    ).toBe(true)
    expect(formatNativeAriaTree([{ role: 'text', text: "Hi, I'm frame" }])).toBe(
      "- text: Hi, I'm frame",
    )
    expect(formatNativeAriaTree(nodes, { boxes: true })).toBe(
      '- textbox "Bio" [invalid=grammar] [box=10,20,80,40]',
    )
    expect(() => formatNativeAriaTree([{ role: 'button' }], { boxes: true })).toThrow(
      'box is unavailable',
    )
    expect(() =>
      formatNativeAriaTree([{ role: 'button', box: [Number.NaN, 0, 1, 1] }], { boxes: true }),
    ).toThrow('box is unavailable')
  })

  it('preserves authentic AX invalid state while projecting an actual DOM subtype only when invalid', () => {
    const nodes: NativeAriaNode[] = [
      { role: 'textbox', states: { invalid: true }, invalidToken: 'grammar' },
    ]
    const before = JSON.stringify(nodes)
    expect(matchNativeAriaTree(nodes, parseNativeAriaTemplate('- textbox [invalid=grammar]'))).toBe(
      true,
    )
    expect(matchNativeAriaTree(nodes, parseNativeAriaTemplate('- textbox [invalid]'))).toBe(false)
    expect(formatNativeAriaTree(nodes)).toBe('- textbox [invalid=grammar]')
    const valid: NativeAriaNode[] = [
      { role: 'textbox', states: { invalid: false }, invalidToken: 'grammar' },
    ]
    expect(matchNativeAriaTree(valid, parseNativeAriaTemplate('- textbox [invalid=false]'))).toBe(
      true,
    )
    expect(matchNativeAriaTree(valid, parseNativeAriaTemplate('- textbox [invalid=grammar]'))).toBe(
      false,
    )
    expect(formatNativeAriaTree(valid)).toBe('- textbox')
    expect(JSON.stringify(nodes)).toBe(before)
  })

  it('rejects malformed grammar, recursive aliases and excess input/traversal', () => {
    expect(() => parseNativeAriaTemplate('- checkbox [checked=5]')).toThrow('boolean or "mixed"')
    expect(() => parseNativeAriaTemplate('- heading /unfinished')).toThrow('Unterminated regex')
    expect(() => parseNativeAriaTemplate('- heading [level=]')).toThrow(
      'heading [level=]\n               ^',
    )
    expect(() => parseNativeAriaTemplate('heading "one"')).toThrow('YAML sequence')
    expect(() => parseNativeAriaTemplate('- &self\n  - *self')).toThrow()
    expect(() => parseNativeAriaTemplate('x'.repeat(65537))).toThrow('64 KiB')
    expect(() =>
      formatNativeAriaTree(Array.from({ length: 4097 }, () => ({ role: 'img' }))),
    ).toThrow('bounds')
    expect(() => formatNativeAriaTree([{ role: 'text', text: 'x'.repeat(32769) }])).toThrow(
      '64 KiB',
    )
  })
})
