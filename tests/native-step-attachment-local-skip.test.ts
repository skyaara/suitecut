// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { pid } from 'node:process'

import { describe, expect, it, vi } from 'vitest'

import { NativeAttachmentCallbacks } from '../src/native-runner-attachments.js'
import { NativeStepCallbacks } from '../src/native-runner-steps.js'

const owner = { token: '0', scope: '1', generation: '1' }
function fixture() {
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
  return { sent, fatal, files }
}

describe('authenticated local step skip attachment receipts', () => {
  it('retains files through native ACK and exact physical terminal, leaving sibling and outer files alone', async () => {
    const { files, fatal, sent } = fixture()
    const canceled = files.bindStep(owner, '19', () => true)('source', { body: 'source' })
    const sibling = files.bindStep(owner, '20', () => true)('sibling', { body: '' })
    const outer = files.bind(owner, () => true)('outer', { body: '' })
    const control = new Error('actual accepted local control')
    files.acceptLocalSkip(owner, '8', '18', ['18', '19'], control)
    files.packet(['ATTACH_STEP_ADMITTED', '1', '0', '1', '1', '7', '19', '1', '-'])
    expect(sent).toHaveLength(3)
    expect(files.physical).toBe(3)
    files.packet(['ATTACH_STEP_LOCAL_SKIP_RESULT', '1', '0', '1', '1', '7', '19', '8', '18', '-'])
    await expect(canceled).rejects.toBe(control)
    expect(files.physical).toBe(2)
    files.packet(['ATTACH_STEP_ADMITTED', '2', '0', '1', '1', '9', '20', '1', '-'])
    files.packet([
      'ATTACH_PUBLISHED',
      '2',
      '0',
      '1',
      '1',
      '9',
      '20',
      Buffer.from('sibling', 'utf16le').toString('hex'),
      Buffer.from('text/plain', 'utf16le').toString('hex'),
      Buffer.from('/controlled/native/sibling.txt', 'utf16le').toString('hex'),
      '0',
    ])
    files.packet(['ATTACH_STEP_RESULT', '2', '0', '1', '1', '9', '20', '1', '-'])
    files.packet(['ATTACH_ADMITTED', '3', '0', '1', '1', '10', '1', '-'])
    files.packet([
      'ATTACH_PUBLISHED',
      '3',
      '0',
      '1',
      '1',
      '10',
      Buffer.from('outer', 'utf16le').toString('hex'),
      Buffer.from('text/plain', 'utf16le').toString('hex'),
      Buffer.from('/controlled/native/outer.txt', 'utf16le').toString('hex'),
      '0',
    ])
    files.packet(['ATTACH_RESULT', '3', '0', '1', '1', '10', '1', '-'])
    await Promise.all([sibling, outer])
    expect(files.physical).toBe(0)
    expect(fatal).not.toHaveBeenCalled()
  })

  it('rejects wrong native local cause/source without retiring the file', async () => {
    const { files, fatal } = fixture()
    const file = files.bindStep(owner, '19', () => true)('source')
    const control = new Error('accepted local cause')
    files.acceptLocalSkip(owner, '8', '19', ['19'], control)
    expect(() =>
      files.packet([
        'ATTACH_STEP_LOCAL_SKIP_RESULT',
        '1',
        '0',
        '1',
        '1',
        '7',
        '19',
        '9',
        '19',
        '-',
      ]),
    ).toThrow('accepted mutation')
    expect(() =>
      files.packet([
        'ATTACH_STEP_LOCAL_SKIP_RESULT',
        '1',
        '0',
        '1',
        '1',
        '7',
        '19',
        '8',
        '20',
        '-',
      ]),
    ).toThrow('accepted mutation')
    expect(files.physical).toBe(1)
    files.packet(['ATTACH_STEP_LOCAL_SKIP_RESULT', '1', '0', '1', '1', '7', '19', '8', '19', '-'])
    await expect(file).rejects.toBe(control)
    expect(fatal).not.toHaveBeenCalled()
  })

  it('cannot silently consume a genuine native error after both accepted causes', async () => {
    const { files, fatal } = fixture()
    const file = files.bindStep(owner, '19', () => true)('source')
    files.acceptLocalSkip(owner, '8', '19', ['19'], new Error('accepted local cause'))
    files.acceptTestSkip(owner, '11', new Error('later accepted test cause'))
    files.packet([
      'ATTACH_STEP_RESULT',
      '1',
      '0',
      '1',
      '1',
      '7',
      '19',
      '0',
      Buffer.from('actual rollback error', 'utf16le').toString('hex'),
    ])
    await expect(file).rejects.toThrow('actual rollback error')
    expect(fatal).toHaveBeenCalledTimes(1)
    expect(fatal.mock.calls[0]?.[0].message).toBe('actual rollback error')
  })

  it('preserves both accepted causes and lets the genuine later test control retire the file', async () => {
    const { files, fatal } = fixture()
    const file = files.bindStep(owner, '19', () => true)('source')
    files.acceptLocalSkip(owner, '8', '19', ['19'], new Error('earlier native local control'))
    const control = new Error('later accepted native test control')
    files.acceptTestSkip(owner, '11', control)
    files.packet(['ATTACH_STEP_TEST_SKIP_RESULT', '1', '0', '1', '1', '7', '19', '11', '-'])
    await expect(file).rejects.toBe(control)
    expect(files.physical).toBe(0)
    expect(fatal).not.toHaveBeenCalled()
  })

  it('requires the native source in the exact bounded outcome step list', () => {
    const { files } = fixture()
    expect(() => files.acceptLocalSkip(owner, '8', '19', ['20'], new Error('control'))).toThrow(
      'actual step IDs',
    )
    expect(() =>
      files.acceptLocalSkip(owner, '8', '19', ['19', '19'], new Error('control')),
    ).toThrow('actual step IDs')
  })

  it('supplies native outcome step IDs to the bridge only after authentic local ACK', async () => {
    const sent: (string | number)[][] = []
    const accepted =
      vi.fn<(...args: Parameters<NativeAttachmentCallbacks['acceptLocalSkip']>) => void>()
    const fatal = vi.fn<(error: Error) => void>()
    const steps = new NativeStepCallbacks(
      (...fields) => {
        sent.push(fields)
        return Promise.resolve()
      },
      fatal,
      () => undefined,
      undefined,
      undefined,
      accepted,
    )
    const result = steps.bind(owner, () => true)('source', (info) => info.skip('actual'))
    steps.handle(['STEP_ADMITTED', '1', '0', '1', '1', '19', '1', '-'])
    await vi.waitFor(() => expect(sent.some((p) => p[0] === 'STEP_LOCAL_SKIP_REQUEST')).toBe(true))
    steps.handle(['STEP_LOCAL_SKIP_ABORT', '19', '0', '1', '1', '1', '19'])
    steps.handle(['STEP_LOCAL_SKIP_RESULT', '19', '0', '1', '1', '1', '19'])
    expect(accepted).not.toHaveBeenCalled()
    steps.handle(['STEP_LOCAL_SKIP_ACK', '1', '19', '0', '1', '1', '1', '-'])
    expect(accepted).toHaveBeenCalledWith(owner, '1', '19', ['19'], expect.any(Error))
    await vi.waitFor(() => expect(sent.some((p) => p[0] === 'STEP_SETTLED_LOCAL_SKIP')).toBe(true))
    steps.handle(['STEP_RECEIVED', '19', '0', '1', '1'])
    await expect(result).resolves.toBeUndefined()
    expect(fatal).not.toHaveBeenCalled()
  })
})
