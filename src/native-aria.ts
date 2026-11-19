// SPDX-License-Identifier: MIT
import { createRequire } from 'node:module'

import * as z from 'zod'

import { nativeMatcherFailure, nativeMatcherMessage } from './native-matcher-failure.js'
import { createNativeValueCodec } from './native-values.js'
import { type UntrustedInput } from './untrusted.js'

// Keep this lifecycle edge type-only; an inline type import emits a runtime cycle.
// eslint-disable-next-line import-x/consistent-type-specifier-style
import type { NativeLocator } from './native-page.js'
import type * as YAML from 'yaml'

// AI snapshots are already formatted by C++. Only template matching needs a
// YAML parser; keep that optional work off ordinary browser startup and reads.
const require = createRequire(import.meta.url)
let yaml: Pick<typeof YAML, 'isMap' | 'isScalar' | 'isSeq' | 'parseDocument'> | undefined

export interface NativeAriaText {
  text?: string
  pattern?: string
  flags?: string
}
export type NativeAriaState = boolean | number | 'mixed' | 'grammar' | 'spelling'
export interface NativeAriaSnapshotOptions {
  mode?: 'expect' | 'ai' | undefined
  timeout?: number | undefined
  depth?: number | undefined
  boxes?: boolean | undefined
  /**
   * With `mode: 'ai'`, lists only what an agent can act on: controls, headings
   * and the named landmarks around them, without text or URLs. Refs are the
   * same as in the full AI snapshot.
   */
  interactive?: boolean | undefined
}
export interface NativeAriaSnapshotJSONOptions {
  mode?: 'default' | 'ai' | undefined
  timeout?: number | undefined
  signal?: AbortSignal | undefined
  depth?: number | undefined
  boxes?: boolean | undefined
}
export interface NativeAriaJSONNode {
  role: string
  name?: string
  text?: string
  checked?: boolean | 'mixed'
  disabled?: boolean
  expanded?: boolean
  active?: boolean
  invalid?: boolean | 'grammar' | 'spelling'
  level?: number
  pressed?: boolean | 'mixed'
  selected?: boolean
  url?: string
  placeholder?: string
  ref?: string
  cursor?: 'pointer'
  inputType?: string
  readOnly?: boolean
  editable?: boolean
  box?: { x: number; y: number; width: number; height: number }
  children?: (NativeAriaJSONNode | string)[]
}
export interface NativeAriaNode {
  ref?: string
  cursor?: 'pointer'
  inputType?: string
  readOnly?: boolean
  editable?: boolean
  role: string
  name?: string
  text?: string
  states?: Record<string, NativeAriaState>
  invalidToken?: 'grammar' | 'spelling'
  properties?: Record<string, string>
  box?: [number, number, number, number]
  children?: NativeAriaNode[]
}
export interface NativeAriaPattern {
  role: string
  name?: NativeAriaText
  text?: NativeAriaText
  states: Record<string, NativeAriaState>
  properties: Record<string, NativeAriaText>
  children: NativeAriaPattern[]
  childMode?: 'contain' | 'equal' | 'deep-equal'
}

/** Removes common indentation while preserving relative YAML indentation. */
export function normalizeNativeAriaTemplate(template: string): string {
  const lines = template.replaceAll('\r\n', '\n').split('\n')
  while (lines.length && !lines[0]?.trim()) lines.shift()
  while (lines.length && !lines.at(-1)?.trim()) lines.pop()
  const indentation = Math.min(
    ...lines.filter((line) => line.trim()).map((line) => /^ */u.exec(line)?.[0].length ?? 0),
  )
  return lines.map((line) => line.slice(indentation)).join('\n')
}

function ariaKey(key: string): NativeAriaPattern {
  const result: NativeAriaPattern = { role: '', states: {}, properties: {}, children: [] }
  let cursor = 0
  const skip = () => {
    while (/\s/u.test(key[cursor] ?? '') && cursor < key.length) ++cursor
  }
  function fail(reason: string, at = cursor): never {
    throw new Error(`${reason}:\n\n${key}\n${' '.repeat(at)}^`)
  }
  const role = /^[a-z][a-z0-9-]*/u.exec(key)
  if (!role) fail('Expected an accessible role', 0)
  result.role = role[0]
  cursor = role[0].length
  skip()
  if (key[cursor] === '"') {
    const start = cursor++
    let escaped = false,
      closed = false
    while (cursor < key.length) {
      const character = key[cursor++]
      if (escaped) {
        escaped = false
        continue
      }
      if (character === '\\') {
        escaped = true
        continue
      }
      if (character === '"') {
        closed = true
        break
      }
    }
    if (!closed) fail('Unterminated string', cursor)
    const value = JSON.parse(key.slice(start, cursor)) as UntrustedInput
    if (typeof value !== 'string') fail('Accessible name must be a string', start)
    result.name = {
      text: value
        .replace(/[\u200b\u00ad]/gu, '')
        .replace(/\s+/gu, ' ')
        .trim(),
    }
  } else if (key[cursor] === '/') {
    const start = cursor++
    let escaped = false,
      characterClass = false,
      closed = false
    while (cursor < key.length) {
      const character = key[cursor++]
      if (escaped) {
        escaped = false
        continue
      }
      if (character === '\\') {
        escaped = true
        continue
      }
      if (character === '[') characterClass = true
      else if (character === ']') characterClass = false
      else if (character === '/' && !characterClass) {
        closed = true
        break
      }
    }
    if (!closed) fail('Unterminated regex', cursor)
    const pattern = key.slice(start + 1, cursor - 1)
    const flags = /^[a-z]*/u.exec(key.slice(cursor))?.[0] ?? ''
    cursor += flags.length
    new RegExp(pattern, flags)
    result.name = { pattern, flags }
  }
  skip()
  while (key[cursor] === '[') {
    const start = cursor++
    skip()
    const attribute = /^[a-z-]+/u.exec(key.slice(cursor))?.[0]
    if (!attribute) fail('Expected an accessible state')
    const attributeStart = cursor
    cursor += attribute.length
    skip()
    let value = 'true',
      valueStart = cursor
    if (key[cursor] === '=') {
      ++cursor
      skip()
      valueStart = cursor
      while (cursor < key.length && key[cursor] !== ']' && !/\s/u.test(key[cursor] ?? '')) ++cursor
      value = key.slice(valueStart, cursor)
      skip()
    }
    if (key[cursor] !== ']') fail('Unterminated attribute', cursor)
    ++cursor
    if (
      !['checked', 'disabled', 'expanded', 'invalid', 'level', 'pressed', 'selected'].includes(
        attribute,
      )
    )
      fail(`Unsupported attribute [${attribute}]`, attributeStart)
    if (attribute === 'level') {
      if (!/^-?\d+$/u.test(value)) fail('Value of "level" attribute must be a number', valueStart)
      const number = Number(value)
      if (!Number.isSafeInteger(number))
        fail('Value of "level" attribute must be a safe number', start)
      result.states[attribute] = number
    } else if ((attribute === 'checked' || attribute === 'pressed') && value === 'mixed') {
      result.states[attribute] = 'mixed'
    } else if (attribute === 'invalid' && (value === 'grammar' || value === 'spelling')) {
      result.states[attribute] = value
    } else if (value === 'true' || value === 'false') {
      result.states[attribute] = value === 'true'
    } else {
      const kind =
        attribute === 'checked' || attribute === 'pressed' ? 'a boolean or "mixed"' : 'a boolean'
      fail(`Value of "${attribute}" attribute must be ${kind}`, valueStart)
    }
    skip()
  }
  if (cursor !== key.length) fail('Unexpected input')
  return result
}

function ariaText(value: UntrustedInput, literal = false, property = false): NativeAriaText {
  if (typeof value !== 'string' && typeof value !== 'number' && typeof value !== 'boolean')
    throw new Error('Accessible text or property must be a string')
  const text = String(value)
  if (!literal && text.startsWith('/') && /\/[a-z]*$/u.test(text.slice(1))) {
    const end = text.lastIndexOf('/')
    const pattern = text.slice(1, end),
      flags = text.slice(end + 1)
    // A malformed property regex remains a literal. This permits genuine href
    // values such as /[a/ while preserving ordinary valid regex matching.
    try {
      new RegExp(pattern, flags)
      return { pattern, flags, ...(property ? { text } : {}) }
    } catch {
      return { text }
    }
  }
  return {
    text: property
      ? text
      : text
          .replace(/[\u200b\u00ad]/gu, '')
          .replace(/\s+/gu, ' ')
          .trim(),
  }
}

/** Parses declarative YAML; aliases cannot create recursive templates. */
export function parseNativeAriaTemplate(template: string): NativeAriaPattern {
  const { isMap, isScalar, isSeq, parseDocument } = (yaml ??= require('yaml') as NonNullable<
    typeof yaml
  >)
  const normalized = normalizeNativeAriaTemplate(template)
  const document = parseDocument(normalized, { strict: true, uniqueKeys: true })
  if (document.errors[0]) throw new Error(document.errors[0].message.trimEnd())
  document.toJS({ maxAliasCount: 0 })
  if (!isSeq(document.contents))
    throw new Error('Aria snapshot must be a YAML sequence, elements starting with " -"')
  const populate = (parent: NativeAriaPattern, entries: YAML.YAMLSeq): void => {
    for (const entry of entries.items) {
      if (isScalar(entry) && typeof entry.value === 'string') {
        parent.children.push(ariaKey(entry.value))
        continue
      }
      if (!isMap(entry))
        throw new Error('Native ARIA template entries must be role strings or mappings')
      if (entry.items.length !== 1)
        throw new Error('Native ARIA template mappings require exactly one key')
      const pair = entry.items[0]
      if (!pair || !isScalar(pair.key) || typeof pair.key.value !== 'string')
        throw new Error('Native ARIA template key is missing')
      const key = pair.key.value
      const scalar = isScalar(pair.value) ? pair.value : undefined
      const value = scalar?.value as UntrustedInput
      if (key === '/children') {
        if (value !== 'contain' && value !== 'equal' && value !== 'deep-equal')
          throw new Error('Native ARIA /children must be contain, equal, or deep-equal')
        parent.childMode = value
        continue
      }
      if (key.startsWith('/')) {
        Object.defineProperty(parent.properties, key.slice(1), {
          value: ariaText(value, scalar?.type !== 'PLAIN', true),
          enumerable: true,
          writable: true,
          configurable: true,
        })
        continue
      }
      const child = ariaKey(key)
      if (isSeq(pair.value)) populate(child, pair.value)
      else if (scalar && value !== null) child.text = ariaText(value, scalar.type !== 'PLAIN')
      else if (pair.value !== null && !(scalar && value === null))
        throw new Error('Native ARIA role children must be a YAML sequence or scalar text')
      parent.children.push(child)
    }
  }
  const root: NativeAriaPattern = { role: '', states: {}, properties: {}, children: [] }
  populate(root, document.contents)
  return root
}

/** Pure comparator is serialized into the renderer; C++ owns retries/deadlines. */
export function matchNativeAriaTree(
  actual: NativeAriaNode[],
  expected: NativeAriaPattern,
): boolean {
  const regexes = new Map<NativeAriaText, RegExp>()
  const textMatches = (actualText: string, wanted: NativeAriaText): boolean => {
    if (wanted.pattern !== undefined) {
      if (wanted.text !== undefined && actualText === wanted.text) return true
      let regex = regexes.get(wanted)
      if (!regex) {
        regex = new RegExp(wanted.pattern, wanted.flags ?? '')
        regexes.set(wanted, regex)
      }
      regex.lastIndex = 0
      return regex.test(actualText)
    }
    return actualText === (wanted.text ?? '')
  }
  const forest = (
    received: NativeAriaNode[],
    wanted: NativeAriaPattern[],
    mode: 'contain' | 'equal' | 'deep-equal',
    depth: number,
  ): boolean => {
    if (mode !== 'contain' && received.length !== wanted.length) return false
    let index = 0
    for (const pattern of wanted) {
      let matched = false
      while (index < received.length) {
        const node = received[index++]
        if (node && matches(node, pattern, mode === 'deep-equal', depth + 1)) {
          matched = true
          break
        }
        if (mode !== 'contain') return false
      }
      if (!matched) return false
    }
    return true
  }
  const matches = (
    node: NativeAriaNode,
    pattern: NativeAriaPattern,
    deepEqual: boolean,
    depth: number,
  ): boolean => {
    if (node.role !== pattern.role) return false
    if (pattern.name && !textMatches(node.name ?? '', pattern.name)) return false
    for (const [key, value] of Object.entries(pattern.states)) {
      const rawState = node.states?.[key]
      const actualState =
        key === 'invalid' && rawState === true && node.invalidToken
          ? node.invalidToken
          : (rawState ?? (key === 'level' ? undefined : false))
      if (actualState !== value) return false
    }
    for (const [key, value] of Object.entries(pattern.properties)) {
      if (!node.properties || !Object.hasOwn(node.properties, key)) return false
      const received = node.properties[key]
      if (typeof received !== 'string' || !textMatches(received, value)) return false
    }
    const children = node.children ?? []
    if (pattern.text) {
      const value =
        node.text ??
        children
          .filter((child) => child.role === 'text')
          .map((child) => child.text ?? child.name ?? '')
          .join(' ')
      if (!textMatches(value, pattern.text)) return false
    }
    const mode = pattern.childMode ?? (deepEqual ? 'deep-equal' : 'contain')
    return forest(
      pattern.text ? children.filter((child) => child.role !== 'text') : children,
      pattern.children,
      mode,
      depth,
    )
  }
  const mode = expected.childMode ?? 'contain'
  const queue: { nodes: NativeAriaNode[]; depth: number }[] = [{ nodes: actual, depth: 0 }]
  while (queue.length) {
    const scope = queue.pop()
    if (!scope) break
    if (forest(scope.nodes, expected.children, mode, scope.depth)) return true
    if (mode !== 'contain') continue
    for (const node of scope.nodes) {
      if (node.children?.length) queue.push({ nodes: node.children, depth: scope.depth + 1 })
    }
  }
  return false
}

/** Emits only actual semantic data, with bounded output and no regex substitutions. */
export function formatNativeAriaTree(
  nodes: NativeAriaNode[],
  options: { depth?: number; boxes?: boolean; mode?: 'expect' | 'ai' } = {},
): string {
  const output: string[] = []
  const squeeze = (value: string) => value.replace(/\s+/gu, '')
  // Whether a name only repeats the text its rendered children already show.
  // The walk stops once it has read past the name, so it stays cheap.
  const repeatsChildren = (name: string, children: NativeAriaNode[]) => {
    const target = squeeze(name)
    let seen = 0
    let steps = target.length * 4 + 64
    const pending = [...children].reverse()
    while (pending.length) {
      const node = pending.pop()
      if (!node || --steps < 0) return false
      const own = node.role === 'text' ? (node.text ?? node.name ?? '') : (node.text ?? '')
      const grand = node.role === 'text' ? [] : (node.children ?? [])
      const piece = squeeze(own || (grand.length ? '' : (node.name ?? '')))
      if (!target.startsWith(piece, seen)) return false
      seen += piece.length
      for (let index = grand.length - 1; index >= 0; --index) {
        const child = grand[index]
        if (child) pending.push(child)
      }
    }
    return seen === target.length
  }
  const scalar = (value: string, property = false) =>
    /:(?:\s|$)|[\n\r[\]{}]|^[,'"#&*!>|%@`-]/u.test(value) ||
    /^(?:true|false|yes|no|y|n|on|off|null|~|[-+]?\d+(?:\.\d+)?(?:e[-+]?\d+)?|\.inf|\.nan)$/iu.test(
      value,
    ) ||
    (!property && value.startsWith('/')) ||
    !value.trim()
      ? JSON.stringify(value)
      : value
  const append = (line: string) => {
    output.push(line)
  }
  const stack = nodes.map((node) => ({ node, depth: 0 })).reverse()
  while (stack.length) {
    const item = stack.pop()
    if (!item) break
    const { node, depth } = item
    const indent = '  '.repeat(depth)
    if (node.role === 'text') {
      append(`${indent}- text: ${scalar(node.text ?? node.name ?? '')}`)
      continue
    }
    const properties = Object.entries(node.properties ?? {})
    let children = node.children ?? []
    let text = node.text && node.text !== node.name ? node.text : undefined
    if (children.length === 1 && children[0]?.role === 'text' && !node.text) {
      const value = children[0].text ?? children[0].name ?? ''
      if (value !== node.name) text = value
      children = []
    }
    const inlineText = text !== undefined && !children.length && !properties.length
    if (text !== undefined && !inlineText) children = [{ role: 'text', text }, ...children]
    if (depth >= (options.depth ?? Infinity)) children = []
    // AI snapshots drop a name its children already spell out.
    const namePreview =
      node.name &&
      !(options.mode === 'ai' && children.length && repeatsChildren(node.name, children))
        ? node.name
        : undefined
    let line = `${indent}- ${node.role}${namePreview ? ` ${JSON.stringify(namePreview)}` : ''}`
    for (const key of [
      'checked',
      'disabled',
      'expanded',
      'invalid',
      'level',
      'pressed',
      'selected',
    ]) {
      if (key === 'level' && node.role !== 'heading') continue
      const rawState = node.states?.[key]
      const state =
        key === 'invalid' && rawState === true && node.invalidToken ? node.invalidToken : rawState
      if (state === true) line += ` [${key}]`
      else if (state !== undefined && state !== false) line += ` [${key}=${String(state)}]`
    }
    if (options.boxes) {
      if (node.box?.length !== 4 || node.box.some((value) => !Number.isFinite(value)))
        throw new Error('Native accessibility snapshot box is unavailable')
      line += ` [box=${node.box.join(',')}]`
    }
    if (options.mode === 'ai' && node.ref !== undefined) {
      if (!/^s[1-9][0-9]*e(?:0|[1-9][0-9]*)$/u.test(node.ref))
        throw new Error('Invalid native ARIA reference token')
      line += ` [ref=${node.ref}]`
    }
    if (inlineText && text !== undefined) line += `: ${scalar(text)}`
    if (children.length || properties.length) line += ':'
    append(line)
    for (const [key, value] of properties) append(`${indent}  - /${key}: ${scalar(value, true)}`)
    for (let index = children.length - 1; index >= 0; --index) {
      const child = children[index]
      if (child) stack.push({ node: child, depth: depth + 1 })
    }
  }
  return output.join('\n')
}

/** Serializes the actual semantic forest directly, without interpreting YAML. */
export function formatNativeAriaJSON(
  nodes: NativeAriaNode[],
  options: { depth?: number; boxes?: boolean; mode?: 'default' | 'ai' } = {},
): NativeAriaJSONNode[] {
  const result: NativeAriaJSONNode[] = []
  const pending: {
    values: NativeAriaNode[]
    output: (NativeAriaJSONNode | string)[]
    depth: number
  }[] = [{ values: nodes, output: result, depth: 0 }]
  while (pending.length) {
    const item = pending.pop()
    if (!item) break
    for (const actual of item.values) {
      if (actual.role === 'text') {
        const text = actual.text ?? actual.name ?? ''
        if (item.depth) item.output.push(text)
        else item.output.push({ role: 'text', text })
        continue
      }
      const node: NativeAriaJSONNode = { role: actual.role }
      if (actual.name) node.name = actual.name
      for (const key of [
        'checked',
        'disabled',
        'expanded',
        'active',
        'invalid',
        'level',
        'pressed',
        'selected',
      ] as const) {
        if (key === 'level' && actual.role !== 'heading') continue
        const state =
          key === 'invalid' && actual.states?.invalid === true && actual.invalidToken
            ? actual.invalidToken
            : actual.states?.[key]
        if (state === undefined) continue
        const valid =
          key === 'level'
            ? typeof state === 'number' && Number.isInteger(state) && state >= 1
            : key === 'checked' || key === 'pressed'
              ? typeof state === 'boolean' || state === 'mixed'
              : key === 'invalid'
                ? typeof state === 'boolean' || state === 'grammar' || state === 'spelling'
                : typeof state === 'boolean'
        if (!valid) throw new Error('Invalid native ARIA JSON state value')
        Object.defineProperty(node, key, {
          value: state,
          enumerable: true,
          configurable: true,
          writable: true,
        })
      }
      for (const key of ['url', 'placeholder'] as const)
        if (actual.properties?.[key] !== undefined) node[key] = actual.properties[key]
      if (options.boxes) {
        if (
          actual.box?.length !== 4 ||
          actual.box.some((value) => !Number.isFinite(value)) ||
          actual.box[2] < 0 ||
          actual.box[3] < 0
        )
          throw new Error('Native ARIA JSON actual box is unavailable')
        node.box = {
          x: actual.box[0],
          y: actual.box[1],
          width: actual.box[2],
          height: actual.box[3],
        }
      }
      if (options.mode === 'ai' && actual.ref !== undefined) {
        if (!/^s[1-9][0-9]*e(?:0|[1-9][0-9]*)$/u.test(actual.ref))
          throw new Error('Invalid native ARIA JSON reference token')
        node.ref = actual.ref
      }
      if (options.mode === 'ai' && actual.cursor === 'pointer') node.cursor = actual.cursor
      if (options.mode === 'ai') {
        if (actual.inputType !== undefined) node.inputType = actual.inputType
        for (const key of ['readOnly', 'editable'] as const)
          if (actual[key] !== undefined) node[key] = actual[key]
      }
      let children = actual.children ?? []
      let text = actual.text && actual.text !== actual.name ? actual.text : undefined
      if (options.mode === 'ai' && actual.inputType === 'password') {
        children = []
        text = undefined
      }
      if (children.length === 1 && children[0]?.role === 'text' && !actual.text) {
        const childText = children[0].text ?? children[0].name ?? ''
        if (childText !== actual.name) text = childText
        children = []
      }
      if (text !== undefined) node.text = text
      item.output.push(node)
      if (children.length && (options.depth === undefined || item.depth < options.depth)) {
        node.children = []
        pending.push({ values: children, output: node.children, depth: item.depth + 1 })
      }
    }
  }
  return result
}

/**
 * Formats only actionable nodes of an AI JSON snapshot, as agent-browser's
 * `snapshot -i`. The browser runs this same function, compiled into
 * native/shared/native_aria_interactive.h, so it must not use anything outside it.
 */
export function formatNativeAriaInteractive(
  nodes: NativeAriaJSONNode[],
  options: { boxes?: boolean } = {},
): string {
  const interactiveRoles = new Set([
    'button',
    'checkbox',
    'combobox',
    'link',
    'listbox',
    'menuitem',
    'menuitemcheckbox',
    'menuitemradio',
    'option',
    'radio',
    'scrollbar',
    'searchbox',
    'slider',
    'spinbutton',
    'switch',
    'tab',
    'textbox',
    'treeitem',
  ])
  // Containers kept for context when they are named and hold a control.
  const contextRoles = new Set([
    'alertdialog',
    'banner',
    'complementary',
    'contentinfo',
    'dialog',
    'form',
    'grid',
    'iframe',
    'main',
    'menu',
    'menubar',
    'navigation',
    'radiogroup',
    'region',
    'search',
    'tablist',
    'tabpanel',
    'toolbar',
    'tree',
  ])
  const output: string[] = []
  // A node's text is its own text and its descendants' in document order, with
  // whitespace collapsed. One walk splits the tree into those collapsed pieces
  // and records each node's run of them, so a node's text is a slice of the run
  // instead of a fresh walk of its subtree for every listed control.
  const pieces: string[] = []
  const spans = new Map<NativeAriaJSONNode, { start: number; end: number }>()
  const piece = (value: string) => {
    const text = value.replace(/\s+/gu, ' ').trim()
    if (text) pieces.push(text)
  }
  const walk: { node: NativeAriaJSONNode; start: number; index: number }[] = []
  // A text node's text is its own; its children are indexed as separate roots
  // after the walk, outside every ancestor's run.
  const detached: NativeAriaJSONNode[] = []
  const open = (node: NativeAriaJSONNode) => {
    const start = pieces.length
    if (node.role === 'text') {
      piece(node.text ?? node.name ?? '')
      spans.set(node, { start, end: pieces.length })
      for (const child of node.children ?? [])
        if (typeof child !== 'string' && child !== undefined) detached.push(child)
      return
    }
    if (node.text) piece(node.text)
    walk.push({ node, start, index: 0 })
  }
  const roots = [...nodes]
  for (let next = 0; next < roots.length || detached.length; ++next) {
    if (next >= roots.length) roots.push(...detached.splice(0))
    const root = roots[next]
    if (root === undefined) continue
    open(root)
    while (walk.length) {
      const frame = walk.at(-1)
      if (!frame) break
      const children = frame.node.children ?? []
      if (frame.index < children.length) {
        const child = children[frame.index++]
        if (typeof child === 'string') piece(child)
        else if (child !== undefined) open(child)
        continue
      }
      walk.pop()
      spans.set(frame.node, { start: frame.start, end: pieces.length })
    }
  }
  const textOf = (node: NativeAriaJSONNode): string => {
    const span = spans.get(node)
    return span ? pieces.slice(span.start, span.end).join(' ') : ''
  }
  const kept = (node: NativeAriaJSONNode) =>
    interactiveRoles.has(node.role) ||
    node.editable === true ||
    (node.cursor === 'pointer' && node.ref !== undefined)
  const line = (node: NativeAriaJSONNode, depth: number) => {
    const name = node.name?.length
      ? node.name
      : node.role === 'heading' || kept(node)
        ? textOf(node)
        : ''
    let text = `${'  '.repeat(depth)}- ${node.role}${name ? ` ${JSON.stringify(name)}` : ''}`
    for (const key of [
      'checked',
      'disabled',
      'expanded',
      'level',
      'pressed',
      'selected',
    ] as const) {
      const state = node[key]
      if (state === true) text += ` [${key}]`
      // A collapsed control says so, since an agent may need to expand it first.
      else if (state !== undefined && (state !== false || key === 'expanded'))
        text += ` [${key}=${String(state)}]`
    }
    if (node.placeholder) text += ` [placeholder=${JSON.stringify(node.placeholder)}]`
    if (options.boxes && node.box)
      text += ` [box=${[node.box.x, node.box.y, node.box.width, node.box.height].join(',')}]`
    if (node.ref !== undefined) text += ` [ref=${node.ref}]`
    if (
      (node.role === 'textbox' || node.role === 'searchbox' || node.role === 'combobox') &&
      node.text
    )
      text += `: ${JSON.stringify(node.text)}`
    return text
  }
  // Each node renders into its own buffer; a container is written only when a
  // control lies beneath it, and otherwise its controls rise to its parent.
  interface Frame {
    node: NativeAriaJSONNode
    depth: number
    lines: string[]
    index: number
    // Whether this node is listed as a control, and whether one encloses it.
    own: boolean
    control: boolean
  }
  const visit = (roots: NativeAriaJSONNode[]): string[] => {
    const top: string[] = []
    const stack: Frame[] = []
    const enter = (node: NativeAriaJSONNode, depth: number, own: boolean, control: boolean) =>
      stack.push({ node, depth, lines: [], index: 0, own, control })
    const emit = (lines: string[]) => {
      const parent = stack.at(-1)
      ;(parent ? parent.lines : top).push(...lines)
    }
    const depthOf = () => {
      for (let index = stack.length - 1; index >= 0; --index) {
        const frame = stack[index]
        if (frame && frame.depth >= 0) return frame.depth + 1
      }
      return 0
    }
    const push = (node: NativeAriaJSONNode) => {
      const inside = stack.at(-1)?.control ?? false
      // A pointer cursor inside a control is the control's own, not another one.
      const pointerOnly =
        !interactiveRoles.has(node.role) && node.editable !== true && node.cursor === 'pointer'
      const own = kept(node) && !(inside && pointerOnly)
      const self =
        own ||
        node.role === 'heading' ||
        (contextRoles.has(node.role) && (!!node.name || node.role === 'dialog'))
      enter(node, self ? depthOf() : -1, own, inside || own)
    }
    for (let index = roots.length - 1; index >= 0; --index) {
      const root = roots[index]
      if (root) push(root)
      while (stack.length) {
        const frame = stack.at(-1)
        if (!frame) break
        const child = frame.node.children?.[frame.index++]
        if (child !== undefined) {
          if (typeof child !== 'string' && child.role !== 'text') push(child)
          continue
        }
        stack.pop()
        const { node, depth, lines, own } = frame
        if (depth < 0) emit(lines)
        else if (own || node.role === 'heading' || lines.length) emit([line(node, depth), ...lines])
      }
    }
    return top
  }
  // Roots are visited last to first above, so restore document order per root.
  for (const root of nodes) output.push(...visit([root]))
  return output.join('\n')
}

const AriaResult = z.object({
  result: z
    .object({
      value: z.object({
        matches: z.boolean(),
        actual: z.union([
          z.tuple([z.literal('primitive'), z.string()]),
          z.tuple([z.literal('utf16'), z.string().regex(/^(?:[0-9a-f]{4})*$/u)]),
        ]),
        count: z.number().int().nonnegative(),
      }),
    })
    .optional(),
  exceptionDetails: z
    .object({
      text: z.string(),
      exception: z.object({ description: z.string().optional() }).optional(),
    })
    .optional(),
  closedReason: z.string().optional(),
})

/** One native scheduler invocation owns every actual accessibility observation. */
export async function observeNativeAria(
  locator: NativeLocator,
  options: NativeAriaSnapshotOptions = {},
  expected?: NativeAriaPattern,
  isNot = false,
): Promise<{ matches: boolean; actual: string; count: number; closedReason?: string }> {
  const timeout = z
    .number()
    .int()
    .min(0)
    .max(120000)
    .parse(options.timeout ?? locator.page().defaultTimeout())
  const codec = createNativeValueCodec()
  const formatOptions = z
    .object({ depth: z.number().int().min(0).optional(), boxes: z.boolean().optional() })
    .parse(options)
  const wanted =
    expected === undefined ? 'undefined' : `JSON.parse(${JSON.stringify(JSON.stringify(expected))})`
  const declaration = `function(nodes,metadata,forest){
    if(nodes.length>1)throw new Error('strict mode violation: native aria snapshot scope resolved to '+nodes.length+' elements');
    if(nodes.length&&!Array.isArray(forest))throw new Error('Native accessibility snapshot is unavailable');
    const actual=(${formatNativeAriaTree.toString()})(forest??[],${JSON.stringify(formatOptions)});
    const expected=${wanted};
    const matches=nodes.length===1&&(expected===undefined||(${matchNativeAriaTree.toString()})(forest,expected));
    const codec=(${createNativeValueCodec.toString()})();return {matches,actual:codec.encode(actual),count:nodes.length};
  }`
  const raw = await locator.runExpectation({
    function: declaration,
    dom: true,
    accessibilitySnapshot: true,
    accessibilitySemantics: true,
    accessibilityBoxes: formatOptions.boxes ?? false,
    timeoutMs: timeout,
    isNot,
  })
  const exception = AriaResult.pick({ exceptionDetails: true }).parse(raw).exceptionDetails
  if (exception) throw new Error(exception.exception?.description ?? exception.text)
  const parsed = AriaResult.parse(raw)
  const value = parsed.result?.value
  if (!value) throw new Error('Native accessibility observation unavailable')
  return {
    matches: value.matches,
    actual: z.string().parse(codec.decode(value.actual)),
    count: value.count,
    ...(parsed.closedReason === undefined ? {} : { closedReason: parsed.closedReason }),
  }
}

/** Validates only the fixed native semantic-forest schema, never author values. */
export function parseNativeReferenceForest(serialized: string): NativeAriaNode[] {
  z.string().parse(serialized)
  const header = z.strictObject({
    role: z.string().regex(/^[a-z][a-z0-9-]*$/u),
    name: z.string().optional(),
    text: z.string().optional(),
    ref: z
      .string()
      .regex(/^s[1-9][0-9]*e(?:0|[1-9][0-9]*)$/u)
      .optional(),
    cursor: z.literal('pointer').optional(),
    inputType: z.string().optional(),
    readOnly: z.boolean().optional(),
    editable: z.boolean().optional(),
    states: z
      .record(
        z.string().regex(/^(?:checked|disabled|expanded|active|invalid|level|pressed|selected)$/u),
        z.union([z.boolean(), z.number().finite(), z.enum(['mixed', 'grammar', 'spelling'])]),
      )
      .optional(),
    invalidToken: z.enum(['grammar', 'spelling']).optional(),
    properties: z.record(z.string().regex(/^(?:url|placeholder)$/u), z.string()).optional(),
    box: z
      .tuple([z.number().finite(), z.number().finite(), z.number().finite(), z.number().finite()])
      .optional(),
    children: z.array(z.unknown()).optional(),
  })
  const result: NativeAriaNode[] = []
  const pending = [
    {
      values: z.array(z.unknown()).parse(JSON.parse(serialized) as UntrustedInput),
      output: result,
      depth: 0,
    },
  ]
  while (pending.length) {
    const level = pending.pop()
    if (!level) break
    for (const value of level.values) {
      const actual = header.parse(value)
      if (actual.role === 'text' && actual.ref !== undefined)
        throw new Error('Native ARIA text records cannot own Element references')
      const node: NativeAriaNode = {
        role: actual.role,
        ...(actual.name === undefined ? {} : { name: actual.name }),
        ...(actual.text === undefined ? {} : { text: actual.text }),
        ...(actual.ref === undefined ? {} : { ref: actual.ref }),
        ...(actual.cursor === undefined ? {} : { cursor: actual.cursor }),
        ...(actual.inputType === undefined ? {} : { inputType: actual.inputType }),
        ...(actual.readOnly === undefined ? {} : { readOnly: actual.readOnly }),
        ...(actual.editable === undefined ? {} : { editable: actual.editable }),
        ...(actual.states === undefined ? {} : { states: actual.states }),
        ...(actual.invalidToken === undefined ? {} : { invalidToken: actual.invalidToken }),
        ...(actual.properties === undefined ? {} : { properties: actual.properties }),
        ...(actual.box === undefined ? {} : { box: actual.box }),
      }
      level.output.push(node)
      if (actual.children !== undefined) {
        node.children = []
        pending.push({ values: actual.children, output: node.children, depth: level.depth + 1 })
      }
    }
  }
  return result
}

/** Validates the dedicated native-produced JSON tree without author tag decoding. */
export function parseNativeAriaJSON(serialized: string): NativeAriaJSONNode[] {
  z.string().parse(serialized)
  const header = z.strictObject({
    role: z.string().regex(/^[a-z][a-z0-9-]*$/u),
    name: z.string().optional(),
    text: z.string().optional(),
    checked: z.union([z.boolean(), z.literal('mixed')]).optional(),
    disabled: z.boolean().optional(),
    expanded: z.boolean().optional(),
    active: z.boolean().optional(),
    invalid: z.union([z.boolean(), z.enum(['grammar', 'spelling'])]).optional(),
    level: z.number().int().min(1).optional(),
    pressed: z.union([z.boolean(), z.literal('mixed')]).optional(),
    selected: z.boolean().optional(),
    url: z.string().optional(),
    placeholder: z.string().optional(),
    ref: z
      .string()
      .regex(/^s[1-9][0-9]*e(?:0|[1-9][0-9]*)$/u)
      .optional(),
    cursor: z.literal('pointer').optional(),
    inputType: z.string().optional(),
    readOnly: z.boolean().optional(),
    editable: z.boolean().optional(),
    box: z
      .strictObject({
        x: z.number().finite(),
        y: z.number().finite(),
        width: z.number().finite().nonnegative(),
        height: z.number().finite().nonnegative(),
      })
      .optional(),
    children: z.array(z.unknown()).optional(),
  })
  const result: NativeAriaJSONNode[] = []
  const pending = [
    {
      values: z.array(z.unknown()).parse(JSON.parse(serialized) as UntrustedInput),
      output: result as (NativeAriaJSONNode | string)[],
      depth: 0,
    },
  ]
  while (pending.length) {
    const item = pending.pop()
    if (!item) break
    for (const input of item.values) {
      if (typeof input === 'string') {
        if (!item.depth) throw new Error('Native ARIA JSON root must contain nodes')
        item.output.push(input)
        continue
      }
      const { children, ...node } = header.parse(input)
      if (node.role === 'text' && node.ref !== undefined)
        throw new Error('Native ARIA JSON text cannot own Element references')
      const output: NativeAriaJSONNode = { role: node.role }
      for (const [key, value] of Object.entries(node))
        if (value !== undefined)
          Object.defineProperty(output, key, {
            value,
            enumerable: true,
            configurable: true,
            writable: true,
          })
      item.output.push(output)
      if (children !== undefined) {
        output.children = []
        pending.push({ values: children, output: output.children, depth: item.depth + 1 })
      }
    }
  }
  return result
}

/**
 * `document.body` as a selector: the root's body or frameset. A page with
 * nested framesets has several, and strict snapshot resolution needs one.
 */
export const NATIVE_DOCUMENT_BODY = ':root > body, :root > frameset'

const changedDocument =
  /snapshot (?:reference )?document changed|frame document changed|frame context unavailable/u

/**
 * A navigation can replace the document while an AI snapshot is captured. As
 * in Playwright, capture again in the new document within the same timeout.
 */
async function captureCurrentDocument<T>(
  locator: NativeLocator,
  timeout: number | undefined,
  signal: AbortSignal | undefined,
  capture: (timeout: number | undefined) => Promise<T>,
): Promise<T> {
  const total = timeout ?? locator.page().defaultTimeout()
  const deadline = total === 0 ? Infinity : Date.now() + total
  for (let attempt = 0; ; ++attempt) {
    try {
      return await capture(
        attempt === 0 || deadline === Infinity
          ? timeout
          : Math.max(1, Math.ceil(deadline - Date.now())),
      )
    } catch (error) {
      if (
        !(error instanceof Error) ||
        !changedDocument.test(error.message) ||
        Date.now() + 50 >= deadline ||
        signal?.aborted
      )
        throw error
      await new Promise((resolve) => setTimeout(resolve, 50))
    }
  }
}

/** Captures native semantic JSON directly, with AI reference commit owned by C++. */
export async function captureNativeAriaSnapshotJSON(
  locator: NativeLocator,
  options: NativeAriaSnapshotJSONOptions = {},
): Promise<NativeAriaJSONNode[]> {
  const config = z
    .strictObject({
      mode: z.enum(['default', 'ai']).optional(),
      timeout: z.number().int().min(0).max(120_000).optional(),
      signal: z.instanceof(AbortSignal).optional(),
      depth: z.number().int().min(0).optional(),
      boxes: z.boolean().optional(),
    })
    .parse(options)
  const abort = () => {
    const error = new Error('ariaSnapshotJSON: The operation was aborted', {
      cause: config.signal?.reason as UntrustedInput,
    })
    error.name = 'AbortError'
    return error
  }
  if (config.signal?.aborted) throw abort()
  const controller = new AbortController(),
    onAbort = () => controller.abort(abort())
  config.signal?.addEventListener('abort', onAbort, { once: true })
  try {
    const reply = z.object({ snapshotJSON: z.string() }).parse(
      config.mode === 'ai'
        ? await captureCurrentDocument(locator, config.timeout, controller.signal, (timeout) =>
            locator.runReferenceSnapshot(
              {
                format: 'json',
                ...(timeout === undefined ? {} : { timeout }),
                ...(config.boxes === undefined ? {} : { boxes: config.boxes }),
                ...(config.depth === undefined ? {} : { depth: config.depth }),
              },
              controller.signal,
            ),
          )
        : await locator.runStructuredSnapshot({ ...config, signal: controller.signal }),
    )
    return parseNativeAriaJSON(reply.snapshotJSON)
  } finally {
    config.signal?.removeEventListener('abort', onAbort)
  }
}

/** Returns the actual snapshot, preserving native missing-scope failures. */
export async function captureNativeAriaSnapshot(
  locator: NativeLocator,
  options: NativeAriaSnapshotOptions = {},
): Promise<string> {
  const config = z
    .strictObject({
      mode: z.enum(['expect', 'ai']).optional(),
      timeout: z.number().int().min(0).max(120_000).optional(),
      depth: z.number().int().min(0).optional(),
      boxes: z.boolean().optional(),
      interactive: z.boolean().optional(),
    })
    .parse(options)
  if (config.interactive && config.mode !== 'ai')
    throw new Error("ariaSnapshot: 'interactive' requires mode: 'ai'")
  if (config.mode === 'ai' && config.interactive) {
    // The browser formats the snapshot itself and sends only the text; one
    // that predates this sends the JSON tree, which is formatted here.
    const reply = z
      .union([z.object({ yamlJSON: z.string() }), z.object({ snapshotJSON: z.string() })])
      .parse(
        await captureCurrentDocument(locator, config.timeout, undefined, (timeout) =>
          locator.runReferenceSnapshot({
            format: 'json',
            interactive: true,
            ...(timeout === undefined ? {} : { timeout }),
            ...(config.boxes === undefined ? {} : { boxes: config.boxes }),
            ...(config.depth === undefined ? {} : { depth: config.depth }),
          }),
        ),
      )
    if ('yamlJSON' in reply) return z.string().parse(JSON.parse(reply.yamlJSON) as UntrustedInput)
    return formatNativeAriaInteractive(parseNativeAriaJSON(reply.snapshotJSON), {
      boxes: config.boxes ?? false,
    })
  }
  if (config.mode === 'ai') {
    const reply = z.object({ yamlJSON: z.string() }).parse(
      await captureCurrentDocument(locator, config.timeout, undefined, (timeout) =>
        locator.runReferenceSnapshot({
          ...(timeout === undefined ? {} : { timeout }),
          ...(config.boxes === undefined ? {} : { boxes: config.boxes }),
          ...(config.depth === undefined ? {} : { depth: config.depth }),
        }),
      ),
    )
    // Fixed native formatting has already succeeded before reference commit.
    // JSON string encoding preserves actual isolated units without scalar tags.
    return z.string().parse(JSON.parse(reply.yamlJSON) as UntrustedInput)
  }
  const observed = await observeNativeAria(locator, options)
  if (!observed.matches || observed.count !== 1)
    throw new Error('Native aria snapshot scope was not found')
  return observed.actual
}

/** Bounded line differences display actual captured semantics without changing them. */
export function formatNativeAriaDifference(expected: string, actual: string): string {
  const limit = 512
  const left = expected.split('\n').slice(0, limit),
    right = actual.split('\n').slice(0, limit)
  const width = right.length + 1
  const table = new Uint16Array((left.length + 1) * width)
  for (let i = left.length - 1; i >= 0; --i)
    for (let j = right.length - 1; j >= 0; --j)
      table[i * width + j] =
        left[i] === right[j]
          ? 1 + (table[(i + 1) * width + j + 1] ?? 0)
          : Math.max(table[(i + 1) * width + j] ?? 0, table[i * width + j + 1] ?? 0)
  const lines: string[] = []
  let i = 0,
    j = 0,
    removed = 0,
    added = 0
  while (i < left.length || j < right.length) {
    if (i < left.length && j < right.length && left[i] === right[j]) {
      lines.push(`  ${left[i] ?? ''}`)
      ++i
      ++j
    } else if (
      i < left.length &&
      (j === right.length || (table[(i + 1) * width + j] ?? 0) >= (table[i * width + j + 1] ?? 0))
    ) {
      lines.push(`- ${left[i] ?? ''}`)
      ++i
      ++removed
    } else {
      lines.push(`+ ${right[j] ?? ''}`)
      ++j
      ++added
    }
  }
  if (expected.split('\n').length > limit || actual.split('\n').length > limit)
    lines.push('  <remaining snapshot lines omitted>')
  return `- Expected  - ${removed}\n+ Received  + ${added}\n\n${lines.join('\n')}`
}

/** A native predicate result controls success; this helper only formats failures. */
export async function assertNativeAriaSnapshot(
  locator: NativeLocator,
  template: string,
  options: { timeout?: number | undefined } = {},
  isNot = false,
  pageTarget = false,
  customMessage?: string,
): Promise<void> {
  const config = z
    .strictObject({ timeout: z.number().int().min(0).max(120000).optional() })
    .parse(options)
  const timeout = config.timeout ?? 5000
  const expected = normalizeNativeAriaTemplate(z.string().parse(template))
  const target = pageTarget ? 'page' : 'locator'
  const locatorText = `locator('${locator.selector.replaceAll('\\', '\\\\').replaceAll("'", "\\'")}')`
  const heading = `expect(${target}).${isNot ? 'not.' : ''}toMatchAriaSnapshot(expected) failed`
  const log = `\n\nCall log:\n  - Expect "${isNot ? 'not ' : ''}toMatchAriaSnapshot"${pageTarget ? '' : ` ${locatorText}`} with timeout ${timeout}ms\n`
  let pattern: NativeAriaPattern
  try {
    pattern = parseNativeAriaTemplate(template)
  } catch (error) {
    throw new Error(
      `${heading}\n\nExpected: ${JSON.stringify(expected)}\nError: ${error instanceof Error ? error.message : 'Invalid native ARIA template'}${log}`,
      { cause: error },
    )
  }
  const observed = await observeNativeAria(locator, { timeout }, pattern, isNot)
  if (observed.matches !== isNot && observed.count === 1) return
  const multiline = expected.includes('\n') || observed.actual.includes('\n')
  const message = `${heading}\n\n${pageTarget ? '' : `Locator: ${locatorText}\n`}Timeout:${multiline ? ' ' : '  '}${timeout}ms\n${formatNativeAriaDifference(expected, observed.actual)}${log}  - unexpected value "${observed.actual}"\n`
  throw nativeMatcherFailure(
    new Error(
      nativeMatcherMessage(
        message +
          (observed.closedReason === undefined ? '' : `\nError: ${observed.closedReason}\n`),
        customMessage,
      ),
    ),
  )
}
