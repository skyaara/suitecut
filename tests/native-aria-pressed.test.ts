// SPDX-License-Identifier: MIT
import { readFile } from 'node:fs/promises'
import { Script } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { formatNativeAriaJSON, parseNativeReferenceForest } from '../src/native-aria.js'
import { type UntrustedInput } from '../src/untrusted.js'

const header = await readFile(
  new URL('../native/shared/dom_semantic_snapshot.h', import.meta.url),
  'utf8',
)
const literal = /R"JS\(([\s\S]*)\)JS";/u.exec(header)?.[1]
if (!literal) throw new Error('Missing native semantic capture')
// Executes the exact production semantic literal over controlled DOM boundary
// objects. The supplied tools retain the selector engine's default-false state
// observation; these controls isolate whether snapshot state presence is owned.
function observe(attributes: Record<string, string>) {
  const value = new Script(`(() => {
    class Element {
      constructor(attributes) { this.attributes = new Map(Object.entries(attributes)); this.nodeType = 1; this.localName = 'button'; this.childNodes = []; this.ownerDocument = { documentElement: this, activeElement: this }; }
      getAttribute(key) { return this.attributes.get(key) ?? null; }
      getRootNode() { return this.ownerDocument; }
    }
    class HTMLInputElement extends Element {}
    class HTMLTextAreaElement extends Element {}
    const getComputedStyle = () => ({ display: 'inline', cursor: 'auto' });
    const query = () => ({
      role: element => element.getAttribute('role') || 'button', name: () => 'Control',
      normalize: text => text.trim(), hidden: () => false, pseudo: () => '', byId: () => null,
      state: (element, key) => {
        if (key === 'disabled') return element.getAttribute('disabled') !== null;
        const value = element.getAttribute('aria-' + key);
        return value === 'true' ? true : value === 'false' ? false : value === 'mixed' ? 'mixed' : ['checked', 'pressed', 'selected'].includes(key) ? false : undefined;
      },
    });
    return JSON.stringify((${literal})(new Element(${JSON.stringify(attributes)}), query));
  })()`).runInNewContext({ TextEncoder }) as UntrustedInput
  if (typeof value !== 'string') throw new Error('Native semantic control did not serialize')
  return formatNativeAriaJSON(parseNativeReferenceForest(value))
}

describe('native semantic toggle-state presence', () => {
  it('omits pressed on ordinary enabled and disabled buttons', () => {
    expect(observe({})).toEqual([{ role: 'button', name: 'Control' }])
    expect(observe({ disabled: '' })).toEqual([{ role: 'button', name: 'Control', disabled: true }])
  })
  it('preserves each genuine explicit toggle value, including false', () => {
    for (const value of ['true', 'false', 'mixed'])
      expect(observe({ 'aria-pressed': value })).toEqual([
        { role: 'button', name: 'Control', pressed: value === 'mixed' ? value : value === 'true' },
      ])
  })
  it('omits undefined or invalid toggle attributes rather than converting them into false', () => {
    for (const value of ['', 'undefined', 'invalid'])
      expect(observe({ 'aria-pressed': value })).toEqual([{ role: 'button', name: 'Control' }])
  })
  it('preserves the independent default false state of an actual checkable role', () => {
    expect(observe({ role: 'checkbox' })).toEqual([
      { role: 'checkbox', name: 'Control', checked: false },
    ])
  })
})
