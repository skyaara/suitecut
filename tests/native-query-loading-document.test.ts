// SPDX-License-Identifier: MIT
import { readFileSync } from 'node:fs'
import { runInNewContext } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { type UntrustedInput } from '../src/untrusted.js'

// Execute the authored C++ literal unchanged. These controlled empty-document
// observations are distinct from the future compiled loading-frame proof.
const header = readFileSync(new URL('../native/shared/dom_query.h', import.meta.url), 'utf8')
const helper = header.slice(header.indexOf('R"JS(') + 5, header.lastIndexOf(')JS";'))
function fixture() {
  const document: { nodeType: number; documentElement: object | null; children: object[] } = {
    nodeType: 9,
    documentElement: null,
    children: [],
  }
  return {
    document,
    run: (selector: string, nativeQueryIntrinsics?: object): UntrustedInput =>
      runInNewContext(`(${helper})(steps,document,{},nativeQueryIntrinsics)`, {
        document,
        steps: [{ engine: 'css', selector }],
        nativeQueryIntrinsics,
      }) as UntrustedInput,
  }
}

describe('native CSS scope during real-document initialization', () => {
  it('returns no matches for a scope selector before a document element exists', () => {
    const { run } = fixture()
    expect(run(':scope')).toEqual([])
    expect(run(':is(:scope, html)')).toEqual([])
  })
  it('does not invoke the captured native Node brand getter on a null scope', () => {
    const { run } = fixture()
    const nativeQueryIntrinsics = {
      nodeType(node: { nodeType: number } | null) {
        if (!node) throw new Error('Actual native getter rejects null')
        return node.nodeType
      },
      regexExec: (expression: RegExp, value: string) => expression.exec(value),
      regexReplace: (value: string, expression: RegExp, replacement: string) =>
        value.replace(expression, replacement),
      regexSplit: (value: string, expression: RegExp) => value.split(expression),
    }
    expect(run(':scope', nativeQueryIntrinsics)).toEqual([])
  })
  it('uses the current document element when it genuinely appears on a later invocation', () => {
    const { run, document } = fixture()
    expect(run(':scope')).toEqual([])
    const html = { nodeType: 1, children: [], matches: (selector: string) => selector === '*' }
    document.documentElement = html
    document.children = [html]
    const actual = run(':scope')
    expect(actual).toEqual([html])
  })
  it('preserves malformed selector errors instead of treating an absent root as a blanket empty query', () => {
    const { run } = fixture()
    expect(() => run('##')).toThrow('Unexpected token "#"')
    expect(() => run('row]')).toThrow('Unexpected token "]"')
  })
})
