// SPDX-License-Identifier: MIT
import * as z from 'zod'

export type NativeTextPattern =
  { text: string; exact?: boolean } | { pattern: string; flags: string }

export interface NativeRoleOptions {
  name?: string | RegExp
  description?: string | RegExp
  exact?: boolean
  includeHidden?: boolean
  checked?: boolean | 'mixed'
  disabled?: boolean
  expanded?: boolean
  level?: number
  pressed?: boolean | 'mixed'
  selected?: boolean
}

export type NativeQueryStep =
  | { engine: 'css'; selector: string }
  | { engine: 'reference'; ref: string }
  | { engine: 'custom'; name: string; selector: string }
  | { engine: 'nth'; index: number }
  | ({ engine: 'text' | 'label' | 'placeholder' | 'alt' | 'title' } & NativeTextPattern)
  | ({ engine: 'testId'; attribute?: string } & NativeTextPattern)
  | ({
      engine: 'role'
      role: string
      name?: NativeTextPattern
      description?: NativeTextPattern
    } & Omit<NativeRoleOptions, 'name' | 'description' | 'exact'>)
  | {
      engine: 'filter'
      hasText?: NativeTextPattern
      hasNotText?: NativeTextPattern
      has?: NativeQueryStep[]
      hasNot?: NativeQueryStep[]
      visible?: boolean
    }
  | { engine: 'and' | 'or' | 'chain'; query: NativeQueryStep[] }
  | { engine: 'invalid'; message: string }

/** Validates an opaque native reference; it never parses CSS or author metadata. */
export function nativeReferenceQuery(ref: string): NativeQueryStep {
  return {
    engine: 'reference',
    ref: z
      .string()
      .max(192)
      .regex(/^r[A-Za-z0-9]{1,64}_[1-9][0-9]*_(?:0|[1-9][0-9]*)$/u)
      .parse(ref),
  }
}

export function nativeTextPattern(
  value: string | RegExp,
  options: { exact?: boolean } = {},
): NativeTextPattern {
  const config = z.strictObject({ exact: z.boolean().optional() }).parse(options)
  if (value instanceof RegExp) return { pattern: value.source, flags: value.flags }
  return {
    text: z.string().parse(value),
    ...(config.exact !== undefined ? { exact: config.exact } : {}),
  }
}

export function nativeRoleQuery(role: string, options: NativeRoleOptions = {}): NativeQueryStep {
  const config = z
    .strictObject({
      name: z.union([z.string(), z.instanceof(RegExp)]).optional(),
      description: z.union([z.string(), z.instanceof(RegExp)]).optional(),
      exact: z.boolean().optional(),
      includeHidden: z.boolean().optional(),
      checked: z.union([z.boolean(), z.literal('mixed')]).optional(),
      disabled: z.boolean().optional(),
      expanded: z.boolean().optional(),
      level: z.number().int().positive().optional(),
      pressed: z.union([z.boolean(), z.literal('mixed')]).optional(),
      selected: z.boolean().optional(),
    })
    .parse(options)
  const roleName = z.string().min(1).parse(role)
  const supported = {
    checked: ['checkbox', 'menuitemcheckbox', 'menuitemradio', 'radio', 'switch'],
    pressed: ['button'],
    selected: ['gridcell', 'option', 'row', 'tab', 'rowheader', 'columnheader', 'treeitem'],
    expanded: [
      'application',
      'button',
      'checkbox',
      'combobox',
      'gridcell',
      'link',
      'listbox',
      'menuitem',
      'row',
      'rowheader',
      'tab',
      'treeitem',
    ],
    level: ['heading', 'listitem', 'row', 'treeitem'],
  }
  for (const key of ['checked', 'pressed', 'selected', 'expanded', 'level'] as const)
    if (config[key] !== undefined && !supported[key].includes(roleName))
      throw new Error(
        `"${key}" attribute is only supported for roles: ${supported[key].map((value) => `"${value}"`).join(', ')}`,
      )
  const { name, description, exact, ...states } = config
  return {
    engine: 'role',
    role: roleName,
    ...(states.includeHidden !== undefined ? { includeHidden: states.includeHidden } : {}),
    ...(states.checked !== undefined ? { checked: states.checked } : {}),
    ...(states.disabled !== undefined ? { disabled: states.disabled } : {}),
    ...(states.expanded !== undefined ? { expanded: states.expanded } : {}),
    ...(states.level !== undefined ? { level: states.level } : {}),
    ...(states.pressed !== undefined ? { pressed: states.pressed } : {}),
    ...(states.selected !== undefined ? { selected: states.selected } : {}),
    ...(description !== undefined
      ? { description: nativeTextPattern(description, exact !== undefined ? { exact } : {}) }
      : {}),
    ...(name !== undefined
      ? { name: nativeTextPattern(name, exact !== undefined ? { exact } : {}) }
      : {}),
  }
}

let testIdAttribute = 'data-testid'

/** Configure the authored native locator namespace's test id attributes. */
export function setNativeTestIdAttribute(attribute: string): void {
  const value = z.string().min(1).parse(attribute)
  const attributes = value.split(',').map((part) => part.trim())
  if (attributes.some((part) => !/^[A-Za-z_][A-Za-z0-9_.:-]*$/u.test(part)))
    throw new Error('Test id attributes must be valid attribute names separated by commas')
  testIdAttribute = [...new Set(attributes)].join(',')
}

export function nativeTestIdQuery(value: string | RegExp): NativeQueryStep {
  return {
    engine: 'testId',
    attribute: testIdAttribute,
    ...nativeTextPattern(value, { exact: true }),
  }
}

/** Describe a native locator using its public construction steps. */
export function nativeQueryDescription(
  query: NativeQueryStep[],
  framePath: NativeQueryStep[][] = [],
): string {
  const quote = (value: string): string =>
    `'${value.replaceAll('\\', '\\\\').replaceAll("'", "\\'").replaceAll('\n', '\\n').replaceAll('\r', '\\r')}'`
  const pattern = (value: NativeTextPattern): string =>
    'pattern' in value ? `/${value.pattern}/${value.flags}` : quote(value.text)
  const describe = (steps: NativeQueryStep[], depth = 0): string => {
    if (depth > 64) return '<native locator nesting limit>'
    return steps
      .map((step) => {
        switch (step.engine) {
          case 'reference':
            return `getByRef(${quote(step.ref)})`
          case 'custom':
            return `locator(${quote(`${step.name}=${step.selector}`)})`
          case 'css':
            return `locator(${quote(step.selector)})`
          case 'nth':
            return `nth(${step.index})`
          case 'text':
            return `getByText(${pattern(step)})`
          case 'label':
            return `getByLabel(${pattern(step)})`
          case 'placeholder':
            return `getByPlaceholder(${pattern(step)})`
          case 'alt':
            return `getByAltText(${pattern(step)})`
          case 'title':
            return `getByTitle(${pattern(step)})`
          case 'testId':
            return `getByTestId(${pattern(step)})`
          case 'role':
            return `getByRole(${quote(step.role)}${step.name ? `, { name: ${pattern(step.name)} }` : ''})`
          case 'and':
          case 'or':
            return `${step.engine}(${describe(step.query, depth + 1)})`
          case 'chain':
            return `locator(${describe(step.query, depth + 1)})`
          case 'filter':
            return 'filter(...)'
          case 'invalid':
            return '<invalid native locator>'
        }
      })
      .join('.')
  }
  return [...framePath.map((path) => `${describe(path)}.contentFrame()`), describe(query)].join('.')
}
