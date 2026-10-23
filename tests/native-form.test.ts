// SPDX-License-Identifier: MIT
import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { describe, expect, it } from 'vitest'

import { NativeFormActions } from '../src/native-form.js'
import { type UntrustedInput } from '../src/untrusted.js'

describe('native form argument boundary', () => {
  it('rejects traversing memory upload names and oversized packets before dispatch', async () => {
    let calls = 0
    const actions = new NativeFormActions(() => {
      ++calls
      return Promise.resolve(null)
    })
    await expect(
      actions.setInputFiles({
        name: '../escape',
        mimeType: 'text/plain',
        buffer: Buffer.from('x'),
      }),
    ).rejects.toThrow()
    await expect(
      actions.setInputFiles({
        name: 'large.bin',
        mimeType: '',
        buffer: Buffer.alloc(128 * 1024 * 1024 + 1),
      }),
    ).rejects.toThrow('128 MiB')
    expect(calls).toBe(0)
  })

  it('validates all paths before an upload starts and keeps binary bytes intact', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-form-sdk-'))
    const calls: Record<string, UntrustedInput>[] = []
    const actions = new NativeFormActions((params) => {
      calls.push(params)
      return Promise.resolve(params.action === 'uploadBegin' ? 'test-upload-token' : null)
    })
    try {
      const path = join(directory, 'file.txt')
      await writeFile(path, 'path data')
      await expect(actions.setInputFiles([path, join(directory, 'missing')])).rejects.toThrow()
      expect(calls).toHaveLength(0)
      await actions.setInputFiles({
        name: 'binary.bin',
        mimeType: 'application/octet-stream',
        buffer: Buffer.from([0, 255, 128, 13, 10]),
      })
      expect(calls[0]?.files).toEqual([
        { name: 'binary.bin', mimeType: 'application/octet-stream', size: 5 },
      ])
      expect(calls[1]).toMatchObject({
        action: 'uploadAppend',
        uploadToken: 'test-upload-token',
        fileIndex: 0,
        base64: 'AP+ADQo=',
      })
      expect(calls[2]).toMatchObject({ action: 'setInputFiles', uploadToken: 'test-upload-token' })
      await actions.setInputFiles(directory)
      expect(calls[3]?.files).toEqual([{ path: directory, directory: true }])
      await expect(actions.setInputFiles([directory, path])).rejects.toThrow(
        'all files or a single directory',
      )
      expect(calls).toHaveLength(4)
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('chunks buffers below the native framer bound and aborts staging after an append error', async () => {
    const calls: Record<string, UntrustedInput>[] = []
    const actions = new NativeFormActions((params) => {
      calls.push(params)
      if (params.action === 'uploadBegin') return Promise.resolve('token')
      if (params.action === 'uploadAppend' && calls.length === 3)
        return Promise.reject(new Error('native disk full'))
      return Promise.resolve(null)
    })
    await expect(
      actions.setInputFiles({
        name: 'chunked.bin',
        mimeType: '',
        buffer: Buffer.alloc(100_000, 128),
      }),
    ).rejects.toThrow('native disk full')
    expect(calls.at(-1)).toMatchObject({ action: 'uploadAbort', uploadToken: 'token' })
    expect(calls.every((call) => Buffer.byteLength(JSON.stringify(call)) < 40_000)).toBe(true)
  })

  it('rejects unsupported and ambiguous form arguments without browser mutations', async () => {
    let calls = 0
    const actions = new NativeFormActions(() => {
      ++calls
      return Promise.resolve([])
    })
    await expect(actions.selectOption({})).rejects.toThrow('requires')
    await expect(actions.selectOption({ index: -1 })).rejects.toThrow()
    await expect(actions.setChecked(true, { timeout: Number.NaN })).rejects.toThrow()
    expect(calls).toBe(0)
    await expect(actions.selectOption(null)).resolves.toEqual([])
    expect(calls).toBe(1)
  })
})
