// SPDX-License-Identifier: MIT
import { runInNewContext } from 'node:vm'

import { describe, expect, it } from 'vitest'

import {
  browserDOM,
  type BrowserDOMOperation,
  type BrowserDOMQuery,
  type BrowserDOMValue,
} from '../src/browser-dom.js'
import { type UntrustedInput } from '../src/untrusted.js'

class DOMElement {
  nodeType = 1
  parentElement: DOMElement | null = null
  children: DOMElement[] = []
  shadowRoot: DOMRoot | null = null
  attributes = new Map<string, string>()
  textContent = ''
  innerText = ''
  isConnected = true
  hidden = false
  readOnly = false
  disabled = false
  type = 'text'
  private inputValue = ''
  checked = false
  selected = false
  multiple = false
  size = 0
  selectedIndex = 0
  isContentEditable = false
  labels: DOMElement[] | null = null
  display = 'block'
  visibility = 'visible'
  focused = false
  focusCount = 0
  selection = false
  events: string[] = []
  scrollCount = 0
  rect = { x: 10, y: 20, left: 10, top: 20, right: 110, bottom: 60, width: 100, height: 40 }
  root: DOMRoot | null = null
  constructor(
    readonly localName: string,
    attributes: Record<string, string> = {},
  ) {
    for (const [key, value] of Object.entries(attributes)) this.attributes.set(key, value)
    this.type = attributes.type ?? 'text'
  }
  append(...children: DOMElement[]) {
    for (const child of children) {
      child.parentElement = this
      this.children.push(child)
    }
  }
  get options() {
    return this.children.flatMap((child) =>
      child.localName === 'optgroup' ? child.children : [child],
    )
  }
  get childNodes(): (DOMElement | { nodeType: number; textContent: string })[] {
    return [{ nodeType: 3, textContent: this.textContent }, ...this.children]
  }
  get label() {
    return this.getAttribute('label') ?? this.textContent
  }
  get value() {
    return this.inputValue
  }
  set value(value: string) {
    this.inputValue = value.startsWith('bad-') ? '' : value
  }
  hasAttribute(key: string) {
    return this.attributes.has(key)
  }
  getAttribute(key: string) {
    return this.attributes.get(key) ?? null
  }
  getBoundingClientRect() {
    return this.rect
  }
  getRootNode(): DOMRoot {
    return this.parentElement?.getRootNode() ?? this.root!
  }
  matches(selector: string) {
    return selector === ':disabled'
      ? this.disabled
      : selector.startsWith('#')
        ? this.getAttribute('id') === selector.slice(1)
        : this.localName === selector
  }
  scrollIntoView() {
    this.scrollCount++
  }
  focus() {
    this.focused = true
    this.focusCount++
  }
  select() {
    this.selection = true
  }
  dispatchEvent(event: Event) {
    this.events.push(event.type)
    return true
  }
}
class DOMRoot {
  elements: DOMElement[] = []
  hit: DOMElement | null = null
  constructor(readonly host: DOMElement | null = null) {}
  append(...elements: DOMElement[]) {
    for (const element of elements) {
      element.root = this
      this.elements.push(element)
    }
  }
  querySelectorAll(selector: string): DOMElement[] {
    const all: DOMElement[] = []
    const visit = (element: DOMElement) => {
      if (selector === '*' || element.matches(selector)) all.push(element)
      element.children.forEach(visit)
    }
    this.elements.forEach(visit)
    return all
  }
  getElementById(id: string) {
    return this.querySelectorAll('*').find((element) => element.getAttribute('id') === id) ?? null
  }
  elementFromPoint() {
    return this.hit
  }
  createRange() {
    return { selectNodeContents: () => undefined }
  }
  createElement(name: string) {
    return new DOMElement(name)
  }
}
function fixture(...elements: DOMElement[]) {
  const document = new DOMRoot()
  document.append(...elements)
  const selection = { removed: 0, added: 0 }
  const styleReads: DOMElement[] = []
  const context = {
    document,
    innerWidth: 800,
    innerHeight: 600,
    Event,
    HTMLInputElement: DOMElement,
    getComputedStyle: (element: DOMElement) => {
      styleReads.push(element)
      return { display: element.display, visibility: element.visibility }
    },
    getSelection: () => ({
      removeAllRanges: () => {
        selection.removed++
      },
      addRange: () => {
        selection.added++
      },
    }),
  }
  return {
    document,
    selection,
    styleReads,
    run: (query: BrowserDOMQuery, operation: BrowserDOMOperation, value?: BrowserDOMValue) =>
      runInNewContext(
        `(${browserDOM.toString()})(${JSON.stringify(query)},${JSON.stringify(operation)},${JSON.stringify(value)})`,
        context,
      ) as UntrustedInput,
  }
}
const css = (value: string, index?: number): BrowserDOMQuery =>
  index === undefined
    ? { selector: { kind: 'css', value } }
    : { selector: { kind: 'css', value }, index }

describe('portable serialized browser DOM operations', () => {
  it('skips layout reads for role candidates with a different accessible name', () => {
    const other = new DOMElement('button'),
      target = new DOMElement('button')
    other.textContent = 'Other action'
    target.textContent = 'Target action'
    const { run, styleReads } = fixture(other, target)
    expect(
      run(
        { selector: { kind: 'role', value: 'button', name: 'Target action', exact: true } },
        'count',
      ),
    ).toBe(1)
    expect(styleReads).toEqual([target])
  })
  it('shares ancestor style reads within an invocation and refreshes them on the next call', () => {
    const wrapper = new DOMElement('div'),
      first = new DOMElement('button'),
      second = new DOMElement('button')
    wrapper.append(first, second)
    const { run, styleReads } = fixture(wrapper)
    const query: BrowserDOMQuery = { selector: { kind: 'role', value: 'button' } }
    expect(run(query, 'count')).toBe(2)
    expect(styleReads.filter((element) => element === wrapper)).toHaveLength(1)
    wrapper.visibility = 'hidden'
    expect(run(query, 'count')).toBe(0)
    expect(styleReads.filter((element) => element === wrapper)).toHaveLength(2)
    wrapper.visibility = 'visible'
    expect(run(query, 'count')).toBe(2)
    expect(styleReads.filter((element) => element === wrapper)).toHaveLength(3)
  })
  it('preserves hidden ancestor checks across shared open shadow roots', () => {
    const host = new DOMElement('custom-element')
    host.shadowRoot = new DOMRoot(host)
    host.shadowRoot.append(new DOMElement('button'), new DOMElement('button'))
    const { run, styleReads } = fixture(host)
    const query: BrowserDOMQuery = { selector: { kind: 'role', value: 'button' } }
    host.display = 'none'
    expect(run(query, 'count')).toBe(0)
    expect(styleReads.filter((element) => element === host)).toHaveLength(1)
    host.display = 'block'
    expect(run(query, 'count')).toBe(2)
  })
  it('executes without module closures, preserves null attributes, and validates strict selection', () => {
    const first = new DOMElement('button'),
      second = new DOMElement('button')
    first.textContent = 'First'
    second.textContent = 'Last'
    const { run } = fixture(first, second)
    expect(run(css('button'), 'count')).toBe(2)
    expect(run(css('button'), 'textContent')).toEqual({
      retry: 'Strict selector resolved to 2 elements',
    })
    expect(run(css('button', 0), 'textContent')).toBe('First')
    expect(run(css('button', -1), 'textContent')).toBe('Last')
    expect(run(css('missing', -1), 'textContent')).toEqual({ retry: 'Element was not found' })
    expect(run(css('button', 0), 'getAttribute', 'missing')).toBeNull()
    expect(run(css('button', 2), 'prepareClick')).toEqual({ retry: 'Element was not found' })
    expect(() => run(css('button', -2), 'textContent')).toThrow(/non-negative/u)
    expect(() => run(css('button', 0.5), 'textContent')).toThrow(/integer/u)
  })
  it('queries CSS, test IDs, and text within open shadow roots', () => {
    const host = new DOMElement('custom-element')
    host.shadowRoot = new DOMRoot(host)
    const button = new DOMElement('button', { 'data-testid': 'save"value' })
    button.textContent = ' Save   draft '
    host.shadowRoot.append(button)
    const { run } = fixture(host)
    expect(run(css('button'), 'count')).toBe(1)
    expect(run({ selector: { kind: 'testId', value: 'save"value' } }, 'textContent')).toBe(
      ' Save   draft ',
    )
    expect(run({ selector: { kind: 'text', value: 'Save draft', exact: true } }, 'count')).toBe(1)
  })
  it('keeps the smallest text matches and distinguishes exact and substring queries', () => {
    const wrapper = new DOMElement('div'),
      button = new DOMElement('button'),
      other = new DOMElement('span')
    wrapper.textContent = 'Save draft'
    button.textContent = 'Save draft'
    other.textContent = 'Save now'
    wrapper.append(button)
    const { run } = fixture(wrapper, other)
    expect(run({ selector: { kind: 'text', value: 'save' } }, 'count')).toBe(2)
    expect(run({ selector: { kind: 'text', value: 'Save draft', exact: true } }, 'count')).toBe(1)
  })
  it('resolves native labels and implicit roles with labelled-by names', () => {
    const label = new DOMElement('label', { id: 'name' }),
      input = new DOMElement('input'),
      button = new DOMElement('button', { 'aria-labelledby': 'name' })
    label.textContent = ' Full name '
    input.labels = [label]
    const { run } = fixture(label, input, button)
    expect(run({ selector: { kind: 'label', value: 'Full name', exact: true } }, 'count')).toBe(2)
    expect(
      run({ selector: { kind: 'role', value: 'button', name: 'Full name', exact: true } }, 'count'),
    ).toBe(1)
    expect(run({ selector: { kind: 'role', value: 'textbox' } }, 'count')).toBe(1)
  })
  it('bounds accessible-name reference cycles and ignores hidden role matches', () => {
    const button = new DOMElement('button', { id: 'self', 'aria-labelledby': 'self' })
    button.textContent = 'Own text'
    const hidden = new DOMElement('button', { 'aria-hidden': 'true' })
    const { run } = fixture(button, hidden)
    expect(run({ selector: { kind: 'role', value: 'button', name: 'Own text' } }, 'count')).toBe(1)
  })
  it('checks visibility, disabled ancestors and missing predicate results', () => {
    const wrapper = new DOMElement('div', { 'aria-disabled': 'true' }),
      button = new DOMElement('button')
    wrapper.append(button)
    const { run } = fixture(wrapper)
    expect(run(css('button'), 'enabled')).toBe(false)
    expect(run(css('button'), 'prepareClick')).toEqual({ retry: 'Element is disabled' })
    expect(run(css('missing'), 'visible')).toBe(false)
    wrapper.attributes.delete('aria-disabled')
    wrapper.visibility = 'hidden'
    expect(run(css('button'), 'box')).toBeNull()
  })
  it('requires a genuine hit target and allows children across open shadow roots', () => {
    const host = new DOMElement('x-button'),
      child = new DOMElement('span'),
      cover = new DOMElement('dialog')
    host.shadowRoot = new DOMRoot(host)
    host.shadowRoot.append(child)
    const { document, run } = fixture(host, cover)
    document.hit = cover
    expect(run(css('x-button'), 'prepareClick')).toEqual({
      retry: 'Element is covered by another element',
    })
    document.hit = host
    host.shadowRoot.hit = child
    expect(run(css('x-button'), 'prepareClick')).toEqual({ x: 60, y: 40 })
    expect(run(css('span'), 'prepareClick')).toEqual({ x: 60, y: 40 })
    expect(run(css('span'), 'box')).toEqual({ x: 10, y: 20, width: 100, height: 40 })
  })
  it('prepares text inputs without changing their values or dispatching synthetic input', () => {
    const input = new DOMElement('input')
    input.value = 'existing'
    const { run } = fixture(input)
    expect(run(css('input'), 'prepareFill', 'replacement')).toBe(true)
    expect(input.value).toBe('existing')
    expect(input.focused && input.selection).toBe(true)
    expect(input.events).toEqual([])
    input.readOnly = true
    expect(run(css('input'), 'prepareFill')).toEqual({ retry: 'Element is read-only' })
    input.readOnly = false
    input.type = 'file'
    expect(() => run(css('input'), 'prepareFill')).toThrow(/not a supported/u)
  })
  it('focuses repeatedly without selecting or replacing existing text', () => {
    const input = new DOMElement('input')
    input.value = 'Existing text'
    const { run } = fixture(input)
    expect(run(css('input'), 'focus')).toBe(true)
    expect(run(css('input'), 'focus')).toBe(true)
    expect(input.focusCount).toBe(2)
    expect(input.selection).toBe(false)
    expect(input.value).toBe('Existing text')
    expect(input.events).toEqual([])
    input.isConnected = false
    expect(run(css('input'), 'focus')).toEqual({ retry: 'Element is detached' })
  })
  it('focuses contenteditable without disturbing the current selection', () => {
    const editable = new DOMElement('div')
    editable.isContentEditable = true
    const { run, selection } = fixture(editable)
    expect(run(css('div'), 'focus')).toBe(true)
    expect(run(css('div'), 'focus')).toBe(true)
    expect(editable.focusCount).toBe(2)
    expect(selection).toEqual({ removed: 0, added: 0 })
  })
  it('requests genuine keyboard select-all for number inputs without calling DOM select', () => {
    const input = new DOMElement('input', { type: 'number' })
    input.value = '123'
    input.select = () => {
      throw new Error('Number inputs do not support DOM text selection')
    }
    const { run } = fixture(input)
    expect(run(css('input'), 'prepareFill', '456')).toEqual({ selectAll: true })
    expect(input.focused).toBe(true)
    expect(input.selection).toBe(false)
    expect(input.value).toBe('123')
    expect(input.events).toEqual([])
  })
  it('selects contenteditable content for subsequent genuine keyboard input', () => {
    const editable = new DOMElement('div')
    editable.isContentEditable = true
    editable.textContent = 'Original content'
    const { run } = fixture(editable)
    expect(run(css('div'), 'prepareFill', 'Replacement')).toBe(true)
    expect(editable.focused).toBe(true)
    expect(editable.textContent).toBe('Original content')
    expect(editable.events).toEqual([])
  })
  it('selects existing values, emits input/change once, and rejects disabled options', () => {
    const select = new DOMElement('select'),
      a = new DOMElement('option'),
      b = new DOMElement('option')
    a.value = 'a'
    b.value = 'b'
    select.append(a, b)
    const { run } = fixture(select)
    expect(run(css('select'), 'selectOption', 'b')).toEqual(['b'])
    expect([a.selected, b.selected]).toEqual([false, true])
    expect(select.events).toEqual(['input', 'change'])
    expect(run(css('select'), 'selectOption', 'missing')).toEqual({
      retry: 'Requested option was not found',
    })
    b.disabled = true
    expect(run(css('select'), 'selectOption', 'b')).toEqual({
      retry: 'Requested option is disabled',
    })
    expect(() => run(css('select'), 'selectOption', ['a', 'b'])).toThrow(/multiple/u)
  })
  it('supports multiple selects, duplicate values, clearing, and disabled optgroups', () => {
    const select = new DOMElement('select'),
      a = new DOMElement('option'),
      duplicate = new DOMElement('option'),
      group = new DOMElement('optgroup'),
      b = new DOMElement('option')
    select.multiple = true
    a.value = 'a'
    duplicate.value = 'a'
    b.value = 'b'
    group.append(b)
    select.append(a, duplicate, group)
    const { run } = fixture(select)
    expect(run(css('select'), 'selectOption', ['a', 'b'])).toEqual(['a', 'b'])
    expect([a.selected, duplicate.selected, b.selected]).toEqual([true, false, true])
    group.disabled = true
    expect(run(css('select'), 'selectOption', 'b')).toEqual({
      retry: 'Requested option is disabled',
    })
    expect(run(css('select'), 'selectOption', [])).toEqual([])
    expect(select.selectedIndex).toBe(-1)
    expect([a.selected, duplicate.selected, b.selected]).toEqual([false, false, false])
  })
  it('reads genuine checkbox state and rejects unrelated elements', () => {
    const input = new DOMElement('input', { type: 'checkbox' }),
      button = new DOMElement('button')
    input.checked = true
    const { run } = fixture(input, button)
    expect(run(css('input'), 'checked')).toBe(true)
    expect(() => run(css('button'), 'checked')).toThrow(/checkbox or radio/u)
  })
  it('finds wrapping labels without incorporating option or control text', () => {
    const label = new DOMElement('label'),
      select = new DOMElement('select'),
      option = new DOMElement('option'),
      span = new DOMElement('span')
    label.textContent = 'Delivery '
    span.textContent = 'window'
    option.textContent = 'Morning Afternoon'
    select.labels = [label]
    select.append(option)
    label.append(span, select)
    const { run } = fixture(label)
    expect(
      run({ selector: { kind: 'label', value: 'Delivery window', exact: true } }, 'count'),
    ).toBe(1)
    expect(
      run(
        { selector: { kind: 'role', value: 'combobox', name: 'Delivery window', exact: true } },
        'count',
      ),
    ).toBe(1)
  })
  it('queries placeholders and preserves hidden elements for raw upload resolution', () => {
    const input = new DOMElement('input', { placeholder: 'Your full name' }),
      file = new DOMElement('input', { type: 'file', id: 'file' })
    file.display = 'none'
    const { run } = fixture(input, file)
    expect(run({ selector: { kind: 'placeholder', value: 'Full Name' } }, 'count')).toBe(1)
    expect(
      run({ selector: { kind: 'placeholder', value: 'full name', exact: true } }, 'count'),
    ).toBe(0)
    expect(run(css('#file'), 'element')).toBe(file)
    expect(run(css('#file'), 'prepareUpload')).toBe(file)
    expect(() => run(css('input', 0), 'prepareUpload')).toThrow(/file input/u)
    file.disabled = true
    expect(run(css('#file'), 'prepareUpload')).toEqual({ retry: 'Element is disabled' })
  })
  it.each([
    ['date', '2026-10-18'],
    ['time', '15:45'],
    ['datetime-local', '2026-10-18T15:45'],
    ['month', '2026-10'],
    ['week', '2026-W42'],
  ])(
    'fills %s through its native setter with events and validates before mutation',
    (type, value) => {
      const input = new DOMElement('input', { type })
      const { run } = fixture(input)
      expect(run(css('input'), 'prepareFill', value)).toEqual({ filled: true })
      expect(input.value).toBe(value)
      expect(input.events).toEqual(['input', 'change'])
      expect(() => run(css('input'), 'prepareFill', 'bad-value')).toThrow(/Invalid/u)
      expect(input.value).toBe(value)
      expect(input.events).toEqual(['input', 'change'])
      expect(run(css('input'), 'prepareFill', '')).toEqual({ filled: true })
      expect(input.value).toBe('')
    },
  )
  it('selects by label and index, combines constraints, and rejects malformed choices', () => {
    const select = new DOMElement('select'),
      first = new DOMElement('option'),
      second = new DOMElement('option', { label: 'Express delivery' })
    first.value = 'standard'
    first.textContent = 'Standard delivery'
    second.value = 'express'
    select.append(first, second)
    const { run } = fixture(select)
    expect(run(css('select'), 'selectOption', { label: 'Express delivery' })).toEqual(['express'])
    expect(run(css('select'), 'selectOption', { index: 0 })).toEqual(['standard'])
    expect(run(css('select'), 'selectOption', { value: 'express', index: 0 })).toEqual({
      retry: 'Requested option was not found',
    })
    expect(() => run(css('select'), 'selectOption', {})).toThrow(/choices require/u)
    expect(() => run(css('select'), 'selectOption', { index: -1 })).toThrow(/non-negative/u)
  })
  it('allows hovering and scrolling disabled controls without allowing clicks', () => {
    const button = new DOMElement('button')
    button.disabled = true
    const { document, run } = fixture(button)
    document.hit = button
    expect(run(css('button'), 'prepareHover')).toEqual({ x: 60, y: 40 })
    expect(run(css('button'), 'scroll')).toBe(true)
    expect(button.scrollCount).toBe(2)
    expect(run(css('button'), 'prepareClick')).toEqual({ retry: 'Element is disabled' })
  })
})
