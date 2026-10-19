import { inspect } from 'node:util'

import * as z from 'zod'

import { assertNativeAriaSnapshot } from './native-aria.js'
import {
  hasNativeAssertionDeadline,
  nativeAssertionDeadlineKind,
} from './native-assertion-deadline.js'
import { NativeCallbackAssertions, type NativeRetryCallback } from './native-callback-assertions.js'
import {
  NativeAPIResponse,
  hasNativeAPIResponseReceipt,
  observeNativeAPIResponseAssertion,
} from './native-http.js'
import {
  nativeMatcherAssertionDeadline,
  nativeMatcherFailure,
  nativeMatcherMessage,
} from './native-matcher-failure.js'
import { NativeLocator, NativePage, nativeArtifactLifetime } from './native-page.js'
import {
  compareNativeScreenshot,
  parseNativeScreenshotAssertionOptions,
  type NativeScreenshotAssertionOptions,
  type NativeLocatorScreenshotAssertionOptions,
} from './native-screenshot-expect.js'
import { nativeSoftOperationOptions, runNativeSoftAssertion } from './native-soft-assertions.js'
import {
  isNativeURLPattern,
  runNativeURLPredicate,
  type NativeURLPattern,
  type NativeURLPredicate,
} from './native-url-predicate.js'
import { createNativeValueCodec } from './native-values.js'
import { type UntrustedInput } from './untrusted.js'

/** Optional author context; it never supplies native matcher observations. */
export type NativeExpectMessage = string | { readonly message: string }
function parseNativeExpectMessage(value: NativeExpectMessage | undefined): string | undefined {
  const parsed = z
    .union([z.string(), z.strictObject({ message: z.string() })])
    .optional()
    .parse(value)
  return typeof parsed === 'string' || parsed === undefined ? parsed : parsed.message
}

export interface NativeAssertionOptions {
  timeout?: number | undefined
  useInnerText?: boolean | undefined
  ignoreCase?: boolean | undefined
  visible?: boolean | undefined
  enabled?: boolean | undefined
  editable?: boolean | undefined
  checked?: boolean | undefined
  indeterminate?: boolean | undefined
  attached?: boolean | undefined
  ratio?: number | undefined
  pseudo?: string | undefined
}

/** Pure DOM observation and comparison; the C++ engine owns assertion retries. */
async function observeNativeAssertion(
  nodes: Element[],
  config: {
    matcher: string
    selector: string
    name?: string
    expected: UntrustedInput
    options: NativeAssertionOptions
    isNot: boolean
  },
  accessibility?: {
    visible?: boolean
    enabled?: boolean
    editable?: boolean
    role?: string | null
    name?: string
    description?: string
    errorMessage?: string
  },
): Promise<{
  matches: boolean
  actual: UntrustedInput
  count: number
  preview?: string
}> {
  const options = config.options
  let element = nodes[0]
  const previewNode = (element: Element | undefined) => {
    if (!element) return undefined
    const tag = element.localName
    const boundedAttributes: Attr[] = []
    for (let index = 0; index < Math.min(element.attributes.length, 32); ++index) {
      const attribute = element.attributes[index]
      if (attribute) boundedAttributes.push(attribute)
    }
    const attributes = boundedAttributes
      .filter(({ name }) => name !== 'style')
      .sort((left, right) => Number(right.value === '') - Number(left.value === ''))
      .map(({ name, value }) =>
        value === '' ? name : `${name}=${JSON.stringify(value.slice(0, 128))}`,
      )
      .join(' ')
    const start = `<${tag}${attributes ? ` ${attributes}` : ''}`
    if (
      [
        'area',
        'base',
        'br',
        'col',
        'embed',
        'hr',
        'img',
        'input',
        'link',
        'meta',
        'param',
        'source',
        'track',
        'wbr',
      ].includes(tag)
    )
      return `${start}/>`.slice(0, 1024)
    const walker = document.createTreeWalker(element, NodeFilter.SHOW_TEXT)
    let content = '',
      visited = 0
    for (
      let node = walker.nextNode();
      node && content.length < 256 && ++visited <= 100;
      node = walker.nextNode()
    )
      content += (node.nodeValue ?? '').slice(0, 256 - content.length)
    return `${start}>${content}</${tag}>`.slice(0, 1024)
  }
  const preview = previewNode(element)
  const multiple =
    config.matcher === 'toHaveCount' ||
    (Array.isArray(config.expected) &&
      ['toHaveText', 'toContainText', 'toHaveClass', 'toContainClass'].includes(config.matcher))
  if (!multiple && nodes.length > 1) {
    const quote = (value: string) => `'${value.replaceAll('\\', '\\\\').replaceAll("'", "\\'")}'`
    const locator = `locator(${quote(config.selector)})`
    const details = nodes.slice(0, 5).map((node, index) => {
      const walker = document.createTreeWalker(node, NodeFilter.SHOW_TEXT)
      let text = '',
        visited = 0
      for (
        let child = walker.nextNode();
        child && text.length < 256 && ++visited <= 100;
        child = walker.nextNode()
      )
        text += (child.nodeValue ?? '').slice(0, 256 - text.length)
      text = text
        .replace(/[\u200b\u00ad]/gu, '')
        .replace(/\s+/gu, ' ')
        .trim()
      const alias =
        node.id && node.id.length <= 128
          ? `locator(${quote('#' + CSS.escape(node.id))})`
          : text
            ? `getByText(${quote(text)})`
            : `locator(${quote(node.localName)})`
      return `    ${index + 1}) ${previewNode(node)} aka ${alias}`
    })
    throw new Error(
      `strict mode violation: ${locator} resolved to ${nodes.length} elements:\n${details.join('\n')}${nodes.length > 5 ? '\n    …' : ''}`,
    )
  }
  const observation = (matches: boolean, actual: UntrustedInput) => ({
    matches,
    actual,
    count: nodes.length,
    ...(preview === undefined ? {} : { preview }),
  })
  const normalize = (text: string) =>
    text
      .replace(/[\u200b\u00ad]/gu, '')
      .replace(/\s+/gu, ' ')
      .trim()
  const compareText = (
    actual: string,
    expected: UntrustedInput,
    contains = false,
    normalizeWhitespace = true,
  ): boolean => {
    if (expected instanceof RegExp) {
      const flags =
        options.ignoreCase === undefined
          ? expected.flags
          : expected.flags.replaceAll('i', '') + (options.ignoreCase ? 'i' : '')
      const regex = new RegExp(expected.source, flags)
      regex.lastIndex = 0
      return regex.test(actual)
    }
    if (typeof expected !== 'string') return false
    let left = normalizeWhitespace ? normalize(actual) : actual,
      right = normalizeWhitespace ? normalize(expected) : expected
    if (options.ignoreCase) {
      left = left.toLowerCase()
      right = right.toLowerCase()
    }
    return contains ? left.includes(right) : left === right
  }
  const equal = (
    left: UntrustedInput,
    right: UntrustedInput,
    seen = new WeakMap<object, object>(),
  ): boolean => {
    if (Object.is(left, right)) return true
    if (!left || !right || typeof left !== 'object' || typeof right !== 'object') return false
    if (Object.prototype.toString.call(left) !== Object.prototype.toString.call(right)) return false
    if (left instanceof Date && right instanceof Date)
      return Object.is(left.getTime(), right.getTime())
    if (left instanceof RegExp && right instanceof RegExp)
      return left.source === right.source && left.flags === right.flags
    if (seen.has(left)) return seen.get(left) === right
    seen.set(left, right)
    const a = Object.keys(left),
      b = Object.keys(right)
    if (a.length !== b.length || a.some((key) => !Object.hasOwn(right, key))) return false
    return a.every((key) =>
      equal(
        Reflect.get(left, key) as UntrustedInput,
        Reflect.get(right, key) as UntrustedInput,
        seen,
      ),
    )
  }
  const text = (node: Element) => {
    if (options.useInnerText && node instanceof HTMLElement) return node.innerText
    const parts: string[] = [],
      stack: Node[] = [node]
    let visited = 0
    while (stack.length) {
      if (++visited > 100_000)
        throw new Error('Native assertion text traversal exceeds 100000 nodes')
      const current = stack.pop()
      if (!current) break
      if (current.nodeType === Node.TEXT_NODE) parts.push(current.nodeValue ?? '')
      else if (current instanceof Element && !current.matches('script,style,noscript')) {
        if (current instanceof HTMLInputElement && ['button', 'submit'].includes(current.type))
          parts.push(current.value)
        else {
          const children = [
            ...Array.from(current.childNodes),
            ...Array.from(current.shadowRoot?.childNodes ?? []),
          ]
          for (let i = children.length - 1; i >= 0; --i) {
            const child = children[i]
            if (child) stack.push(child)
          }
        }
      }
    }
    return parts.join('')
  }
  const visible = (node: Element | undefined) => {
    if (!node) return false
    if (accessibility?.visible !== undefined) return accessibility.visible
    const style = getComputedStyle(node),
      rect = node.getBoundingClientRect()
    return (
      style.visibility !== 'hidden' &&
      style.visibility !== 'collapse' &&
      style.display !== 'none' &&
      rect.width > 0 &&
      rect.height > 0
    )
  }
  const disabled = (node: Element | undefined) => {
    if (!node) return false
    if (accessibility?.enabled !== undefined) return !accessibility.enabled
    if (node.matches(':disabled')) return true
    const role = node.getAttribute('role')?.trim().split(/\s+/u)[0] ?? ''
    if (
      !node.matches('button,input,select,textarea,option,optgroup,fieldset,a[href]') &&
      ![
        'application',
        'button',
        'checkbox',
        'columnheader',
        'combobox',
        'grid',
        'gridcell',
        'group',
        'link',
        'listbox',
        'menuitem',
        'menuitemcheckbox',
        'menuitemradio',
        'option',
        'radio',
        'radiogroup',
        'row',
        'rowheader',
        'scrollbar',
        'searchbox',
        'separator',
        'slider',
        'spinbutton',
        'tab',
        'tablist',
        'textbox',
        'toolbar',
        'tree',
        'treeitem',
      ].includes(role)
    )
      return false
    for (
      let parent: Element | null = node;
      parent;
      parent =
        parent.parentElement ??
        (parent.getRootNode() instanceof ShadowRoot
          ? (parent.getRootNode() as ShadowRoot).host
          : null)
    ) {
      const state = parent.getAttribute('aria-disabled')?.toLowerCase()
      if (state === 'true' || state === 'false') return state === 'true'
    }
    return false
  }
  let actual: UntrustedInput
  let matches: boolean
  const missingRequiresRetry =
    !element &&
    config.matcher !== 'toHaveCount' &&
    !['toBeVisible', 'toBeHidden', 'toBeAttached'].includes(config.matcher) &&
    !(
      ['toHaveText', 'toContainText', 'toHaveClass', 'toContainClass'].includes(config.matcher) &&
      Array.isArray(config.expected)
    )
  if (missingRequiresRetry) return observation(config.isNot, undefined)
  if (config.matcher === 'toHaveCount') {
    actual = nodes.length
    matches = actual === config.expected
  } else if (config.matcher === 'toHaveText' || config.matcher === 'toContainText') {
    const contains = config.matcher === 'toContainText'
    if (Array.isArray(config.expected)) {
      const values = nodes.map(text)
      actual = values
      if (contains) {
        let index = 0
        matches = config.expected.every((expected) => {
          while (
            index < values.length &&
            !compareText(values[index] ?? '', expected as UntrustedInput, true)
          )
            ++index
          return index++ < values.length
        })
      } else
        matches =
          values.length === config.expected.length &&
          config.expected.every((expected, index) =>
            compareText(values[index] ?? '', expected as UntrustedInput),
          )
    } else {
      actual = element ? text(element) : undefined
      matches =
        nodes.length === 1 &&
        compareText(typeof actual === 'string' ? actual : '', config.expected, contains)
    }
  } else if (
    (config.matcher === 'toHaveClass' || config.matcher === 'toContainClass') &&
    Array.isArray(config.expected)
  ) {
    actual = nodes.map((node) => node.getAttribute('class') ?? '')
    matches =
      nodes.length === config.expected.length &&
      config.expected.every((expected, index) => {
        const value = (actual as string[])[index] ?? ''
        return config.matcher === 'toHaveClass'
          ? compareText(value, expected as UntrustedInput, false, false)
          : typeof expected === 'string' &&
              expected
                .trim()
                .split(/\s+/u)
                .filter(Boolean)
                .every((token) => nodes[index]?.classList.contains(token))
      })
  } else {
    if (nodes.length > 1) throw new Error(`Strict locator matched ${nodes.length} elements`)
    switch (config.matcher) {
      case 'toHaveAccessibleName':
      case 'toHaveAccessibleDescription':
      case 'toHaveAccessibleErrorMessage': {
        if (element && !accessibility)
          throw new Error('Native accessibility metadata is unavailable')
        const key =
          config.matcher === 'toHaveAccessibleName'
            ? 'name'
            : config.matcher === 'toHaveAccessibleDescription'
              ? 'description'
              : 'errorMessage'
        actual = accessibility?.[key] ?? ''
        matches = !!element && compareText(actual, config.expected)
        break
      }
      case 'toHaveRole':
        if (element && !accessibility)
          throw new Error('Native accessibility metadata is unavailable')
        actual = accessibility?.role ?? ''
        matches = !!element && actual === config.expected
        break
      case 'toBeVisible':
        actual = visible(element)
        matches = actual === (options.visible ?? true)
        break
      case 'toBeHidden':
        actual = !visible(element)
        matches = actual
        break
      case 'toBeAttached':
        actual = element?.isConnected ?? false
        matches = actual === (options.attached ?? true)
        break
      case 'toBeEnabled':
        actual = !!element && !disabled(element)
        matches = actual === (options.enabled ?? true)
        break
      case 'toBeDisabled':
        actual = !!element && disabled(element)
        matches = actual
        break
      case 'toBeEditable':
        if (element && accessibility?.enabled !== undefined) {
          if (accessibility.editable === undefined)
            throw new Error(
              'Element is not an <input>, <textarea>, <select> or [contenteditable] and does not have a role allowing [aria-readonly]',
            )
          actual = accessibility.editable
          matches = actual === (options.editable ?? true)
          break
        }
        if (
          element &&
          !(
            element instanceof HTMLInputElement ||
            element instanceof HTMLTextAreaElement ||
            element instanceof HTMLSelectElement
          ) &&
          !(element instanceof HTMLElement && element.isContentEditable) &&
          !element.matches(
            '[role=checkbox],[role=combobox],[role=gridcell],[role=listbox],[role=slider],[role=spinbutton],[role=textbox],[role=searchbox],[role=treegrid]',
          )
        )
          throw new Error(
            'Element is not an <input>, <textarea>, <select> or [contenteditable] and does not have a role allowing [aria-readonly]',
          )
        actual =
          !!element &&
          !disabled(element) &&
          (element instanceof HTMLInputElement || element instanceof HTMLTextAreaElement
            ? !element.readOnly
            : element instanceof HTMLSelectElement ||
              (element instanceof HTMLElement && element.isContentEditable) ||
              element.getAttribute('aria-readonly') !== 'true')
        matches = actual === (options.editable ?? true)
        break
      case 'toBeChecked':
        if (element instanceof HTMLLabelElement) element = element.control ?? element
        if (
          element &&
          !(element instanceof HTMLInputElement && ['checkbox', 'radio'].includes(element.type)) &&
          !element.matches(
            '[role=checkbox],[role=radio],[role=menuitemcheckbox],[role=menuitemradio],[role=option],[role=switch],[role=treeitem]',
          )
        )
          throw new Error('Element is not a checkbox or radio')
        actual =
          element instanceof HTMLInputElement
            ? options.indeterminate
              ? element.indeterminate
              : element.checked
            : element?.getAttribute('aria-checked') === (options.indeterminate ? 'mixed' : 'true')
        matches = !!element && actual === (options.indeterminate ? true : (options.checked ?? true))
        break
      case 'toBeFocused':
        actual =
          !!element && (element.getRootNode() as Document | ShadowRoot).activeElement === element
        matches = actual
        break
      case 'toBeEmpty':
        actual = !element
          ? undefined
          : element instanceof HTMLInputElement || element instanceof HTMLTextAreaElement
            ? element.value
            : text(element)
        matches = !!element && actual === ''
        break
      case 'toBeInViewport': {
        const observedElement = element
        if (!observedElement) return observation(config.isNot, undefined)
        actual = await new Promise<number>((resolve) => {
          const observer = new IntersectionObserver((entries) => {
            observer.disconnect()
            resolve(entries[0]?.intersectionRatio ?? 0)
          })
          observer.observe(observedElement)
        })
        matches = actual > 0 && actual >= (options.ratio ?? 0)
        break
      }
      case 'toHaveTitle':
        actual = document.title
        matches = compareText(actual, config.expected)
        break
      case 'toHaveURL':
        actual = window.location.href
        matches = compareText(actual, config.expected, false, false)
        break
      case 'toHaveValue':
        if (element instanceof HTMLLabelElement) element = element.control ?? element
        if (
          element &&
          !(
            element instanceof HTMLInputElement ||
            element instanceof HTMLTextAreaElement ||
            element instanceof HTMLSelectElement
          )
        )
          throw new Error('Element is not an input, textarea or select')
        actual = (element as HTMLInputElement | undefined)?.value
        matches = !!element && compareText(actual ?? '', config.expected, false, false)
        break
      case 'toHaveValues': {
        if (element instanceof HTMLLabelElement) element = element.control ?? element
        if (!(element instanceof HTMLSelectElement) || !element.multiple)
          throw new Error('Not a select element with a multiple attribute')
        const values = Array.from(element.selectedOptions, (option) => option.value)
        actual = values
        matches =
          Array.isArray(config.expected) &&
          values.length === config.expected.length &&
          config.expected.every((expected, index) =>
            compareText(
              (actual as string[])[index] ?? '',
              expected as UntrustedInput,
              false,
              false,
            ),
          )
        break
      }
      case 'toHaveAttribute':
        actual = element?.getAttribute(config.name ?? '')
        matches =
          !!element &&
          (config.expected === undefined
            ? actual !== null
            : typeof actual === 'string' && compareText(actual, config.expected, false, false))
        break
      case 'toHaveClass':
        actual = element?.getAttribute('class') ?? ''
        matches = !!element && compareText(actual, config.expected, false, false)
        break
      case 'toContainClass':
        actual = element?.getAttribute('class') ?? ''
        matches =
          !!element &&
          typeof config.expected === 'string' &&
          config.expected
            .trim()
            .split(/\s+/u)
            .filter(Boolean)
            .every((token) => element?.classList.contains(token))
        break
      case 'toHaveId':
        actual = element?.id
        matches = !!element && compareText(actual ?? '', config.expected, false, false)
        break
      case 'toHaveCSS':
        actual = element
          ? getComputedStyle(element, options.pseudo).getPropertyValue(config.name ?? '')
          : undefined
        matches = !!element && compareText(actual ?? '', config.expected, false, false)
        break
      case 'toHaveJSProperty':
        actual = element
        for (const part of (config.name ?? '').split('.'))
          actual =
            actual !== null &&
            actual !== undefined &&
            (typeof actual === 'object' || typeof actual === 'function')
              ? (Reflect.get(actual, part) as UntrustedInput)
              : undefined
        matches = !!element && equal(actual, config.expected)
        break
      default:
        throw new Error(`Unsupported native assertion: ${config.matcher}`)
    }
  }
  return observation(matches, actual)
}

const optionsSchema = z.strictObject({
  timeout: z.number().int().min(0).max(120_000).optional(),
  useInnerText: z.boolean().optional(),
  ignoreCase: z.boolean().optional(),
  visible: z.boolean().optional(),
  enabled: z.boolean().optional(),
  editable: z.boolean().optional(),
  checked: z.boolean().optional(),
  indeterminate: z.boolean().optional(),
  attached: z.boolean().optional(),
  ratio: z.number().min(0).max(1).optional(),
  pseudo: z.string().min(1).max(256).optional(),
})
const observationSchema = z.object({
  closedReason: z.string().max(8192).optional(),
  result: z
    .object({
      value: z
        .object({
          matches: z.boolean(),
          actual: z.unknown(),
          count: z.number().int().min(0).optional(),
          preview: z.string().max(1024).optional(),
          truncated: z.boolean().optional(),
        })
        .optional(),
    })
    .optional(),
  exceptionDetails: z
    .object({
      text: z.string(),
      exception: z.object({ description: z.string().optional() }).optional(),
    })
    .optional(),
})

export class NativeAssertionError extends Error {
  override readonly name = 'NativeAssertionError'
}

function formatAssertionValue(value: UntrustedInput): string {
  return typeof value === 'string'
    ? JSON.stringify(value)
    : inspect(value, {
        depth: 3,
        maxArrayLength: 20,
        maxStringLength: 1024,
        customInspect: false,
        getters: false,
        breakLength: Infinity,
      })
}

/** A bounded edit summary of the actual array observation. */
function arrayAssertionDiff(expected: UntrustedInput[], actual: UntrustedInput[]): string {
  const limit = 32
  const left = expected.slice(0, limit).map((value, index) => {
    const candidate = actual[index]
    if (value instanceof RegExp && typeof candidate === 'string') {
      const pattern = new RegExp(value.source, value.flags)
      if (pattern.test(candidate)) return formatAssertionValue(candidate)
    }
    return formatAssertionValue(value)
  })
  const right = actual.slice(0, limit).map(formatAssertionValue)
  const table = Array.from({ length: left.length + 1 }, () =>
    Array<number>(right.length + 1).fill(0),
  )
  for (let i = left.length - 1; i >= 0; --i)
    for (let j = right.length - 1; j >= 0; --j) {
      const row = table[i]
      if (row)
        row[j] =
          left[i] === right[j]
            ? 1 + (table[i + 1]?.[j + 1] ?? 0)
            : Math.max(table[i + 1]?.[j] ?? 0, row[j + 1] ?? 0)
    }
  const lines: string[] = []
  let i = 0,
    j = 0,
    removed = 0,
    added = 0
  while (i < left.length || j < right.length) {
    if (i < left.length && j < right.length && left[i] === right[j]) {
      lines.push(`    ${left[i]},`)
      ++i
      ++j
    } else if (
      i < left.length &&
      (j >= right.length || (table[i + 1]?.[j] ?? 0) >= (table[i]?.[j + 1] ?? 0))
    ) {
      lines.push(`-   ${left[i]},`)
      ++i
      ++removed
    } else {
      lines.push(`+   ${right[j]},`)
      ++j
      ++added
    }
  }
  if (expected.length > limit || actual.length > limit) lines.push('    … remaining items omitted')
  return `- Expected  - ${removed}\n+ Received  + ${added}\n\n  Array [\n${lines.join('\n')}\n  ]`
}

/** Formats only the final observation; repeated observations remain owned by C++. */
function assertionFailure(
  matcher: string,
  selector: string,
  expected: UntrustedInput,
  actual: UntrustedInput,
  options: NativeAssertionOptions,
  isNot: boolean,
  observation: { count?: number | undefined; preview?: string | undefined },
): string {
  const locator = `locator('${selector.replaceAll('\\', '\\\\').replaceAll("'", "\\'")}')`
  const pageMatcher = ['toHaveTitle', 'toHaveURL'].includes(matcher)
  const timeout = options.timeout ?? 5000
  const booleanStates: Record<string, [string, string]> = {
    toBeChecked: ['checked', 'unchecked'],
    toBeEditable: ['editable', 'read-only'],
    toBeEnabled: ['enabled', 'disabled'],
    toBeDisabled: ['disabled', 'enabled'],
    toBeEmpty: ['empty', 'not empty'],
    toBeFocused: ['focused', 'not focused'],
    toBeVisible: ['visible', 'hidden'],
    toBeHidden: ['hidden', 'visible'],
    toBeAttached: ['attached', 'detached'],
    toBeInViewport: ['in viewport', 'outside viewport'],
  }
  const state = booleanStates[matcher]
  let invocation = state ? '' : 'expected'
  let wanted = formatAssertionValue(expected)
  let received = formatAssertionValue(actual)
  if (
    matcher === 'toHaveAttribute' &&
    actual === null &&
    typeof expected === 'string' &&
    expected === ''
  )
    received = 'serializes to the same string'
  if (matcher === 'toHaveAttribute' && actual === null && expected instanceof RegExp)
    received = JSON.stringify('')
  if (state) {
    const optionName = {
      toBeChecked: options.indeterminate === undefined ? 'checked' : 'indeterminate',
      toBeEditable: 'editable',
      toBeEnabled: 'enabled',
      toBeVisible: 'visible',
      toBeAttached: 'attached',
    }[matcher]
    const flag = optionName ? (Reflect.get(options, optionName) as UntrustedInput) : undefined
    if (flag !== undefined) invocation = `{ ${optionName}: ${formatAssertionValue(flag)} }`
    wanted = options.indeterminate ? 'indeterminate' : state[flag === false ? 1 : 0]
    received =
      matcher === 'toBeInViewport'
        ? `viewport ratio ${formatAssertionValue(actual)}`
        : matcher === 'toBeEmpty'
          ? state[actual === '' ? 0 : 1]
          : options.indeterminate && actual === true
            ? 'indeterminate'
            : state[actual ? 0 : 1]
  }
  if (isNot) wanted = `not ${wanted}`
  const pattern = expected instanceof RegExp
  const substring = matcher === 'toContainText' && typeof expected === 'string'
  const array = Array.isArray(expected) && Array.isArray(actual)
  const labels = pattern
    ? ['Expected pattern:', 'Received string:', 'Timeout:']
    : substring
      ? ['Expected substring:', 'Received string:', 'Timeout:']
      : ['Expected:', 'Received:', 'Timeout:']
  const missing = observation.count === 0 && matcher !== 'toHaveCount' && !Array.isArray(expected)
  const rows: [string, string][] = [
    ...(!pageMatcher ? [['Locator:', locator] as [string, string]] : []),
    ...(!array ? [[labels[0] ?? 'Expected:', wanted] as [string, string]] : []),
    ...(!missing && !array ? [[labels[1] ?? 'Received:', received] as [string, string]] : []),
    [labels[2] ?? 'Timeout:', `${timeout}ms`],
  ]
  const width = missing || array ? 0 : Math.max(...rows.map(([label]) => label.length))
  const emptyNegatedArray = array && isNot && expected.length === 0 && actual.length === 0
  let message = `expect(${pageMatcher ? 'page' : 'locator'}).${isNot ? 'not.' : ''}${matcher}(${invocation}) failed\n\n`
  message += rows
    .map(
      ([label, value]) =>
        `${label.padEnd(emptyNegatedArray && label === 'Timeout:' ? 'Expected:'.length : (pattern || substring) && (label === 'Locator:' || label === 'Timeout:' || (matcher === 'toHaveAttribute' && actual === '' && isNot)) ? 0 : width)} ${value}`,
    )
    .join('\n')
  if (array)
    message += `\n${arrayAssertionDiff(expected as UntrustedInput[], actual as UntrustedInput[])}`
  if (missing) message += '\nError: element(s) not found'
  message += `\n\nCall log:\n  - Expect "${isNot ? 'not ' : ''}${matcher}"${pageMatcher ? '' : ` ${locator}`} with timeout ${timeout}ms\n`
  if (!pageMatcher) message += `  - waiting for ${locator}\n`
  if (observation.preview && !pageMatcher)
    message += `  - locator resolved to ${observation.preview}\n`
  if (Array.isArray(expected) || matcher === 'toHaveCount') {
    if (observation.count !== undefined)
      message += `  - locator resolved to ${observation.count} element${observation.count === 1 ? '' : 's'}\n`
  }
  if (
    matcher === 'toHaveJSProperty' &&
    expected &&
    actual &&
    typeof expected === 'object' &&
    typeof actual === 'object' &&
    !Array.isArray(expected)
  ) {
    for (const key of Object.keys(expected).slice(0, 32)) {
      const expectedProperty = Object.getOwnPropertyDescriptor(expected, key)
      const actualProperty = Object.getOwnPropertyDescriptor(actual, key)
      const left: UntrustedInput =
        expectedProperty && 'value' in expectedProperty
          ? (expectedProperty.value as UntrustedInput)
          : '<accessor>'
      const right: UntrustedInput =
        actualProperty && 'value' in actualProperty
          ? (actualProperty.value as UntrustedInput)
          : '<accessor>'
      if (formatAssertionValue(left) !== formatAssertionValue(right))
        message += `  - changed property ${JSON.stringify(key)}:\n-   ${JSON.stringify(key)}: ${formatAssertionValue(left)}\n+   ${JSON.stringify(key)}: ${formatAssertionValue(right)}\n`
    }
  }
  if (!missing)
    message += `  - unexpected value ${JSON.stringify(state ? received : typeof actual === 'string' ? actual : formatAssertionValue(actual))}\n`
  return message
}

/** Cause diagnostics retain actual engine errors and known matcher inputs only. */
function assertionCauseFailure(
  matcher: string,
  selector: string,
  expected: UntrustedInput,
  options: NativeAssertionOptions,
  isNot: boolean,
  reason: string,
): string {
  const separator = reason.indexOf('\n    at ')
  const cause = (separator < 0 ? reason : reason.slice(0, separator)).replace(
    /^[A-Za-z]*Error:\s*/u,
    '',
  )
  const original = assertionFailure(matcher, selector, expected, undefined, options, isNot, {
    count: 0,
  })
  const heading = original.split('\n\nCall log:')[0] ?? original
  const lines = heading
    .split('\n')
    .filter((line) => !line.startsWith('Timeout:') && !line.startsWith('Error:'))
  if (cause.includes('while parsing css selector')) {
    const index = lines.findIndex((line) => line.startsWith('Locator:'))
    if (index >= 0) lines[index] = `Locator: ${selector}`
  }
  const log = original.slice(original.indexOf('\n\nCall log:'))
  return `${lines.join('\n')}\nError: ${cause}${log}`
}

/** Assertions compare real DOM observations with native deadlines and cancellation. */
export class NativeLocatorAssertions {
  constructor(
    private readonly locator: NativeLocator,
    private readonly isNot = false,
    private readonly customMessage?: string,
  ) {}
  get not(): NativeLocatorAssertions {
    return new NativeLocatorAssertions(this.locator, !this.isNot, this.customMessage)
  }
  async toHaveScreenshot(
    expected: string | Buffer,
    options: NativeLocatorScreenshotAssertionOptions = {},
  ): Promise<void> {
    const { page, target } = this.locator.nativeScreenshotDescriptor()
    const comparison = await compareNativeScreenshot(
      (params, signal) => page.runAutomation('artifact', params, signal),
      expected,
      options,
      page,
      this.isNot,
      target,
      page.source,
      nativeArtifactLifetime(page),
    ).catch((error: UntrustedInput) => {
      if (error instanceof Error && hasNativeAssertionDeadline(error, 'toHaveScreenshot'))
        throw nativeMatcherAssertionDeadline(
          new NativeAssertionError(nativeMatcherMessage(error.message, this.customMessage)),
          error,
        )
      throw error
    })
    if (!comparison.result.passed) {
      const error = new NativeAssertionError(
        nativeMatcherMessage(comparison.message, this.customMessage),
      )
      throw comparison.observedMismatch ? nativeMatcherFailure(error) : error
    }
  }
  async check(
    matcher: string,
    expected: UntrustedInput = undefined,
    options: NativeAssertionOptions = {},
    name?: string,
  ): Promise<void> {
    const textExpected = z.union([z.string(), z.instanceof(RegExp)])
    if (matcher === 'toHaveURL' && typeof expected !== 'string' && !(expected instanceof RegExp))
      throw new NativeAssertionError(
        `expect(page).${this.isNot ? 'not.' : ''}toHaveURL(expected) failed\n\nError: expected value must be a string or regular expression\nExpected has type:  ${expected === null ? 'null' : Array.isArray(expected) ? 'array' : typeof expected}\nExpected has value: ${formatAssertionValue(expected)}\n`,
      )
    if (matcher === 'toHaveRole' && typeof expected !== 'string')
      throw new Error('"role" argument in toHaveRole must be a string')
    if (matcher === 'toHaveCount') z.number().int().min(0).parse(expected)
    else if (matcher === 'toHaveValues') z.array(textExpected).parse(expected)
    else if (['toHaveText', 'toContainText', 'toHaveClass'].includes(matcher))
      z.union([textExpected, z.array(textExpected)]).parse(expected)
    else if (matcher === 'toContainClass')
      z.union([z.string(), z.array(z.string())]).parse(expected)
    else if (matcher === 'toHaveAttribute') textExpected.optional().parse(expected)
    else if (
      [
        'toHaveValue',
        'toHaveId',
        'toHaveCSS',
        'toHaveTitle',
        'toHaveURL',
        'toHaveAccessibleName',
        'toHaveAccessibleDescription',
        'toHaveAccessibleErrorMessage',
      ].includes(matcher)
    )
      textExpected.parse(expected)
    const config = optionsSchema.parse(options)
    const allowed = new Set([
      'timeout',
      ...(matcher === 'toHaveText' || matcher === 'toContainText'
        ? ['useInnerText', 'ignoreCase']
        : matcher === 'toHaveCSS'
          ? ['pseudo']
          : [
                'toHaveURL',
                'toHaveTitle',
                'toHaveAttribute',
                'toHaveAccessibleName',
                'toHaveAccessibleDescription',
                'toHaveAccessibleErrorMessage',
              ].includes(matcher)
            ? ['ignoreCase']
            : matcher === 'toBeVisible'
              ? ['visible']
              : matcher === 'toBeEnabled'
                ? ['enabled']
                : matcher === 'toBeEditable'
                  ? ['editable']
                  : matcher === 'toBeChecked'
                    ? ['checked', 'indeterminate']
                    : matcher === 'toBeAttached'
                      ? ['attached']
                      : matcher === 'toBeInViewport'
                        ? ['ratio']
                        : []),
    ])
    for (const key of Object.keys(config))
      if (!allowed.has(key)) throw new Error(`Unsupported ${matcher} option: ${key}`)
    if (config.checked !== undefined && config.indeterminate !== undefined)
      throw new Error("Can't assert indeterminate and checked at the same time")
    const codec = createNativeValueCodec()
    const encoded = codec.encode(
      {
        matcher,
        selector: this.locator.selector,
        name,
        expected,
        options: config,
        isNot: this.isNot,
      },
      'argument',
    )
    const declaration = `async function(nodes,accessibility){const codec=(${createNativeValueCodec.toString()})();const config=codec.decode(${JSON.stringify(encoded)});const observed=await (${observeNativeAssertion.toString()})(nodes,config,accessibility);return {...observed,actual:codec.encode(observed.actual)};}`
    let raw: UntrustedInput
    try {
      raw = await this.locator.runExpectation({
        function: declaration,
        nativeAssertionKind: nativeAssertionDeadlineKind(matcher, expected),
        dom: matcher !== 'toHaveJSProperty',
        strictDiagnostics:
          matcher !== 'toHaveJSProperty' &&
          !(
            matcher === 'toHaveCount' ||
            (Array.isArray(expected) &&
              ['toHaveText', 'toContainText', 'toHaveClass', 'toContainClass'].includes(matcher))
          ),
        isNot: this.isNot,
        accessibility: matcher.startsWith('toHaveAccessible') || matcher === 'toHaveRole',
        visibility: matcher === 'toBeVisible' || matcher === 'toBeHidden',
        elementState: ['toBeEnabled', 'toBeDisabled', 'toBeEditable'].includes(matcher),
        timeoutMs: config.timeout ?? 5000,
      })
    } catch (error) {
      if (error instanceof Error && hasNativeAssertionDeadline(error, matcher)) {
        const message = nativeMatcherMessage(
          `expect(${JSON.stringify(this.locator.selector)}).${this.isNot ? 'not.' : ''}${matcher}(${expected === undefined ? '' : formatAssertionValue(expected)})\n\n` +
            `Locator: ${this.locator.selector}\nTimeout: ${config.timeout ?? 5000}ms\n` +
            'Error: Native assertion original deadline expired without an admitted value',
          this.customMessage,
        )
        throw nativeMatcherAssertionDeadline(new NativeAssertionError(message), error)
      }
      throw new NativeAssertionError(
        assertionCauseFailure(
          matcher,
          this.locator.selector,
          expected,
          config,
          this.isNot,
          error instanceof Error ? error.message : 'Native assertion command failed',
        ),
      )
    }
    const exception = observationSchema.pick({ exceptionDetails: true }).parse(raw).exceptionDetails
    if (exception)
      throw new NativeAssertionError(
        assertionCauseFailure(
          matcher,
          this.locator.selector,
          expected,
          config,
          this.isNot,
          exception.exception?.description ?? exception.text,
        ),
      )
    const result = observationSchema.parse(raw)
    const observed = result.result?.value
    if (!observed) throw new NativeAssertionError(`${matcher}: native observation unavailable`)
    if (observed.matches === this.isNot) {
      const actual = codec.decode(observed.actual as UntrustedInput)
      throw nativeMatcherFailure(
        new NativeAssertionError(
          nativeMatcherMessage(
            assertionFailure(
              matcher,
              this.locator.selector,
              expected,
              actual,
              config,
              this.isNot,
              observed,
            ) + (result.closedReason ? `\nError: ${result.closedReason}\n` : ''),
            this.customMessage,
          ),
        ),
      )
    }
  }
  toMatchAriaSnapshot(expected: string, options?: NativeAssertionOptions): Promise<void> {
    return assertNativeAriaSnapshot(
      this.locator,
      expected,
      options,
      this.isNot,
      false,
      this.customMessage,
    )
  }
  toHaveText(
    expected: string | RegExp | (string | RegExp)[],
    options?: NativeAssertionOptions,
  ): Promise<void> {
    return this.check('toHaveText', expected, options)
  }
  toContainText(
    expected: string | RegExp | (string | RegExp)[],
    options?: NativeAssertionOptions,
  ): Promise<void> {
    return this.check('toContainText', expected, options)
  }
  toHaveValue(expected: string | RegExp, options?: NativeAssertionOptions): Promise<void> {
    return this.check('toHaveValue', expected, options)
  }
  toHaveAccessibleName(expected: string | RegExp, options?: NativeAssertionOptions): Promise<void> {
    return this.check('toHaveAccessibleName', expected, options)
  }
  toHaveRole(role: string, options?: NativeAssertionOptions): Promise<void> {
    return this.check('toHaveRole', role, options)
  }
  toHaveAccessibleDescription(
    expected: string | RegExp,
    options?: NativeAssertionOptions,
  ): Promise<void> {
    return this.check('toHaveAccessibleDescription', expected, options)
  }
  toHaveAccessibleErrorMessage(
    expected: string | RegExp,
    options?: NativeAssertionOptions,
  ): Promise<void> {
    return this.check('toHaveAccessibleErrorMessage', expected, options)
  }
  toHaveValues(expected: (string | RegExp)[], options?: NativeAssertionOptions): Promise<void> {
    return this.check('toHaveValues', expected, options)
  }
  toHaveCount(expected: number, options?: NativeAssertionOptions): Promise<void> {
    return this.check('toHaveCount', expected, options)
  }
  toHaveJSProperty(
    name: string,
    expected: UntrustedInput,
    options?: NativeAssertionOptions,
  ): Promise<void> {
    return this.check('toHaveJSProperty', expected, options, name)
  }
  toHaveAttribute(
    name: string,
    expected?: string | RegExp | NativeAssertionOptions,
    options?: NativeAssertionOptions,
  ): Promise<void> {
    if (typeof expected === 'object' && !(expected instanceof RegExp))
      return this.check('toHaveAttribute', undefined, expected, name)
    return this.check('toHaveAttribute', expected, options, name)
  }
  toHaveClass(
    expected: string | RegExp | (string | RegExp)[],
    options?: NativeAssertionOptions,
  ): Promise<void> {
    return this.check('toHaveClass', expected, options)
  }
  toContainClass(expected: string | string[], options?: NativeAssertionOptions): Promise<void> {
    return this.check('toContainClass', expected, options)
  }
  toHaveId(expected: string | RegExp, options?: NativeAssertionOptions): Promise<void> {
    return this.check('toHaveId', expected, options)
  }
  toHaveCSS(
    name: string,
    expected: string | RegExp,
    options?: NativeAssertionOptions,
  ): Promise<void> {
    return this.check('toHaveCSS', expected, options, name)
  }
  toBeVisible(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeVisible', undefined, options)
  }
  toBeHidden(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeHidden', undefined, options)
  }
  toBeAttached(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeAttached', undefined, options)
  }
  toBeEnabled(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeEnabled', undefined, options)
  }
  toBeDisabled(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeDisabled', undefined, options)
  }
  toBeEditable(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeEditable', undefined, options)
  }
  toBeChecked(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeChecked', undefined, options)
  }
  toBeFocused(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeFocused', undefined, options)
  }
  toBeEmpty(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeEmpty', undefined, options)
  }
  toBeInViewport(options?: NativeAssertionOptions): Promise<void> {
    return this.check('toBeInViewport', undefined, options)
  }
}

/** Page assertions use the same C++ polling and cancellation as locator assertions. */
export class NativePageAssertions {
  constructor(
    private readonly page: NativePage,
    private readonly isNot = false,
    private readonly customMessage?: string,
  ) {}
  get not(): NativePageAssertions {
    return new NativePageAssertions(this.page, !this.isNot, this.customMessage)
  }
  async toHaveScreenshot(
    expected: string | Buffer,
    options: NativeScreenshotAssertionOptions = {},
  ): Promise<void> {
    const comparison = await compareNativeScreenshot(
      (params, signal) => this.page.runAutomation('artifact', params, signal),
      expected,
      options,
      this.page,
      this.isNot,
      undefined,
      this.page.source,
      nativeArtifactLifetime(this.page),
    ).catch((error: UntrustedInput) => {
      if (error instanceof Error && hasNativeAssertionDeadline(error, 'toHaveScreenshot'))
        throw nativeMatcherAssertionDeadline(
          new NativeAssertionError(nativeMatcherMessage(error.message, this.customMessage)),
          error,
        )
      throw error
    })
    if (!comparison.result.passed) {
      const error = new NativeAssertionError(
        nativeMatcherMessage(comparison.message, this.customMessage),
      )
      throw comparison.observedMismatch ? nativeMatcherFailure(error) : error
    }
  }
  toMatchAriaSnapshot(expected: string, options?: NativeAssertionOptions): Promise<void> {
    return assertNativeAriaSnapshot(
      this.page.locator('body, frameset'),
      expected,
      options,
      this.isNot,
      true,
      this.customMessage,
    )
  }
  toHaveTitle(expected: string | RegExp, options?: NativeAssertionOptions): Promise<void> {
    return new NativeLocatorAssertions(
      this.page.locator('html'),
      this.isNot,
      this.customMessage,
    ).check('toHaveTitle', expected, options)
  }
  async toHaveURL(
    expected: string | RegExp | NativeURLPredicate | NativeURLPattern,
    options?: NativeAssertionOptions,
  ): Promise<void> {
    if (typeof expected === 'function' || isNativeURLPattern(expected)) {
      const config = optionsSchema.parse(options ?? {})
      const soft = nativeSoftOperationOptions(config.timeout)
      const predicate =
        typeof expected === 'function' ? expected : (url: URL) => expected.test(url.href)
      const observed = await runNativeURLPredicate(this.page.source, predicate, {
        timeout: soft?.timeout ?? config.timeout,
        isNot: this.isNot,
        ...(soft ? { signal: soft.signal } : {}),
      })
      if (observed.matches === this.isNot)
        throw nativeMatcherFailure(
          new NativeAssertionError(
            nativeMatcherMessage(
              `expect(page).${this.isNot ? 'not.' : ''}toHaveURL(expected) failed\n\nExpected: predicate to ${this.isNot ? 'fail' : 'succeed'}\nReceived: ${JSON.stringify(observed.url)}\nTimeout:  ${config.timeout ?? 5000}ms\n\nCall log:\n  - Expect "${this.isNot ? 'not ' : ''}toHaveURL" with timeout ${config.timeout ?? 5000}ms\n`,
              this.customMessage,
            ),
          ),
        )
      return
    }
    return new NativeLocatorAssertions(
      this.page.locator('html'),
      this.isNot,
      this.customMessage,
    ).check('toHaveURL', expected, options)
  }
}

/** HTTP responses are immutable; their status needs no browser retry loop. */
export class NativeAPIResponseAssertions {
  constructor(
    private readonly response: NativeAPIResponse,
    private readonly isNot = false,
    private readonly customMessage?: string,
  ) {}
  get not(): NativeAPIResponseAssertions {
    return new NativeAPIResponseAssertions(this.response, !this.isNot, this.customMessage)
  }
  async toBeOK(): Promise<void> {
    const soft = nativeSoftOperationOptions(0)
    if (soft) {
      const observed = await observeNativeAPIResponseAssertion(this.response, {
        isNot: this.isNot,
        timeoutMs: soft.timeout,
        signal: soft.signal,
      })
      if (observed.ok !== this.isNot) return
      let message =
        `expect(response).${this.isNot ? 'not.' : ''}toBeOK() failed\n\n` +
        `→ ${observed.method} ${observed.url}\n← ${observed.status} ${observed.statusText}`
      if (observed.text !== undefined) {
        message += `\n\nResponse text:\n${observed.text.slice(0, 4096)}`
        if (observed.text.length > 4096)
          message += '\n<response diagnostic truncated at 4096 characters>'
      }
      throw nativeMatcherFailure(
        new NativeAssertionError(nativeMatcherMessage(message, this.customMessage)),
      )
    }
    if (this.response.ok() !== this.isNot) return
    let message =
      `expect(response).${this.isNot ? 'not.' : ''}toBeOK() failed\n\n` +
      `→ ${this.response.requestMethod()} ${this.response.url()}\n` +
      `← ${this.response.status()} ${this.response.statusText()}`
    const contentType = this.response.headers()['content-type']?.toLowerCase() ?? ''
    if (
      /^(?:text\/|application\/(?:json|xml|javascript|[^;]+\+json|[^;]+\+xml))/u.test(contentType)
    ) {
      try {
        const text = await this.response.text()
        message += `\n\nResponse text:\n${text.slice(0, 4096)}`
        if (text.length > 4096) message += '\n<response diagnostic truncated at 4096 characters>'
      } catch (error) {
        message += `\n\nResponse text unavailable: ${error instanceof Error ? error.message : 'native body request failed'}`
      }
    }
    throw new NativeAssertionError(nativeMatcherMessage(message, this.customMessage))
  }
}

export function expectNative(target: NativeRetryCallback): NativeCallbackAssertions
export function expectNative(
  target: NativeLocator,
  message?: NativeExpectMessage,
): NativeLocatorAssertions
export function expectNative(
  target: NativePage,
  message?: NativeExpectMessage,
): NativePageAssertions
export function expectNative(
  target: NativeAPIResponse,
  message?: NativeExpectMessage,
): NativeAPIResponseAssertions
export function expectNative(
  target: NativeLocator | NativePage | NativeAPIResponse | NativeRetryCallback,
  message?: NativeExpectMessage,
):
  | NativeLocatorAssertions
  | NativePageAssertions
  | NativeAPIResponseAssertions
  | NativeCallbackAssertions {
  if (typeof target === 'function') {
    if (message !== undefined)
      throw new Error('Native callback toPass does not support custom messages')
    return new NativeCallbackAssertions(target)
  }
  const customMessage = parseNativeExpectMessage(message)
  if (target instanceof NativeLocator)
    return new NativeLocatorAssertions(target, false, customMessage)
  if (target instanceof NativePage) return new NativePageAssertions(target, false, customMessage)
  if (target instanceof NativeAPIResponse)
    return new NativeAPIResponseAssertions(target, false, customMessage)
  throw new Error('Native assertions require a native locator, page or API response')
}

export type NativeSoftLocatorAssertions = Pick<
  NativeLocatorAssertions,
  (typeof softLocatorMethods)[number]
> & {
  readonly not: NativeSoftLocatorAssertions
}
export type NativeSoftPageAssertions = Pick<
  NativePageAssertions,
  (typeof softPageMethods)[number]
> & {
  readonly not: NativeSoftPageAssertions
}
export type NativeSoftAPIResponseAssertions = Pick<
  NativeAPIResponseAssertions,
  (typeof softResponseMethods)[number]
> & {
  readonly not: NativeSoftAPIResponseAssertions
}
const softLocatorMethods = [
  'toHaveScreenshot',
  'toMatchAriaSnapshot',
  'toHaveText',
  'toContainText',
  'toHaveValue',
  'toHaveValues',
  'toHaveCount',
  'toHaveJSProperty',
  'toHaveAttribute',
  'toHaveClass',
  'toContainClass',
  'toHaveId',
  'toHaveCSS',
  'toHaveAccessibleName',
  'toHaveAccessibleDescription',
  'toHaveAccessibleErrorMessage',
  'toHaveRole',
  'toBeVisible',
  'toBeHidden',
  'toBeAttached',
  'toBeEnabled',
  'toBeDisabled',
  'toBeEditable',
  'toBeChecked',
  'toBeFocused',
  'toBeEmpty',
  'toBeInViewport',
] as const satisfies readonly (keyof NativeLocatorAssertions)[]
const softPageMethods = [
  'toHaveScreenshot',
  'toMatchAriaSnapshot',
  'toHaveTitle',
  'toHaveURL',
] as const satisfies readonly (keyof NativePageAssertions)[]
const softResponseMethods = [
  'toBeOK',
] as const satisfies readonly (keyof NativeAPIResponseAssertions)[]

function softAssertions(
  assertions: NativeLocatorAssertions,
  methods: typeof softLocatorMethods,
): NativeSoftLocatorAssertions
function softAssertions(
  assertions: NativePageAssertions,
  methods: typeof softPageMethods,
): NativeSoftPageAssertions
function softAssertions(
  assertions: NativeAPIResponseAssertions,
  methods: typeof softResponseMethods,
): NativeSoftAPIResponseAssertions
function softAssertions(
  assertions: NativeLocatorAssertions | NativePageAssertions | NativeAPIResponseAssertions,
  methods: readonly string[],
): NativeSoftLocatorAssertions | NativeSoftPageAssertions | NativeSoftAPIResponseAssertions {
  const result: Record<string, UntrustedInput> = Object.create(null) as Record<
    string,
    UntrustedInput
  >
  for (const name of methods) {
    const method = Reflect.get(assertions, name) as UntrustedInput
    if (typeof method !== 'function') throw new Error('Native soft matcher method is unavailable')
    Object.defineProperty(result, name, {
      enumerable: true,
      value: (...args: UntrustedInput[]) =>
        runNativeSoftAssertion(async () => {
          await (Reflect.apply(method, assertions, args) as Promise<void>)
        }),
    })
  }
  Object.defineProperty(result, 'not', {
    enumerable: true,
    get: () =>
      assertions instanceof NativeLocatorAssertions
        ? softAssertions(assertions.not, softLocatorMethods)
        : assertions instanceof NativePageAssertions
          ? softAssertions(assertions.not, softPageMethods)
          : softAssertions(assertions.not, softResponseMethods),
  })
  return Object.freeze(result) as UntrustedInput as
    NativeSoftLocatorAssertions | NativeSoftPageAssertions | NativeSoftAPIResponseAssertions
}

function softExpectNative(
  target: NativeLocator,
  message?: NativeExpectMessage,
): NativeSoftLocatorAssertions
function softExpectNative(
  target: NativePage,
  message?: NativeExpectMessage,
): NativeSoftPageAssertions
function softExpectNative(
  target: NativeAPIResponse,
  message?: NativeExpectMessage,
): NativeSoftAPIResponseAssertions
function softExpectNative(
  target: NativeLocator | NativePage | NativeAPIResponse,
  message?: NativeExpectMessage,
): NativeSoftLocatorAssertions | NativeSoftPageAssertions | NativeSoftAPIResponseAssertions {
  if (arguments.length > 2)
    throw new Error('Native soft assertions accept a target and optional message')
  const customMessage = parseNativeExpectMessage(message)
  if (target instanceof NativeLocator)
    return softAssertions(
      new NativeLocatorAssertions(target, false, customMessage),
      softLocatorMethods,
    )
  if (target instanceof NativePage)
    return softAssertions(new NativePageAssertions(target, false, customMessage), softPageMethods)
  if (target instanceof NativeAPIResponse) {
    if (!hasNativeAPIResponseReceipt(target))
      throw new Error(
        'Native response soft assertions require an actual native HTTP dispatch result',
      )
    return softAssertions(
      new NativeAPIResponseAssertions(target, false, customMessage),
      softResponseMethods,
    )
  }
  throw new Error(
    'Native soft assertions support native locators, pages or admitted HTTP responses in an active native test callback',
  )
}
expectNative.soft = softExpectNative

/** Authored matcher adapter for test-only runners; pure-value assertions stay in the runner. */
export function createNativeMatchers(): Record<
  string,
  (
    this: { isNot: boolean; timeout?: number },
    target: UntrustedInput,
    ...args: UntrustedInput[]
  ) => Promise<{ pass: boolean; message(): string }>
> {
  const matchers = [
    'toHaveText',
    'toContainText',
    'toHaveValue',
    'toHaveValues',
    'toHaveCount',
    'toHaveJSProperty',
    'toHaveAttribute',
    'toHaveClass',
    'toContainClass',
    'toHaveId',
    'toHaveCSS',
    'toHaveAccessibleName',
    'toHaveAccessibleDescription',
    'toHaveAccessibleErrorMessage',
    'toHaveRole',
    'toBeVisible',
    'toBeHidden',
    'toBeAttached',
    'toBeEnabled',
    'toBeDisabled',
    'toBeEditable',
    'toBeChecked',
    'toBeFocused',
    'toBeEmpty',
    'toBeInViewport',
    'toHaveTitle',
    'toHaveURL',
    'toMatchAriaSnapshot',
  ]
  const native = Object.fromEntries(
    matchers.map((matcher) => [
      matcher,
      function (
        this: { isNot: boolean; timeout?: number },
        target: UntrustedInput,
        ...args: UntrustedInput[]
      ) {
        const isPage =
          matcher === 'toHaveTitle' || matcher === 'toHaveURL' || matcher === 'toMatchAriaSnapshot'
        if (!(target instanceof NativeLocator) && !(isPage && target instanceof NativePage))
          throw new Error('Native DOM matcher requires a NativeLocator')
        const locator =
          target instanceof NativePage
            ? target.locator(matcher === 'toMatchAriaSnapshot' ? 'body, frameset' : 'html')
            : target
        const named = ['toHaveJSProperty', 'toHaveAttribute', 'toHaveCSS'].includes(matcher)
        const state = matcher.startsWith('toBe')
        const name = named ? z.string().parse(args[0]) : undefined
        const attributeOptionsOnly =
          matcher === 'toHaveAttribute' &&
          typeof args[1] === 'object' &&
          args[1] !== null &&
          !(args[1] instanceof RegExp)
        const expected = state || attributeOptionsOnly ? undefined : args[named ? 1 : 0]
        const options = optionsSchema.parse(
          args[attributeOptionsOnly ? 1 : state ? 0 : named ? 2 : 1] ?? {},
        )
        if (options.timeout === undefined && this.timeout !== undefined)
          options.timeout = optionsSchema.shape.timeout.parse(this.timeout)
        if (matcher === 'toContainClass') {
          if (expected instanceof RegExp)
            throw new Error('"expected" argument in toContainClass cannot be a RegExp value')
          if (Array.isArray(expected) && expected.some((value) => value instanceof RegExp))
            throw new Error('"expected" argument in toContainClass cannot contain RegExp values')
          if (
            typeof expected !== 'string' &&
            !(Array.isArray(expected) && expected.every((value) => typeof value === 'string'))
          )
            throw new Error('toContainClass expected a string or string array')
        }
        const isNot = this.isNot
        return (async () => {
          try {
            if (matcher === 'toMatchAriaSnapshot')
              await assertNativeAriaSnapshot(
                locator,
                z.string().parse(expected),
                options,
                isNot,
                target instanceof NativePage,
              )
            else if (
              matcher === 'toHaveURL' &&
              target instanceof NativePage &&
              (typeof expected === 'function' || isNativeURLPattern(expected))
            )
              await new NativePageAssertions(target, isNot).toHaveURL(
                typeof expected === 'function'
                  ? async (url) =>
                      z
                        .boolean()
                        .parse(
                          await Promise.resolve(
                            Reflect.apply(expected, undefined, [url]) as UntrustedInput,
                          ),
                        )
                  : expected,
                options,
              )
            else
              await new NativeLocatorAssertions(locator, isNot).check(
                matcher,
                expected,
                options,
                name,
              )
            return { pass: !isNot, message: () => `Native ${matcher} matched` }
          } catch (error) {
            return {
              pass: isNot,
              message: () => (error instanceof Error ? error.message : 'Native assertion failed'),
            }
          }
        })()
      },
    ]),
  )
  return {
    ...native,
    toHaveScreenshot: async function (
      this: { isNot: boolean; timeout?: number },
      target: UntrustedInput,
      ...args: UntrustedInput[]
    ) {
      if (!(target instanceof NativePage) && !(target instanceof NativeLocator))
        throw new Error('Native screenshot assertion requires a NativePage or NativeLocator')
      const expected = args[0]
      if (typeof expected !== 'string' && !Buffer.isBuffer(expected))
        throw new Error(
          'Native screenshot assertion requires an explicit PNG baseline path or Buffer',
        )
      const options = parseNativeScreenshotAssertionOptions(args[1] ?? {}, this.timeout)
      try {
        if (target instanceof NativePage)
          await new NativePageAssertions(target, this.isNot).toHaveScreenshot(expected, options)
        else
          await new NativeLocatorAssertions(target, this.isNot).toHaveScreenshot(expected, options)
        return {
          pass: !this.isNot,
          message: () => 'Native screenshot matched the explicit baseline',
        }
      } catch (error) {
        return {
          pass: this.isNot,
          message: () =>
            error instanceof Error ? error.message : 'Native screenshot assertion failed',
        }
      }
    },
    toBeOK: async function (this: { isNot: boolean }, target: UntrustedInput) {
      if (!(target instanceof NativeAPIResponse))
        throw new Error('toBeOK can be only used with APIResponse object')
      try {
        await new NativeAPIResponseAssertions(target, this.isNot).toBeOK()
        return { pass: !this.isNot, message: () => 'Native HTTP response status matched' }
      } catch (error) {
        return {
          pass: this.isNot,
          message: () => (error instanceof Error ? error.message : 'Native HTTP assertion failed'),
        }
      }
    },
  }
}
