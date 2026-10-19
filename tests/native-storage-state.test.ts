import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { describe, expect, it, vi } from 'vitest'

import {
  captureNativeStorageState,
  readNativeStorageState,
  NativeStorageStateSchema,
  restoreNativeStorageState,
  type NativeStorageState,
  type NativeStorageTransport,
} from '../src/native-storage-state.js'

const state: NativeStorageState = {
  cookies: [],
  origins: [
    {
      origin: 'https://storage.example.test',
      localStorage: [{ name: 'state', value: '🧪'.repeat(20000) }],
    },
  ],
}

describe('bounded native storage-state transport', () => {
  it('normalizes omitted input collections without loosening native captured-state validation', async () => {
    await expect(readNativeStorageState({})).resolves.toEqual({ cookies: [], origins: [] })
    await expect(readNativeStorageState({ origins: state.origins })).resolves.toEqual(state)
    expect(() => NativeStorageStateSchema.parse({})).toThrow()
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-empty-storage-'))
    try {
      const file = join(directory, 'empty.json')
      await writeFile(file, '{}')
      await expect(readNativeStorageState(file)).resolves.toEqual({ cookies: [], origins: [] })
      await writeFile(file, '{"cookies":null}')
      await expect(readNativeStorageState(file)).rejects.toThrow()
      await writeFile(file, '{"unexpected":true}')
      await expect(readNativeStorageState(file)).rejects.toThrow()
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })
  it('round-trips pretty authentication files while enforcing the compact state budget', async () => {
    const large: NativeStorageState = {
      cookies: [],
      origins: Array.from({ length: 1024 }, (_, index) => ({
        origin: `https://storage-${index}.example.test`,
        localStorage: [{ name: 'state', value: 'x'.repeat(8040) }],
      })),
    }
    expect(Buffer.byteLength(JSON.stringify(large))).toBeLessThan(8 * 1024 * 1024)
    expect(Buffer.byteLength(JSON.stringify(large, null, 2))).toBeGreaterThan(8 * 1024 * 1024)
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-storage-state-'))
    try {
      const path = join(directory, 'authentication.json')
      const transport: NativeStorageTransport = { runContext: vi.fn().mockResolvedValue(large) }
      await captureNativeStorageState(transport, 2, { path })
      expect((await readFile(path)).toString('utf8')).toBe(JSON.stringify(large, null, 2))
      expect(await readNativeStorageState(path)).toEqual(large)
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('preserves Unicode across bounded command envelopes and discards failed native uploads', async () => {
    const chunks: string[] = []
    const runContext = vi.fn<NativeStorageTransport['runContext']>((action, params) => {
      if (action === 'storageUploadBegin') return Promise.resolve({ uploadId: '1' })
      if (action === 'storageUploadAppend') {
        expect(
          Buffer.byteLength(JSON.stringify({ command: 'context', action, params })),
        ).toBeLessThan(65536)
        const data = params?.data
        if (typeof data !== 'string') throw new Error('Expected a native upload string')
        chunks.push(data)
      }
      return Promise.resolve(undefined)
    })
    await restoreNativeStorageState({ runContext }, 2, state)
    expect(JSON.parse(chunks.join(''))).toEqual(state)
    expect(chunks.length).toBeGreaterThan(1)
    expect(runContext.mock.calls.at(-1)?.[0]).toBe('storageUploadDiscard')
    runContext.mockClear()
    runContext.mockImplementation((action) => {
      if (action === 'storageUploadBegin') return Promise.resolve({ uploadId: '2' })
      if (action === 'storageUploadAppend')
        return Promise.reject(new Error('Native upload admission failed'))
      return Promise.resolve(undefined)
    })
    await expect(restoreNativeStorageState({ runContext }, 2, state)).rejects.toThrow(
      'Native upload admission failed',
    )
    expect(runContext.mock.calls.map(([action]) => action)).toEqual([
      'storageUploadBegin',
      'storageUploadAppend',
      'storageUploadDiscard',
    ])
  })

  it('rejects oversized file input before parsing its content', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-storage-state-'))
    try {
      const path = join(directory, 'oversized.json')
      await writeFile(path, ' '.repeat(32 * 1024 * 1024 + 1))
      await expect(readNativeStorageState(path)).rejects.toThrow('exceeds 32 MiB')
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })
  it('preserves isolated UTF-16 units through private native JSON and public files', async () => {
    const origin = 'https://storage.example.test'
    const entry = {
      origin,
      localStorage: [{ name: String.fromCharCode(0xd800), value: String.fromCharCode(0xda7e) }],
    }
    const actual: NativeStorageState = { cookies: [], origins: [entry] }
    const chunks: string[] = []
    const runContext = vi.fn<NativeStorageTransport['runContext']>((action, params) => {
      if (action === 'storageState')
        return Promise.resolve({
          cookies: [],
          origins: [{ origin, stateJSON: JSON.stringify(entry), empty: false }],
        })
      if (action === 'storageUploadBegin') return Promise.resolve({ uploadId: '1' })
      if (action === 'storageUploadAppend') {
        const data = params?.data
        if (typeof data !== 'string') throw new Error('Expected a native upload string')
        chunks.push(data)
      }
      return Promise.resolve(undefined)
    })
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-storage-state-'))
    try {
      const path = join(directory, 'isolated-units.json')
      expect(await captureNativeStorageState({ runContext }, 2, { path })).toEqual(actual)
      expect(await readFile(path, 'utf8')).toBe(JSON.stringify(actual, null, 2))
      expect(await readNativeStorageState(path)).toEqual(actual)
      await restoreNativeStorageState({ runContext }, 2, path)
      expect(JSON.parse(chunks.join(''))).toEqual({
        cookies: [],
        origins: [{ origin, stateJSON: JSON.stringify(entry) }],
      })
      expect(chunks.join('')).not.toMatch(/[\ud800-\udfff]/)
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('reports file paths and underlying filesystem/JSON errors', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-storage-state-'))
    try {
      const missing = join(directory, 'missing.json')
      await expect(readNativeStorageState(missing)).rejects.toThrow(
        `Error reading storage state from ${missing}:\nENOENT`,
      )
      const malformed = join(directory, 'malformed.json')
      await writeFile(malformed, 'not-json')
      await expect(readNativeStorageState(malformed)).rejects.toThrow(
        `Error reading storage state from ${malformed}:\nUnexpected token`,
      )
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('includes the invalid origin and rejects a native wire origin mismatch', async () => {
    await expect(
      readNativeStorageState({ cookies: [], origins: [{ origin: 'foo', localStorage: [] }] }),
    ).rejects.toThrow('foo')
    const transport: NativeStorageTransport = {
      runContext: vi.fn().mockResolvedValue({
        cookies: [],
        origins: [
          {
            origin: 'https://wrong.test',
            stateJSON: JSON.stringify(state.origins[0]),
            empty: false,
          },
        ],
      }),
    }
    await expect(captureNativeStorageState(transport, 2)).rejects.toThrow(
      'Native storage wire origin mismatch',
    )
  })
})
