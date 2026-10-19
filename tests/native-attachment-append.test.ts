// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { pid } from 'node:process'

import { describe, expect, it, vi } from 'vitest'

import { NativeAttachmentCallbacks } from '../src/native-runner-attachments.js'
import { NativeStepCallbacks } from '../src/native-runner-steps.js'

const owner = { token: '0', scope: '1', generation: '1' }
const wire = (text: string) => Buffer.from(text, 'utf16le').toString('hex') || '-'
function fixture() {
  const sent: (string | number)[][] = [],
    fatal = vi.fn<(error: Error) => void>()
  const files = new NativeAttachmentCallbacks(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    fatal,
    () => undefined,
  )
  files.packet(['ATTACH_CONTEXT', '0', '1', '1', '1', '1', '1', String(pid)])
  return { files, sent, fatal }
}
describe('actual native attachment declaration wire', () => {
  it('retains callback bookkeeping after its step receipt until native file-context retirement', () => {
    const { files } = fixture(),
      released = vi.fn()
    files.run(
      owner,
      () => true,
      () => files.view(owner).push({ name: 'pending ref', contentType: 'x' }),
    )
    files.retireCallback(owner, released)
    expect(() => files.packet(['ATTACH_CONTEXT_RETIRED', '0', '1', '1'])).toThrow(
      'before actual declaration receipts',
    )
    expect(released).not.toHaveBeenCalled()
    files.packet(['ATTACH_REFERENCE_ACCEPTED', '1', '0', '1', '1', '17'])
    expect(released).not.toHaveBeenCalled()
    files.packet(['ATTACH_CONTEXT_RETIRED', '0', '1', '1'])
    expect(released).toHaveBeenCalledTimes(1)
  })
  it('waits for the real step callback receipt even if native file-context retirement arrived first', () => {
    const { files } = fixture(),
      released = vi.fn()
    files.packet(['ATTACH_CONTEXT_RETIRED', '0', '1', '1'])
    expect(released).not.toHaveBeenCalled()
    files.retireCallback(owner, released)
    expect(released).toHaveBeenCalledTimes(1)
  })

  it('enters the actual admitted step author scope when native admission arrives outside the outer async context', async () => {
    const { files, sent, fatal } = fixture(),
      view = files.view(owner)
    const steps = new NativeStepCallbacks(
      (...fields) => {
        sent.push(fields)
        return Promise.resolve()
      },
      fatal,
      () => undefined,
      undefined,
      undefined,
      undefined,
      (actualOwner, active, callback) => files.run(actualOwner, active, callback),
    )
    const result = files.run(
      owner,
      () => true,
      () =>
        steps.bind(owner, () => true)('actual admitted step', () =>
          view.push({ name: 'nested actual author', contentType: 'x', path: 'missing' }),
        ),
    )
    // The real host protocol ingress is outside the original run() scope.
    steps.handle(['STEP_ADMITTED', '1', '0', '1', '1', '19', '1', '-'])
    expect(view.length).toBe(1)
    files.packet(['ATTACH_REFERENCE_ACCEPTED', '1', '0', '1', '1', '17'])
    await vi.waitFor(() => expect(sent.some((fields) => fields[0] === 'STEP_SETTLED')).toBe(true))
    steps.handle(['STEP_RESULT', '19', '0', '1', '1', 'passed', '-'])
    steps.handle(['STEP_RECEIVED', '19', '0', '1', '1'])
    await expect(result).resolves.toBe(1)
    expect(fatal).not.toHaveBeenCalled()
  })

  it('sends synchronous path/absent declarations without copy, length, writer or fake commit', () => {
    const { files, sent } = fixture(),
      view = files.view(owner)
    files.run(
      owner,
      () => true,
      () =>
        expect(
          view.push(
            { name: 'ref', contentType: 'image/png', path: 'exact missing \ud800 path' },
            { name: '', contentType: '' },
          ),
        ).toBe(2),
    )
    expect(sent).toEqual([
      [
        'ATTACH_APPEND_BEGIN',
        '1',
        '0',
        '1',
        '1',
        wire('ref'),
        'R',
        0,
        wire('image/png'),
        wire('exact missing \ud800 path'),
      ],
      ['ATTACH_APPEND_BEGIN', '2', '0', '1', '1', '-', 'N', 0, '-', '-'],
    ])
    expect(files.physical).toBe(2)
    files.packet(['ATTACH_REFERENCE_ACCEPTED', '1', '0', '1', '1', '17'])
    files.packet(['ATTACH_REFERENCE_ACCEPTED', '2', '0', '1', '1', '18'])
    expect(files.physical).toBe(0)
    expect(Object.keys(view[0] ?? {}).sort()).toEqual(['contentType', 'name', 'path'])
    files.packet(['ATTACH_CONTEXT_RETIRED', '0', '1', '1'])
    expect(() =>
      files.run(
        owner,
        () => true,
        () => view.push({ name: 'late', contentType: 'x' }),
      ),
    ).toThrow('stale or retired')
  })
  it('keeps body append pending until native byte/commit publication and exact terminal', () => {
    const { files, sent, fatal } = fixture(),
      view = files.view(owner),
      data = Buffer.from([0, 255])
    files.run(
      owner,
      () => true,
      () => expect(view.push({ name: 'body', contentType: 'x', body: data })).toBe(1),
    )
    data.fill(17)
    files.packet(['ATTACH_ADMITTED', '1', '0', '1', '1', '7', '1', '-'])
    expect(sent[1]).toEqual(['ATTACH_CHUNK', '7', '0', '1', '1', 1, '00ff'])
    files.packet(['ATTACH_CHUNK_OK', '1', '0', '1', '1', '7', '1'])
    expect(() => files.packet(['ATTACH_RESULT', '1', '0', '1', '1', '7', '1', '-'])).toThrow(
      'no committed publication',
    )
    expect(files.physical).toBe(1)
    expect(() =>
      files.packet([
        'ATTACH_PUBLISHED',
        '1',
        '0',
        '1',
        '1',
        '8',
        wire('body'),
        wire('x'),
        wire('/actual/native/body.dat'),
        '2',
      ]),
    ).toThrow('file/byte admission')
    files.packet([
      'ATTACH_PUBLISHED',
      '1',
      '0',
      '1',
      '1',
      '7',
      wire('body'),
      wire('x'),
      wire('/actual/native/body.dat'),
      '2',
    ])
    expect(files.physical).toBe(1)
    files.packet(['ATTACH_RESULT', '1', '0', '1', '1', '7', '1', '-'])
    expect(files.physical).toBe(0)
    expect(fatal).not.toHaveBeenCalled()
  })
  it('observes only its own unawaitable append error while preserving the native failed declaration', async () => {
    const { files, fatal } = fixture(),
      view = files.view(owner)
    files.run(
      owner,
      () => true,
      () => view.push({ name: 'rejected body', contentType: 'x', body: Buffer.from('real') }),
    )
    files.packet([
      'ATTACH_APPEND_REJECTED',
      '1',
      '0',
      '1',
      '1',
      '19',
      wire('actual native capacity rejection'),
    ])
    await Promise.resolve()
    expect(files.physical).toBe(0)
    expect(view[0]?.name).toBe('rejected body')
    expect(Object.keys(view[0] ?? {}).sort()).toEqual(['body', 'contentType', 'name'])
    expect(fatal).not.toHaveBeenCalled()
    // This controlled wire test makes no claim about native outcome policy;
    // C++ ledger/fence controls and real-process proofs cover that boundary.
    files.packet(['ATTACH_CONTEXT_RETIRED', '0', '1', '1'])
  })
  it('keeps the native artifact-count rejection asynchronous after 128 committed ordinary files', async () => {
    const { files } = fixture()
    for (let n = 1; n <= 128; ++n) {
      const request = String(n),
        attached = files.bind(owner, () => true)('ordinary', { body: Buffer.alloc(0) })
      files.packet(['ATTACH_ADMITTED', request, '0', '1', '1', request, '1', '-'])
      files.packet([
        'ATTACH_PUBLISHED',
        request,
        '0',
        '1',
        '1',
        request,
        wire('ordinary'),
        wire('application/octet-stream'),
        wire('/actual/native/' + request),
        '0',
      ])
      files.packet(['ATTACH_RESULT', request, '0', '1', '1', request, '1', '-'])
      await attached
    }
    const rejected = files.bind(owner, () => true)('native over-count', { body: Buffer.alloc(0) })
    const observed = expect(rejected).rejects.toThrow('actual native capacity rejection')
    files.packet([
      'ATTACH_ADMITTED',
      '129',
      '0',
      '1',
      '1',
      '0',
      '0',
      wire('actual native capacity rejection'),
    ])
    await observed
    expect(files.view(owner).length).toBe(128)
    expect(files.physical).toBe(0)
  })
  it('shows ordinary attach only after actual copied-path publication in the same view', async () => {
    const { files } = fixture(),
      view = files.view(owner)
    const attached = files.bind(owner, () => true)('copy', { path: 'author/source.bin' })
    expect(view.length).toBe(0)
    files.packet(['ATTACH_ADMITTED', '1', '0', '1', '1', '7', '1', '-'])
    files.packet([
      'ATTACH_PUBLISHED',
      '1',
      '0',
      '1',
      '1',
      '7',
      wire('copy'),
      wire('application/octet-stream'),
      wire('/actual/native/copy.dat'),
      '2',
    ])
    expect(view[0]).toEqual({
      name: 'copy',
      contentType: 'application/octet-stream',
      path: '/actual/native/copy.dat',
    })
    files.packet(['ATTACH_RESULT', '1', '0', '1', '1', '7', '1', '-'])
    await attached
    files.run(
      owner,
      () => true,
      () =>
        expect(view.push({ name: 'author reference', contentType: 'x', path: 'missing' })).toBe(2),
    )
    files.packet(['ATTACH_REFERENCE_ACCEPTED', '2', '0', '1', '1', '18'])
    expect([...view].map((item) => item.path)).toEqual(['/actual/native/copy.dat', 'missing'])
  })
})
