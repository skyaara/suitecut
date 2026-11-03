// SPDX-License-Identifier: MIT
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { runInNewContext } from 'node:vm'

import { describe, expect, it } from 'vitest'

import { nativeInitScriptSource } from '../src/native-init-script.js'

describe('native initialization source transport', () => {
  it('preserves structured initialization arguments without live native handles', async () => {
    const script = await nativeInitScriptSource(
      ({ date, value, map }) => {
        Reflect.set(globalThis, 'observed', `${date.toISOString()}:${value}:${map.key}`)
      },
      { date: new Date('2026-01-02T03:04:05Z'), value: 42n, map: { key: 'text' } },
    )
    const realm = { observed: '' }
    runInNewContext(script, realm)
    expect(realm.observed).toBe('2026-01-02T03:04:05.000Z:42:text')
    await expect(nativeInitScriptSource(() => undefined, { fn: () => true })).rejects.toThrow(
      'unexpected value',
    )
  })

  it('accepts scripts of any size and rejects nulls', async () => {
    const big = '"'.repeat(4 * 1024 * 1024)
    expect(await nativeInitScriptSource(big)).toBe(big)
    await expect(nativeInitScriptSource('\0')).rejects.toThrow('no nulls')
  })

  it('reads script files of any size', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-init-'))
    const path = join(directory, 'init.js')
    try {
      await writeFile(path, 'globalThis.fromFile = 123;')
      expect(await nativeInitScriptSource({ path })).toBe('globalThis.fromFile = 123;')
      await writeFile(path, 'x'.repeat(4 * 1024 * 1024))
      expect(await nativeInitScriptSource({ path })).toHaveLength(4 * 1024 * 1024)
      await expect(nativeInitScriptSource({ path, content: 'x' })).rejects.toThrow('exactly one')
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })
})
