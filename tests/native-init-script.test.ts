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

  it('rejects oversized escaped commands before sending them to the native process', async () => {
    await expect(nativeInitScriptSource('"'.repeat(40000))).rejects.toThrow('envelope capacity')
    await expect(nativeInitScriptSource('x'.repeat(65537))).rejects.toThrow('65536 bytes')
    await expect(nativeInitScriptSource('\0')).rejects.toThrow('without nulls')
  })

  it('bounds file reads and closes descriptors on success and failure', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-init-'))
    const path = join(directory, 'init.js')
    try {
      await writeFile(path, 'globalThis.fromFile = 123;')
      expect(await nativeInitScriptSource({ path })).toBe('globalThis.fromFile = 123;')
      await writeFile(path, 'x'.repeat(65537))
      await expect(nativeInitScriptSource({ path })).rejects.toThrow('65536 bytes')
      await expect(nativeInitScriptSource({ path, content: 'x' })).rejects.toThrow('exactly one')
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })
})
