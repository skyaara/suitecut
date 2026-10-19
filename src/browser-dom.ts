// SPDX-License-Identifier: MIT

export type BrowserSelector =
  | { kind: 'css'; value: string }
  | { kind: 'text'; value: string; exact?: boolean }
  | { kind: 'role'; value: string; name?: string; exact?: boolean }
  | { kind: 'label'; value: string; exact?: boolean }
  | { kind: 'placeholder'; value: string; exact?: boolean }
  | { kind: 'testId'; value: string }

export type BrowserSelectOption = string | { value?: string; label?: string; index?: number }
export type BrowserDOMValue = string | BrowserSelectOption | BrowserSelectOption[]

export interface BrowserDOMQuery {
  selector: BrowserSelector
  index?: number
}
export type BrowserDOMOperation =
  | 'count'
  | 'textContent'
  | 'innerText'
  | 'getAttribute'
  | 'visible'
  | 'enabled'
  | 'box'
  | 'prepareClick'
  | 'prepareHover'
  | 'prepareFill'
  | 'prepareUpload'
  | 'element'
  | 'scroll'
  | 'focus'
  | 'selectOption'
  | 'checked'
export type BrowserDOMResult =
  | number
  | string
  | string[]
  | boolean
  | null
  | Element
  | { retry: string }
  | { selectAll: true }
  | { filled: true }
  | { x: number; y: number; width?: number; height?: number }

/** Runs in the target document. Keep every dependency inside this function: callers serialize it. */
export function browserDOM(
  query: BrowserDOMQuery,
  operation: BrowserDOMOperation,
  value?: BrowserDOMValue,
): BrowserDOMResult {
  const normalize = (text: string) => text.replace(/\s+/gu, ' ').trim()
  const match = (actual: string, expected: string, exact = false) =>
    exact
      ? normalize(actual) === normalize(expected)
      : normalize(actual).toLocaleLowerCase().includes(normalize(expected).toLocaleLowerCase())
  const parent = (element: Element): Element | null =>
    element.parentElement ?? (element.getRootNode() as ShadowRoot).host ?? null
  const disabled = (element: Element) => {
    if (element.matches(':disabled')) return true
    for (let current: Element | null = element; current; current = parent(current)) {
      if (current.getAttribute('aria-disabled') === 'true' || current.hasAttribute('inert'))
        return true
    }
    return false
  }
  // Ancestors are shared by many candidates; retain style results for this invocation only.
  const styleVisibility = new Map<Element, boolean>()
  const visible = (element: Element) => {
    if (!element.isConnected) return false
    const checked: Element[] = []
    let shown = true
    for (let current: Element | null = element; current; current = parent(current)) {
      const cached = styleVisibility.get(current)
      if (cached !== undefined) {
        shown = cached
        break
      }
      checked.push(current)
      const style = getComputedStyle(current)
      if (
        style.display === 'none' ||
        style.visibility === 'hidden' ||
        style.visibility === 'collapse' ||
        current.hasAttribute('hidden')
      ) {
        shown = false
        break
      }
    }
    for (const current of checked) styleVisibility.set(current, shown)
    if (!shown) return false
    const rect = element.getBoundingClientRect()
    return rect.width > 0 && rect.height > 0
  }
  const implicitRoles: Record<string, string> = {
    button: 'button',
    textarea: 'textbox',
    option: 'option',
    optgroup: 'group',
    img: 'img',
    progress: 'progressbar',
    meter: 'meter',
    summary: 'button',
    h1: 'heading',
    h2: 'heading',
    h3: 'heading',
    h4: 'heading',
    h5: 'heading',
    h6: 'heading',
    ul: 'list',
    ol: 'list',
    li: 'listitem',
    table: 'table',
    tr: 'row',
    td: 'cell',
    th: 'columnheader',
    nav: 'navigation',
    main: 'main',
    aside: 'complementary',
    article: 'article',
    dialog: 'dialog',
    fieldset: 'group',
  }
  const role = (element: Element): string => {
    const explicit = element.getAttribute('role')?.trim().split(/\s+/u)[0]
    if (explicit) return explicit
    const tag = element.localName
    if (tag === 'a' || tag === 'area') return element.hasAttribute('href') ? 'link' : ''
    if (tag === 'input') {
      const type = (element.getAttribute('type') ?? 'text').toLowerCase()
      if (type === 'hidden' || type === 'password') return ''
      if (['button', 'submit', 'reset', 'image'].includes(type)) return 'button'
      if (type === 'checkbox' || type === 'radio') return type
      if (type === 'range') return 'slider'
      if (type === 'number') return 'spinbutton'
      if (type === 'search') return 'searchbox'
      return element.hasAttribute('list') ? 'combobox' : 'textbox'
    }
    if (tag === 'select')
      return (element as HTMLSelectElement).multiple || (element as HTMLSelectElement).size > 1
        ? 'listbox'
        : 'combobox'
    return implicitRoles[tag] ?? ''
  }
  const labelText = (element: Element): string => {
    const text = (node: Node): string => {
      if (node.nodeType === 3) return node.textContent ?? ''
      if (
        node !== element &&
        node.nodeType === 1 &&
        ['input', 'select', 'textarea', 'button', 'option', 'script', 'style'].includes(
          (node as Element).localName,
        )
      )
        return ''
      return Array.from(node.childNodes, text).join('')
    }
    return normalize(text(element))
  }
  const name = (element: Element, visited = new Set<Element>()): string => {
    if (visited.has(element)) return ''
    visited.add(element)
    const references = element.getAttribute('aria-labelledby')?.trim().split(/\s+/u)
    if (references?.length) {
      const root = element.getRootNode() as Document | ShadowRoot
      const result = references
        .map((id) => {
          const target = root.getElementById(id)
          return target ? name(target, visited) : ''
        })
        .filter(Boolean)
        .join(' ')
      if (result) return normalize(result)
    }
    const aria = element.getAttribute('aria-label')
    if (aria !== null) return normalize(aria)
    const labels = (element as HTMLInputElement).labels
    if (labels?.length) return normalize(Array.from(labels, labelText).join(' '))
    if (
      element.localName === 'img' ||
      (element.localName === 'input' && element.getAttribute('type') === 'image')
    )
      return element.getAttribute('alt') ?? ''
    if (
      element.localName === 'input' &&
      ['button', 'submit', 'reset'].includes((element as HTMLInputElement).type)
    )
      return (
        (element as HTMLInputElement).value ||
        (element.getAttribute('type') === 'submit'
          ? 'Submit'
          : element.getAttribute('type') === 'reset'
            ? 'Reset'
            : '')
      )
    const text = normalize(element.textContent ?? '')
    return text ? text : (element.getAttribute('title') ?? '')
  }
  const roots: (Document | ShadowRoot)[] = [document]
  const elements: Element[] = []
  for (const root of roots) {
    for (const element of Array.from(root.querySelectorAll('*'))) {
      elements.push(element)
      if (element.shadowRoot) roots.push(element.shadowRoot)
    }
  }
  const selector = query.selector
  let matches: Element[]
  if (selector.kind === 'css') {
    matches = roots.flatMap((root) => Array.from(root.querySelectorAll(selector.value)))
  } else {
    matches = elements.filter((element) => {
      if (selector.kind === 'testId') return element.getAttribute('data-testid') === selector.value
      if (selector.kind === 'placeholder')
        return (
          element.hasAttribute('placeholder') &&
          match(element.getAttribute('placeholder') ?? '', selector.value, selector.exact)
        )
      if (selector.kind === 'label') {
        const labels = (element as HTMLInputElement).labels
        return (
          (!!labels &&
            Array.from(labels).some((label) =>
              match(labelText(label), selector.value, selector.exact),
            )) ||
          (element.hasAttribute('aria-label') &&
            match(element.getAttribute('aria-label') ?? '', selector.value, selector.exact)) ||
          (element.hasAttribute('aria-labelledby') &&
            match(name(element), selector.value, selector.exact))
        )
      }
      if (selector.kind === 'role') {
        if (
          role(element) !== selector.value ||
          (selector.name !== undefined && !match(name(element), selector.name, selector.exact)) ||
          !visible(element)
        )
          return false
        for (let current: Element | null = element; current; current = parent(current))
          if (current.getAttribute('aria-hidden') === 'true') return false
        return true
      }
      return (
        !['script', 'style', 'head', 'title', 'noscript'].includes(element.localName) &&
        match(element.textContent ?? '', selector.value, selector.exact)
      )
    })
    if (selector.kind === 'text') {
      // Keep the most specific matches without quadratic pairwise containment checks.
      const ancestors = new Set<Element>()
      for (const element of matches)
        for (let current = parent(element); current; current = parent(current))
          ancestors.add(current)
      matches = matches.filter((element) => !ancestors.has(element))
    }
  }
  if (operation === 'count') return matches.length
  if (query.index !== undefined && (!Number.isInteger(query.index) || query.index < -1))
    throw new Error('Locator index must be -1 or a non-negative integer')
  if (query.index === undefined && matches.length > 1)
    return { retry: `Strict selector resolved to ${matches.length} elements` }
  const element = matches[query.index === -1 ? matches.length - 1 : (query.index ?? 0)]
  if (!element)
    return operation === 'visible' || operation === 'enabled'
      ? false
      : { retry: 'Element was not found' }
  if (!element.isConnected) return { retry: 'Element is detached' }
  if (operation === 'element') return element
  if (operation === 'prepareUpload') {
    if (element.localName !== 'input' || (element as HTMLInputElement).type !== 'file')
      throw new Error('Element is not a file input')
    if (disabled(element)) return { retry: 'Element is disabled' }
    return element
  }
  if (operation === 'textContent') return element.textContent
  if (operation === 'innerText') return (element as HTMLElement).innerText ?? element.textContent
  if (operation === 'getAttribute') {
    if (typeof value !== 'string') throw new Error('Attribute name must be a string')
    return element.getAttribute(value)
  }
  if (operation === 'visible') return visible(element)
  if (operation === 'enabled') return !disabled(element)
  if (operation === 'checked') {
    if (
      element.localName !== 'input' ||
      !['checkbox', 'radio'].includes((element as HTMLInputElement).type)
    )
      throw new Error('Element is not a checkbox or radio input')
    return (element as HTMLInputElement).checked
  }
  if (operation === 'box') {
    if (!visible(element)) return null
    const { x, y, width, height } = element.getBoundingClientRect()
    return { x, y, width, height }
  }
  if (operation !== 'prepareHover' && operation !== 'scroll' && disabled(element))
    return { retry: 'Element is disabled' }
  if (!visible(element)) return { retry: 'Element is hidden' }
  element.scrollIntoView({ block: 'center', inline: 'center', behavior: 'instant' })
  if (!element.isConnected) return { retry: 'Element is detached' }
  if (operation === 'scroll') return true
  if (operation === 'focus') {
    ;(element as HTMLElement).focus()
    return true
  }
  if (operation === 'prepareClick' || operation === 'prepareHover') {
    const rect = element.getBoundingClientRect()
    const left = Math.max(0, rect.left),
      top = Math.max(0, rect.top)
    const right = Math.min(innerWidth, rect.right),
      bottom = Math.min(innerHeight, rect.bottom)
    if (right <= left || bottom <= top) return { retry: 'Element is outside the viewport' }
    const x = (left + right) / 2,
      y = (top + bottom) / 2
    let target = document.elementFromPoint(x, y)
    while (target?.shadowRoot) {
      const nested = target.shadowRoot.elementFromPoint(x, y)
      if (!nested || nested === target) break
      target = nested
    }
    for (let current = target; current; current = parent(current))
      if (current === element) return { x, y }
    return { retry: 'Element is covered by another element' }
  }
  if (operation === 'prepareFill') {
    const html = element as HTMLElement
    const input = element as HTMLInputElement
    if (input.readOnly) return { retry: 'Element is read-only' }
    if (
      element.localName === 'input' &&
      ['date', 'time', 'datetime-local', 'month', 'week'].includes(input.type)
    ) {
      if (typeof value !== 'string') throw new Error('Fill value must be a string')
      const candidate = document.createElement('input')
      candidate.type = input.type
      const descriptor = Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value')
      if (!descriptor?.set) throw new Error('Native input value setter is unavailable')
      descriptor.set.call(candidate, value)
      if (value !== '' && candidate.value === '')
        throw new Error(`Invalid ${input.type} value: ${value}`)
      input.focus()
      descriptor.set.call(input, candidate.value)
      input.dispatchEvent(new Event('input', { bubbles: true, composed: true }))
      input.dispatchEvent(new Event('change', { bubbles: true }))
      return { filled: true }
    }
    if (
      element.localName === 'textarea' ||
      (element.localName === 'input' &&
        ['text', 'search', 'email', 'tel', 'url', 'password', 'number'].includes(input.type))
    ) {
      input.focus()
      if (input.type === 'number') return { selectAll: true }
      if (typeof input.select === 'function') input.select()
      return true
    }
    if (html.isContentEditable) {
      html.focus()
      const range = document.createRange()
      range.selectNodeContents(element)
      const selection = getSelection()
      selection?.removeAllRanges()
      selection?.addRange(range)
      return true
    }
    throw new Error('Element is not a supported text input, textarea, or contenteditable element')
  }
  if (operation === 'selectOption') {
    if (element.localName !== 'select') throw new Error('Element is not a select')
    const select = element as HTMLSelectElement
    const wanted = value === undefined ? undefined : Array.isArray(value) ? value : [value]
    if (!wanted) throw new Error('Select choices must be provided')
    for (const item of wanted) {
      if (typeof item === 'string') continue
      if (
        !item ||
        typeof item !== 'object' ||
        (item.value === undefined && item.label === undefined && item.index === undefined) ||
        (item.value !== undefined && typeof item.value !== 'string') ||
        (item.label !== undefined && typeof item.label !== 'string') ||
        (item.index !== undefined && (!Number.isInteger(item.index) || item.index < 0))
      )
        throw new Error('Select choices require a value, label, or non-negative integer index')
    }
    if (!select.multiple && wanted.length > 1)
      throw new Error('Cannot select multiple values in a single select')
    const options = Array.from(select.options)
    const selected = new Set<HTMLOptionElement>()
    for (const item of wanted) {
      const option = options.find((candidate, index) =>
        typeof item === 'string'
          ? candidate.value === item
          : (item.value === undefined || candidate.value === item.value) &&
            (item.label === undefined || candidate.label === item.label) &&
            (item.index === undefined || index === item.index),
      )
      if (!option) return { retry: 'Requested option was not found' }
      if (
        option.disabled ||
        (option.parentElement?.localName === 'optgroup' &&
          (option.parentElement as HTMLOptGroupElement).disabled)
      )
        return { retry: 'Requested option is disabled' }
      selected.add(option)
    }
    for (const option of options) option.selected = selected.has(option)
    if (!wanted.length) select.selectedIndex = -1
    select.dispatchEvent(new Event('input', { bubbles: true, composed: true }))
    select.dispatchEvent(new Event('change', { bubbles: true }))
    return options.filter((option) => option.selected).map((option) => option.value)
  }
  throw new Error(`Unknown DOM operation: ${String(operation)}`)
}
