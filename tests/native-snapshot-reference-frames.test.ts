// SPDX-License-Identifier: MIT
import { readFile } from 'node:fs/promises'
import { Script } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { type NativeAriaNode } from '../src/native-aria.js'
import { type UntrustedInput } from '../src/untrusted.js'

const header = await readFile(
  new URL('../native/browser/snapshot_reference_frames.h', import.meta.url),
  'utf8',
)
const literal = /R"JS\(([\s\S]*)\)JS";/u.exec(header)?.[1]
if (!literal) throw new Error('Missing native frame merge helper')
const merge = new Script(`(${literal})`).runInNewContext({ TextEncoder }) as UntrustedInput
const ownerRef = 'rActualMain_1_0',
  childRef = 'rActualChild_2_0'
function capture(): { forest: NativeAriaNode[] } {
  return { forest: [{ role: 'generic', children: [{ role: 'iframe', ref: ownerRef }] }] }
}
function apply(target: { forest: NativeAriaNode[] }, ref: string, child: string): string {
  if (typeof merge !== 'function') throw new Error('Invalid native frame merge helper')
  const value: UntrustedInput = Reflect.apply(merge, target, [ref, child])
  if (typeof value !== 'string') throw new Error('Native frame merge did not serialize its forest')
  return value
}

describe('native retained frame forest merge', () => {
  it('merges genuine child observations by exact opaque owner reference while preserving isolated units', () => {
    const target = capture()
    const child = [
      {
        role: 'textbox',
        ref: childRef,
        name: '\ud800',
        text: '{"__suitecutNativeUTF16Scalar":"dfff"}',
      },
    ]
    const json = apply(target, ownerRef, JSON.stringify(child))
    expect(JSON.parse(json)).toEqual([
      { role: 'generic', children: [{ role: 'iframe', ref: ownerRef, children: child }] },
    ])
    expect(target.forest[0]?.children?.[0]?.children).toEqual(child)
  })
  it('composes nested actual frame forests without changing their reference spelling', () => {
    const child = { forest: [{ role: 'iframe', ref: childRef }] }
    const leaf = [{ role: 'button', name: 'Actual nested child', ref: 'rNested_3_0' }]
    const childJSON = apply(child, childRef, JSON.stringify(leaf))
    expect(JSON.parse(apply(capture(), ownerRef, childJSON))).toEqual([
      {
        role: 'generic',
        children: [
          {
            role: 'iframe',
            ref: ownerRef,
            children: [{ role: 'iframe', ref: childRef, children: leaf }],
          },
        ],
      },
    ])
  })
  it('rejects missing, ambiguous or non-frame owner identities without changing the old private forest', () => {
    for (const target of [
      { forest: [{ role: 'button', ref: ownerRef }] },
      {
        forest: [
          { role: 'iframe', ref: ownerRef },
          { role: 'iframe', ref: ownerRef },
        ],
      },
      { forest: [{ role: 'iframe', ref: childRef }] },
    ]) {
      const before = JSON.stringify(target)
      expect(() => apply(target, ownerRef, '[]')).toThrow(/owner/u)
      expect(JSON.stringify(target)).toBe(before)
    }
  })
  it('validates proposed depth and node bounds before mutating an owner record', () => {
    const root: NativeAriaNode = { role: 'generic' }
    let next = root
    for (let i = 0; i < 64; i++) {
      const node: NativeAriaNode = { role: 'generic' }
      next.children = [node]
      next = node
    }
    for (const child of [[root], Array.from({ length: 4096 }, () => ({ role: 't' }))]) {
      const target = capture(),
        before = JSON.stringify(target)
      expect(() => apply(target, ownerRef, JSON.stringify(child))).toThrow(/depth|record/u)
      expect(JSON.stringify(target)).toBe(before)
    }
  })
  it('rolls back exactly the previous children when genuine UTF8 output exceeds the byte cap', () => {
    const target = capture(),
      owner = target.forest[0]?.children?.[0]
    if (!owner) throw new Error('Missing test owner')
    const previous: NativeAriaNode[] = [{ role: 'text', text: 'Previous' }]
    owner.children = previous
    expect(() =>
      apply(target, ownerRef, JSON.stringify([{ role: 'text', text: '界'.repeat(22000) }])),
    ).toThrow(/64 KiB/u)
    expect(owner.children).toBe(previous)
    const empty = capture()
    expect(() =>
      apply(empty, ownerRef, JSON.stringify([{ role: 'text', text: '界'.repeat(22000) }])),
    ).toThrow(/64 KiB/u)
    expect(Object.hasOwn(empty.forest[0]?.children?.[0] ?? {}, 'children')).toBe(false)
  })
  it('rejects malformed child encoding and scalar records without decoding author tag-shaped values', () => {
    for (const child of ['{', '{}', '[17]', '[null]']) {
      const target = capture(),
        before = JSON.stringify(target)
      expect(() => apply(target, ownerRef, child)).toThrow()
      expect(JSON.stringify(target)).toBe(before)
    }
  })
})
