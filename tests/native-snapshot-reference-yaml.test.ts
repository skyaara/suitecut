// SPDX-License-Identifier: MIT
import { readFile } from 'node:fs/promises'
import { Script } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { type NativeAriaNode } from '../src/native-aria.js'
import { type UntrustedInput } from '../src/untrusted.js'

async function literal(file: string): Promise<UntrustedInput> {
  const header = await readFile(new URL(`../native/browser/${file}`, import.meta.url), 'utf8')
  const source = /R"JS\(([\s\S]*)\)JS";/u.exec(header)?.[1]
  if (!source) throw new Error('Missing canonical native helper')
  return new Script(`(${source})`).runInNewContext({ TextEncoder }) as UntrustedInput
}
const merge = await literal('snapshot_reference_frames.h')
const formatter = await literal('native_aria_formatter.h')
const owner = 'rActualMain_1_0',
  child = 'rActualChild_2_0'
function merged(children: NativeAriaNode[]): { forest: NativeAriaNode[] } {
  if (typeof merge !== 'function') throw new Error('Missing native merge')
  const capture = {
    forest: [
      { role: 'iframe', ref: owner, box: [0, 0, 100, 80] as [number, number, number, number] },
    ],
  }
  Reflect.apply(merge, capture, [owner, JSON.stringify(children)])
  return capture
}
function format(
  capture: { forest: NativeAriaNode[] },
  options: { mode?: 'ai'; depth?: number; boxes?: boolean } = { mode: 'ai' },
): string {
  if (typeof formatter !== 'function') throw new Error('Missing canonical YAML formatter')
  const value: UntrustedInput = Reflect.apply(formatter, undefined, [capture.forest, options])
  if (typeof value !== 'string') throw new Error('Invalid canonical YAML output')
  return value
}

describe('canonical native YAML over composed frame observations', () => {
  it('formats the actual merged forests directly with genuine opaque references', () => {
    expect(format(merged([{ role: 'button', ref: child, name: 'Child' }]))).toBe(
      `- iframe [ref=${owner}]:\n  - button "Child" [ref=${child}]`,
    )
  })
  it('applies canonical depth and genuine per-node boxes after composition', () => {
    const capture = merged([{ role: 'button', ref: child, name: 'Child', box: [2, 3, 40, 20] }])
    expect(format(capture, { mode: 'ai', depth: 0, boxes: true })).toBe(
      `- iframe [box=0,0,100,80] [ref=${owner}]`,
    )
    expect(format(capture, { mode: 'ai', boxes: true })).toContain(
      `  - button "Child" [box=2,3,40,20] [ref=${child}]`,
    )
    expect(format(capture, {})).toBe('- iframe:\n  - button "Child"')
  })
  it('preserves isolated units and author tag-shaped text without parsing or rewriting YAML', () => {
    const text = '{"__suitecutNativeUTF16Scalar":"dfff"}'
    expect(format(merged([{ role: 'textbox', ref: child, name: '\ud800', text }]))).toBe(
      `- iframe [ref=${owner}]:\n  - textbox "\\ud800" [ref=${child}]: ${JSON.stringify(text)}`,
    )
  })
  it('enforces YAML output bounds independently of the smaller genuine JSON forest', () => {
    const root: NativeAriaNode = { role: 'group' }
    let node = root
    for (let i = 0; i < 55; i++) {
      const child: NativeAriaNode = { role: 'group' }
      node.children = [child]
      node = child
    }
    node.children = Array.from({ length: 240 }, () => ({ role: 'text', text: 'x' }))
    const capture = merged([root]),
      before = JSON.stringify(capture)
    expect(new TextEncoder().encode(before).byteLength).toBeLessThan(65536)
    expect(() => format(capture)).toThrow(/64 KiB/u)
    expect(JSON.stringify(capture)).toBe(before)
  })
})
