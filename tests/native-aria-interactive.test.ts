// SPDX-License-Identifier: MIT
import { expect, it } from 'vitest'

import { formatNativeAriaInteractive, type NativeAriaJSONNode } from '../src/native-aria.js'

it('keeps controls, headings and named landmarks, with the full snapshot refs', () => {
  const forest: NativeAriaJSONNode[] = [
    {
      role: 'banner',
      ref: 's1e1',
      children: [
        { role: 'heading', name: 'Navigation Menu', level: 2, ref: 's1e2' },
        {
          role: 'navigation',
          name: 'Global',
          ref: 's1e4',
          children: [
            {
              role: 'list',
              ref: 's1e5',
              children: [
                {
                  role: 'listitem',
                  ref: 's1e6',
                  children: [{ role: 'button', name: 'Products', expanded: false, ref: 's1e7' }],
                },
                {
                  role: 'listitem',
                  ref: 's1e8',
                  children: [
                    {
                      role: 'link',
                      name: 'Pricing',
                      url: '/pricing',
                      ref: 's1e9',
                      children: ['Pricing'],
                    },
                  ],
                },
              ],
            },
          ],
        },
      ],
    },
    { role: 'paragraph', ref: 's1e10', children: ['Plain text the agent cannot act on.'] },
    { role: 'navigation', name: 'Empty', ref: 's1e11', children: ['no controls'] },
    {
      role: 'generic',
      cursor: 'pointer',
      ref: 's1e12',
      children: [{ role: 'text', text: 'Clickable card' }],
    },
    { role: 'textbox', name: 'Search', placeholder: 'Type to search', text: 'cats', ref: 's1e13' },
  ]
  expect(formatNativeAriaInteractive(forest)).toBe(
    [
      '- heading "Navigation Menu" [level=2] [ref=s1e2]',
      '- navigation "Global" [ref=s1e4]',
      '  - button "Products" [expanded=false] [ref=s1e7]',
      '  - link "Pricing" [ref=s1e9]',
      '- generic "Clickable card" [ref=s1e12]',
      '- textbox "Search" [placeholder="Type to search"] [ref=s1e13]: "cats"',
    ].join('\n'),
  )
})

it('keeps long names whole and keeps dialogs without a name', () => {
  const forest: NativeAriaJSONNode[] = [
    {
      role: 'dialog',
      ref: 's1e1',
      children: [{ role: 'button', name: 'x'.repeat(150), ref: 's1e2' }],
    },
  ]
  expect(formatNativeAriaInteractive(forest)).toBe(
    `- dialog [ref=s1e1]\n  - button "${'x'.repeat(150)}" [ref=s1e2]`,
  )
})

it('names nested controls from their own subtree text, collapsing whitespace once', () => {
  let node: NativeAriaJSONNode = { role: 'text', text: ' leaf  text ' }
  for (let depth = 0; depth < 400; depth++)
    node = { role: 'button', text: depth % 2 ? `b${depth}` : '', children: [node, ' '] }
  const lines = formatNativeAriaInteractive([node]).split('\n')
  expect(lines).toHaveLength(400)
  expect(lines.at(-1)).toBe(`${'  '.repeat(399)}- button "leaf text"`)
  expect(lines.at(-2)).toBe(`${'  '.repeat(398)}- button "b1 leaf text"`)
  expect(lines[0]).toMatch(/^- button "b399 b397 .* b1 leaf text"$/u)
  // A text node's own text names it; its children are named from their own text only.
  expect(
    formatNativeAriaInteractive([
      {
        role: 'text',
        text: 'outer',
        cursor: 'pointer',
        ref: 'e1',
        children: [{ role: 'button', children: ['inner ', { role: 'text', name: ' label ' }] }],
      },
    ]),
  ).toBe('- text "outer" [ref=e1]\n  - button "inner label"')
})
