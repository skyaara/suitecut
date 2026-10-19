// SPDX-License-Identifier: MIT
import { resolve } from 'node:path'

import { describe, expect, it } from 'vitest'

import {
  NativeSnapshotCallbacks,
  nativeTestSnapshots,
} from '../src/native-runner-snapshot-paths.js'

const literal = (value: string) => ({ kind: 'literal', value })
const argument = { kind: 'arg', value: '' }
const extension = { kind: 'ext', value: '' }
const plan = () => ({
  version: 1,
  baseDirectory: '/actual/native/module',
  snapshotDir: '/actual/native/module/test.mjs-snapshots',
  snapshot: [literal('/actual/snapshots/'), argument, literal('-proj-linux'), extension],
  screenshot: [literal('/actual/pixels/'), argument, extension],
  aria: [literal('/actual/semantics/'), argument, extension],
})
const packet = (id = '1', value = plan()) => [
  'TEST_SNAPSHOT_CONTEXT',
  id,
  Buffer.from(JSON.stringify(value)).toString('hex'),
]
const shared = (prefix = '/actual/common/') => ({
  version: 2,
  baseDirectory: '/actual/native/module',
  snapshotDir: '/actual/native/module/test.mjs-snapshots',
  plans: [{ id: 1, parts: [literal(prefix), argument, extension] }],
  kinds: { snapshot: 1, screenshot: 1, aria: 1 },
})
const mutable = () => {
  const generic = [
    literal('/actual/file.mjs-snapshots/'),
    argument,
    literal('-named'),
    { kind: 'suffix', value: '-' },
    extension,
  ]
  return {
    version: 3,
    suffix: { initial: 'linux', maxBytes: 1024, policy: 'filenameComponent' },
    paths: {
      ...plan(),
      snapshot: generic,
      screenshot: generic,
      aria: [literal('/actual/file.mjs-snapshots/'), argument, extension],
    },
  }
}

describe('native-issued snapshot token plans', () => {
  it('inserts argument/ext only and keeps snapshotDir independent of configured templates', () => {
    const info = nativeTestSnapshots(plan())
    expect(info.snapshotDir).toBe('/actual/native/module/test.mjs-snapshots')
    expect(info.snapshotPath('image.png')).toBe('/actual/snapshots/image-proj-linux.png')
    expect(info.snapshotPath('foo', 'bar', 'baz.jpeg')).toBe(
      '/actual/snapshots/foo/bar/baz-proj-linux.jpeg',
    )
    expect(info.snapshotPath('image.png', { kind: 'screenshot' })).toBe('/actual/pixels/image.png')
    expect(info.snapshotPath('tree.aria.yml', { kind: 'aria' })).toBe(
      '/actual/semantics/tree.aria.yml',
    )
    expect(Object.isFrozen(info)).toBe(true)
    const explicit = mutable()
    explicit.paths.screenshot = [literal('/actual/literal-linux')]
    const literalKind = nativeTestSnapshots(explicit)
    literalKind.snapshotSuffix = 'custom'
    expect(literalKind.snapshotPath('tree.png', { kind: 'screenshot' })).toBe(
      '/actual/literal-linux',
    )
    expect(literalKind.snapshotPath('tree.png')).toBe(
      '/actual/file.mjs-snapshots/tree-named-custom.png',
    )
  })
  it('supports extension-before-argument and trusted relative/empty native template output', () => {
    const raw = plan()
    raw.snapshot = [extension, argument]
    expect(nativeTestSnapshots(raw).snapshotPath('foo', 'bar', 'baz.jpeg')).toBe(
      resolve(raw.baseDirectory, '.jpegfoo/bar/baz'),
    )
    raw.snapshot = [literal('')]
    expect(nativeTestSnapshots(raw).snapshotPath('unused.png')).toBe(raw.baseDirectory)
  })
  it('rejects unknown kind, mixed name/kind shape, missing name and unsafe/unrepresentable arguments', () => {
    const path = nativeTestSnapshots(plan()).snapshotPath
    expect(() => {
      Reflect.apply(path, null, ['foo', { kind: 'bar' }])
    }).toThrow('unknown kind "bar"')
    for (const args of [
      [],
      ['foo', 'bar', { kind: 'aria' }],
      ['../foreign'],
      ['/absolute'],
      ['a\0b'],
      ['\ud800'],
      ['x'.repeat(4097)],
    ])
      expect(() => {
        Reflect.apply(path, null, args)
      }).toThrow()
    expect(path('a', '..', 'kept.png')).toBe('/actual/snapshots/kept-proj-linux.png')
  })
  it('does not replace native tokens that appear in authored names', () => {
    expect(nativeTestSnapshots(plan()).snapshotPath('{testName}.png')).toBe(
      '/actual/snapshots/{testName}-proj-linux.png',
    )
  })
  it('does not turn unsupported filesystem names into callback admission failure', () => {
    const info = nativeTestSnapshots({
      version: 1,
      unsupported: 'Native filesystem encoding unavailable',
    })
    expect(() => info.snapshotPath('path.png')).toThrow('encoding unavailable')
    expect(() => info.snapshotDir).toThrow('encoding unavailable')
    const independent = nativeTestSnapshots({
      version: 1,
      unsupported: 'Unrepresentable testName',
      snapshotDir: '/actual/real/suite.mjs-snapshots',
    })
    expect(independent.snapshotDir).toBe('/actual/real/suite.mjs-snapshots')
    expect(() => independent.snapshotPath('safe.png')).toThrow('testName')
  })
  it('admits only unique native test declarations, preserves existing plans on malformed packets', () => {
    const callbacks = new NativeSnapshotCallbacks()
    callbacks.packet(packet(), 2)
    expect(() => callbacks.packet(packet(), 2)).toThrow('actual test')
    expect(() => callbacks.packet(packet('3'), 2)).toThrow('actual test')
    expect(() => callbacks.packet(['TEST_SNAPSHOT_CONTEXT', '2', 'ff'], 2)).toThrow('encoding')
    expect(() => callbacks.snapshot('2')).toThrow('admission')
    expect(callbacks.snapshot('1').snapshotPath('safe.png')).toBe(
      '/actual/snapshots/safe-proj-linux.png',
    )
    callbacks.packet(packet('2'), 2)
    expect(callbacks.snapshot('2').snapshotDir).toBe(plan().snapshotDir)
  })
  it('retains the admitted copied plan rather than later mutable author metadata', () => {
    const raw = plan(),
      info = nativeTestSnapshots(raw)
    raw.snapshot[0] = literal('/foreign/')
    expect(info.snapshotPath('image.png')).toBe('/actual/snapshots/image-proj-linux.png')
    expect(() => nativeTestSnapshots({ ...plan(), baseDirectory: '/actual/../foreign' })).toThrow()
    expect(() =>
      nativeTestSnapshots({ ...plan(), aria: [{ kind: 'eval', value: 'arbitrary' }] }),
    ).toThrow()
  })
  it('bounds final UTF8 paths after all inserted argument copies', () => {
    const raw = plan()
    raw.snapshot = Array.from({ length: 4 }, () => argument)
    expect(() => nativeTestSnapshots(raw).snapshotPath('a'.repeat(3000) + '.png')).toThrow(
      'filesystem bound',
    )
  })
  it('uses native local IDs for every kind without inferring from definition order', () => {
    const info = nativeTestSnapshots(shared())
    for (const kind of ['snapshot', 'screenshot', 'aria'] as const)
      expect(info.snapshotPath('same.txt', { kind })).toBe('/actual/common/same.txt')
    const reordered = {
      ...shared(),
      plans: [
        { id: 3, parts: [literal('/actual/third/'), argument] },
        { id: 1, parts: [literal('/actual/first/'), argument] },
      ],
      kinds: { snapshot: 3, screenshot: 1, aria: 3 },
    }
    const actual = nativeTestSnapshots(reordered)
    expect(actual.snapshotPath('file.txt')).toBe('/actual/third/file')
    expect(actual.snapshotPath('file.txt', { kind: 'screenshot' })).toBe('/actual/first/file')
  })
  it('rejects missing, foreign, duplicate and unbounded native references instead of falling back', () => {
    for (const input of [
      { ...shared(), kinds: { snapshot: 2, screenshot: 1, aria: 1 } },
      { ...shared(), plans: [] },
      { ...shared(), plans: [...shared().plans, ...shared().plans] },
      { ...shared(), plans: [{ id: 0, parts: [literal('x')] }] },
      { ...shared(), kinds: { snapshot: 1, screenshot: 4, aria: 1 } },
      { ...shared(), kinds: { snapshot: 1, screenshot: 1, aria: 1, fake: 1 } },
      { ...shared(), plans: [{ id: 1, parts: Array.from({ length: 257 }, () => argument) }] },
      { ...shared(), plans: [{ id: 1, parts: [literal('x'.repeat(32769))] }] },
    ])
      expect(() => nativeTestSnapshots(input)).toThrow()
  })
  it('keeps plan IDs local to the actual admitted DTO and preserves earlier snapshot closures', () => {
    const first = nativeTestSnapshots(shared('/project-first/'))
    const second = nativeTestSnapshots(shared('/project-second/'))
    expect(first.snapshotPath('x.png')).toBe('/project-first/x.png')
    expect(second.snapshotPath('x.png')).toBe('/project-second/x.png')
    const callbacks = new NativeSnapshotCallbacks()
    callbacks.packet(
      [
        'TEST_SNAPSHOT_CONTEXT',
        '1',
        Buffer.from(JSON.stringify(shared('/native-first/'))).toString('hex'),
      ],
      2,
    )
    callbacks.packet(
      [
        'TEST_SNAPSHOT_CONTEXT',
        '2',
        Buffer.from(JSON.stringify(shared('/native-second/'))).toString('hex'),
      ],
      2,
    )
    expect(callbacks.snapshot('1').snapshotPath('x.png')).toBe('/native-first/x.png')
    expect(callbacks.snapshot('2').snapshotPath('x.png')).toBe('/native-second/x.png')
  })
  it('formats native-issued default ARIA independently of generic project and suffix parts', () => {
    // These are supplied native-plan DTO cases, not a mutable TestInfo setter.
    for (const suffix of ['', 'linux', 'custom']) {
      const tail = '-named-project' + (suffix ? '-' + suffix : '')
      const generic = [literal('/actual/file.mjs-snapshots/'), argument, literal(tail), extension]
      const info = nativeTestSnapshots({
        ...plan(),
        snapshot: generic,
        screenshot: generic,
        aria: [literal('/actual/file.mjs-snapshots/'), argument, extension],
      })
      expect(info.snapshotPath('tree.png')).toBe('/actual/file.mjs-snapshots/tree' + tail + '.png')
      expect(info.snapshotPath('tree.png', { kind: 'screenshot' })).toBe(
        info.snapshotPath('tree.png'),
      )
      expect(info.snapshotPath('tree.png', { kind: 'aria' })).toBe(
        '/actual/file.mjs-snapshots/tree.png',
      )
    }
  })
  it('uses only native-issued suffix opcodes and keeps a live bounded author cell', () => {
    const info = nativeTestSnapshots(mutable())
    expect(info.snapshotSuffix).toBe('linux')
    for (const suffix of ['', 'custom']) {
      info.snapshotSuffix = suffix
      expect(info.snapshotPath('tree.png')).toBe(
        '/actual/file.mjs-snapshots/tree-named' + (suffix ? '-' + suffix : '') + '.png',
      )
      expect(info.snapshotPath('tree.png', { kind: 'screenshot' })).toBe(
        info.snapshotPath('tree.png'),
      )
      expect(info.snapshotPath('tree.png', { kind: 'aria' })).toBe(
        '/actual/file.mjs-snapshots/tree.png',
      )
    }
    for (const invalid of [
      '.',
      '..',
      '../foreign',
      'a/b',
      'a\\b',
      '\0',
      '\ud800',
      'x'.repeat(1025),
      1,
      null,
    ]) {
      expect(() => Reflect.set(info, 'snapshotSuffix', invalid)).toThrow()
      expect(info.snapshotSuffix).toBe('custom')
    }
    expect(Object.isFrozen(info)).toBe(true)
  })
  it('keeps suffix mutation scoped to the actual native test and supports unavailable paths independently', () => {
    const callbacks = new NativeSnapshotCallbacks()
    for (const id of ['1', '2'])
      callbacks.packet(
        ['TEST_SNAPSHOT_CONTEXT', id, Buffer.from(JSON.stringify(mutable())).toString('hex')],
        2,
      )
    const first = callbacks.admit('1', 0)
    first.snapshotSuffix = 'custom'
    expect(callbacks.snapshot('1')).toBe(first)
    expect(callbacks.snapshot('1').snapshotSuffix).toBe('custom')
    expect(callbacks.snapshot('2').snapshotSuffix).toBe('linux')
    expect(callbacks.admit('1', 0)).toBe(first)
    const retry = callbacks.admit('1', 1)
    expect(retry).not.toBe(first)
    expect(retry.snapshotSuffix).toBe('linux')
    first.snapshotSuffix = 'stale'
    expect(retry.snapshotSuffix).toBe('linux')
    expect(() => callbacks.admit('1', 101)).toThrow()
    expect(() => callbacks.admit('foreign', 0)).toThrow('no associated test admission')
    expect(callbacks.admit('1', 1)).toBe(retry)
    const fresh = new NativeSnapshotCallbacks()
    fresh.packet(
      ['TEST_SNAPSHOT_CONTEXT', '1', Buffer.from(JSON.stringify(mutable())).toString('hex')],
      1,
    )
    expect(fresh.snapshot('1').snapshotSuffix).toBe('linux')
    const unsupported = nativeTestSnapshots({
      ...mutable(),
      paths: {
        version: 1,
        unsupported: 'Actual title encoding unsupported',
        snapshotDir: '/actual/native',
      },
    })
    unsupported.snapshotSuffix = ''
    expect(unsupported.snapshotSuffix).toBe('')
    expect(unsupported.snapshotDir).toBe('/actual/native')
    expect(() => unsupported.snapshotPath('x')).toThrow('Actual title encoding unsupported')
    expect(() => nativeTestSnapshots(plan()).snapshotSuffix).toThrow('no issued policy')
  })
  it('rejects malformed native suffix policy and references rather than granting author authority', () => {
    for (const input of [
      {
        ...mutable(),
        suffix: { initial: '../foreign', maxBytes: 1024, policy: 'filenameComponent' },
      },
      { ...mutable(), suffix: { initial: 'linux', maxBytes: 2048, policy: 'filenameComponent' } },
      { ...mutable(), suffix: { initial: 'linux', maxBytes: 1024, policy: 'path' } },
      { ...mutable(), paths: { ...shared(), kinds: { snapshot: 2, screenshot: 1, aria: 1 } } },
      mutable().paths,
    ])
      expect(() => nativeTestSnapshots(input)).toThrow()
    const pooled = nativeTestSnapshots({
      ...mutable(),
      paths: { ...shared(), plans: [{ id: 1, parts: mutable().paths.snapshot }] },
    })
    pooled.snapshotSuffix = 'custom'
    expect(pooled.snapshotPath('tree.png', { kind: 'aria' })).toBe(
      '/actual/file.mjs-snapshots/tree-named-custom.png',
    )
  })
})
