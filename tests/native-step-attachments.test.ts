// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { pid } from 'node:process'

import { describe, expect, it, vi } from 'vitest'

import { type NativeTestAttach } from '../src/native-runner-attachment-types.js'
import { NativeAttachmentCallbacks } from '../src/native-runner-attachments.js'
import { NativeStepCallbacks, type NativeStepOwner } from '../src/native-runner-steps.js'

const owner = { token: '0', scope: '1', generation: '1' }
describe('native step attachment bridge', () => {
  it('binds the actual native admitted step and verifies its receipt identity', async () => {
    const sent: (string | number)[][] = []
    const fatal = vi.fn<(error: Error) => void>()
    const files = new NativeAttachmentCallbacks(
      (...fields) => {
        sent.push(fields)
        return Promise.resolve()
      },
      fatal,
      () => undefined,
    )
    files.packet(['ATTACH_CONTEXT', '0', '1', '1', '1', '1', '1', String(pid)])
    const bytes = new Uint8Array([0, 255])
    const file = files.bindStep(owner, '19', () => true)('actual', { body: bytes })
    bytes[0] = 17
    expect(sent[0]?.slice(0, 6)).toEqual(['ATTACH_STEP_BEGIN', '1', '0', '1', '1', '19'])
    expect(() =>
      files.packet(['ATTACH_STEP_ADMITTED', '1', '0', '1', '1', '7', '20', '1', '-']),
    ).toThrow('step identity')
    expect(files.physical).toBe(1)
    files.packet(['ATTACH_STEP_ADMITTED', '1', '0', '1', '1', '7', '19', '1', '-'])
    expect(sent[1]).toEqual(['ATTACH_CHUNK', '7', '0', '1', '1', 1, '00ff'])
    files.packet(['ATTACH_STEP_CHUNK_OK', '1', '0', '1', '1', '7', '19', '1'])
    files.packet([
      'ATTACH_PUBLISHED',
      '1',
      '0',
      '1',
      '1',
      '7',
      '19',
      Buffer.from('actual', 'utf16le').toString('hex'),
      Buffer.from('application/octet-stream', 'utf16le').toString('hex'),
      Buffer.from('/controlled/native/artifact.dat', 'utf16le').toString('hex'),
      '2',
    ])
    files.packet(['ATTACH_STEP_RESULT', '1', '0', '1', '1', '7', '19', '1', '-'])
    await expect(file).resolves.toBeUndefined()
    expect(files.physical).toBe(0)
    expect(fatal).not.toHaveBeenCalled()
  })

  it('passes native admitted identity to the factory and preserves actual callback value', async () => {
    const sent: (string | number)[][] = []
    const attach = vi.fn<NativeTestAttach>().mockResolvedValue(undefined)
    const factory = vi
      .fn<(owner: NativeStepOwner, id: string, active: () => boolean) => NativeTestAttach>()
      .mockReturnValue(attach)
    const steps = new NativeStepCallbacks(
      (...fields) => {
        sent.push(fields)
        return Promise.resolve()
      },
      () => undefined,
      () => undefined,
      undefined,
      factory,
    )
    const result = steps.bind(owner, () => true)('actual', async (info) => {
      await info.attach('real', { body: 'data' })
      return 17
    })
    steps.handle(['STEP_ADMITTED', '1', '0', '1', '1', '19', '1', '-'])
    await vi.waitFor(() => expect(sent.some((fields) => fields[0] === 'STEP_SETTLED')).toBe(true))
    expect(factory).toHaveBeenCalledWith(owner, '19', expect.any(Function))
    expect(attach).toHaveBeenCalledWith('real', { body: 'data' })
    steps.handle(['STEP_RESULT', '19', '0', '1', '1', 'passed', '-'])
    steps.handle(['STEP_RECEIVED', '19', '0', '1', '1'])
    await expect(result).resolves.toBe(17)
  })

  it('does not replace a genuine native file failure with undefined after raw callback success', async () => {
    const sent: (string | number)[][] = []
    const steps = new NativeStepCallbacks(
      (...fields) => {
        sent.push(fields)
        return Promise.resolve()
      },
      () => undefined,
      () => undefined,
    )
    const result = steps.bind(owner, () => true)('raw success then native failure', () => 17)
    steps.handle(['STEP_ADMITTED', '1', '0', '1', '1', '19', '1', '-'])
    await vi.waitFor(() => expect(sent.some((fields) => fields[0] === 'STEP_SETTLED')).toBe(true))
    const rejected = expect(result).rejects.toThrow('actual file error')
    steps.handle([
      'STEP_RESULT',
      '19',
      '0',
      '1',
      '1',
      'failed',
      '610063007400750061006c002000660069006c00650020006500720072006f007200',
    ])
    steps.handle(['STEP_RECEIVED', '19', '0', '1', '1'])
    await rejected
  })
})
