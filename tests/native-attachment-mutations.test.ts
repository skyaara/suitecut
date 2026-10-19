// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { pid } from 'node:process'

import { describe, expect, it, vi } from 'vitest'

import {
  NativeAttachmentViews,
  type NativeTestAttachment,
} from '../src/native-runner-attachment-view.js'
import { NativeAttachmentCallbacks } from '../src/native-runner-attachments.js'
import { type NativeStepOwner } from '../src/native-runner-steps.js'

const owner: NativeStepOwner = { token: '0', scope: '1', generation: '1' }
const context = { ...owner, testId: '1', attemptGeneration: '1', workerIndex: 1, pid: 101 }
const wire = (text: string): string => Buffer.from(text, 'utf16le').toString('hex') || '-'
function staging() {
  let sequence = 0
  const declarations: (readonly NativeTestAttachment[])[] = []
  const proposals: {
    expected: string
    slots: readonly (string | null)[]
    publication?: { ticket: string; artifact: string }
  }[] = []
  const views = new NativeAttachmentViews(
    1,
    101,
    (_actual, items) => {
      declarations.push(items)
      return items.map(() => String(++sequence))
    },
    (_actual, expected, slots, publication) => {
      proposals.push({
        expected,
        slots: [...slots],
        ...(publication === undefined ? {} : { publication }),
      })
      return String(++sequence)
    },
  )
  views.open(context)
  return { views, declarations, proposals, view: views.view(owner) }
}

describe('mutable attachment author staging (controlled sender, no native/file receipt claim)', () => {
  it('stages replacement, splice, holes and order without withdrawing old request entries', () => {
    const { views, view, declarations, proposals } = staging()
    views.run(
      owner,
      () => true,
      () => {
        expect(
          view.push(
            { name: 'body', contentType: 'x', body: Buffer.from('old') },
            { name: 'ref', contentType: 'x', path: 'missing\ud800' },
          ),
        ).toBe(2)
        const first = view[0],
          removed = view.splice(1, 1, { name: 'replacement', contentType: 'x' })
        expect(removed[0]?.path).toBe('missing\ud800')
        view[2] = { name: 'index', contentType: 'x' }
        Reflect.deleteProperty(view, '1')
        expect(1 in view).toBe(false)
        expect(view.reverse()).toBe(view)
        expect(view).toHaveLength(3)
        expect(view[0]?.name).toBe('index')
        expect(1 in view).toBe(false)
        expect(view[2]?.name).toBe('body')
        expect(first).toBeDefined()
        expect(view.copyWithin(1, 2)).toBe(view)
        expect(view[1]).toBe(first)
        expect(view[2]).toBe(first)
        view.length = 0
      },
    )
    expect(view.length).toBe(0)
    expect(declarations.flat().map((item) => item.name)).toEqual([
      'body',
      'ref',
      'replacement',
      'index',
    ])
    expect(proposals[0]).toEqual({ expected: '0', slots: [] })
    expect(proposals.at(-1)?.slots).toEqual([])
    // Removal is not a terminal native body receipt or callback retirement.
    expect(() => views.retire(owner)).toThrow('before actual declaration receipts')
  })
  it('reuses admitted references for fill/copy/reorder and preserves immutable writer bytes', () => {
    const { views, view, declarations } = staging(),
      source = Buffer.from('truth')
    views.run(
      owner,
      () => true,
      () => {
        view.push({ name: 'original', contentType: 'x', body: source })
        const original = view[0]
        if (!original) throw new Error('Missing actual staged item')
        view.length = 3
        expect(view.fill(original)).toBe(view)
        view.reverse()
        expect(view.shift()).toBe(original)
        expect(view.pop()).toBe(original)
      },
    )
    source.fill(0)
    view[0]?.body?.fill(1)
    expect(declarations.flat()).toHaveLength(1)
    expect(Buffer.from(declarations[0]?.[0]?.body ?? []).toString()).toBe('truth')
  })
  it('preserves author entry identity and edits while keeping native insertion snapshots immutable', () => {
    const { views, view, declarations, proposals } = staging()
    const bytes = Buffer.from('original'),
      held: NativeTestAttachment = {
        name: 'original',
        contentType: 'x',
        path: 'exact original',
      }
    views.run(
      owner,
      () => true,
      () => {
        view.push(held)
        const inserted = view[0]
        expect(inserted).toBe(held)
        const count = proposals.length
        held.name = 'later author input'
        held.contentType = 'later type'
        held.path = 'later path'
        held.body = bytes
        expect(inserted?.body).toBe(bytes)
        expect(proposals).toHaveLength(count)
        view.unshift(held)
        expect(view[0]).toBe(held)
        expect(view[1]).toBe(held)
        expect(proposals).toHaveLength(count + 1)
      },
    )
    expect(declarations.flat()).toHaveLength(1)
    expect(declarations[0]?.[0]).toEqual({
      name: 'original',
      contentType: 'x',
      path: 'exact original',
    })
  })
  it('admits every explicit push occurrence and preserves older same-object snapshot references', () => {
    const { views, view, declarations, proposals } = staging()
    const bytes = Buffer.from([1, 2]),
      held = { name: 'initial', contentType: 'x', body: bytes }
    views.run(
      owner,
      () => true,
      () => {
        view.push(held, held)
        expect(view[0]).toBe(held)
        expect(view[1]).toBe(held)
        expect(view[0]?.body).toBe(bytes)
        held.name = 'edited push'
        bytes[0] = 9
        view.push(held)
        expect(view[2]).toBe(held)
        const last = proposals.at(-1)?.slots
        expect(last).toHaveLength(3)
        expect(new Set(last).size).toBe(3)
        const admissionCount = declarations.flat().length
        view.unshift(held)
        view.splice(1, 0, held)
        view[5] = held
        view.reverse()
        expect(view.every((item) => item === held)).toBe(true)
        expect(declarations.flat()).toHaveLength(admissionCount)
      },
    )
    expect(
      declarations.flat().map((item) => [item.name, Buffer.from(item.body ?? []).toString('hex')]),
    ).toEqual([
      ['initial', '0102'],
      ['initial', '0102'],
      ['edited push', '0902'],
    ])
  })
  it('transports direct-push body plus authored path metadata with an explicit presence discriminant', () => {
    const sent: (string | number)[][] = [],
      files = new NativeAttachmentCallbacks(
        (...fields) => {
          sent.push(fields)
          return Promise.resolve()
        },
        () => undefined,
        () => undefined,
        { workerIndex: 1, pid },
        () => '0',
      )
    files.packet(['ATTACH_CONTEXT', '0', '1', '1', '1', '1', '1', String(pid)])
    const bytes = Buffer.from([0, 128, 255]),
      held = { name: 'both', contentType: 'x', body: bytes, path: 'authored \ud800 path' },
      view = files.view(owner)
    files.run(
      owner,
      () => true,
      () =>
        view.push(
          held,
          { name: 'empty path', contentType: 'x', body: Buffer.alloc(0), path: '' },
          { name: 'absent path', contentType: 'x', body: Buffer.alloc(0) },
        ),
    )
    expect(view[0]).toBe(held)
    expect(view[0]?.body).toBe(bytes)
    const begins = sent.filter((row) => row[0] === 'ATTACH_APPEND_BEGIN')
    expect(begins.map((row) => [row[6], row[7], row[9]])).toEqual([
      ['D', 3, wire(held.path)],
      ['D', 0, '-'],
      ['B', 0, '-'],
    ])
    expect(() =>
      files.bind(owner, () => true)('ordinary', { body: bytes, path: held.path }),
    ).toThrow('Exactly one')
  })
  it('keeps ordinary publication metadata private while exposing mutable author values', () => {
    const { views, view, declarations } = staging(),
      bytes = Buffer.from('truth')
    views.prepareNormal(owner)
    views.trackNormal(owner, '27', { name: 'ordinary', contentType: 'x', body: bytes })
    views.published(
      owner,
      '27',
      { id: '91', name: 'ordinary', contentType: 'x', path: '/native/real.txt', bytes: 5 },
      '17',
    )
    const entry = view[0]
    if (!entry) throw new Error('Missing actual ordinary publication')
    expect(Object.isFrozen(entry)).toBe(false)
    expect(entry.body).not.toBe(bytes)
    entry.name = 'edited ordinary'
    entry.contentType = 'edited/type'
    entry.body?.fill(9)
    expect(bytes.toString()).toBe('truth')
    expect(views.committed(owner, '27')).toBe(true)
    views.run(
      owner,
      () => true,
      () => view.push(entry),
    )
    expect(view[0]).toBe(entry)
    expect(view[1]).toBe(entry)
    expect(declarations[0]?.[0]?.name).toBe('edited ordinary')
    expect(Buffer.from(declarations[0]?.[0]?.body ?? []).toString('hex')).toBe('0909090909')
  })
  it('retains the actual async author scope and rejects mutation after that callback closes', async () => {
    const { views, view, proposals } = staging()
    let active = true
    await views.run(
      owner,
      () => active,
      async () => {
        await Promise.resolve()
        view.push({ name: 'async declaration', contentType: 'x' })
        const inserted = view[0]
        if (!inserted) throw new Error('Missing actual async declaration')
        await Promise.resolve()
        view.splice(0, 1, inserted, inserted)
        expect(view[0]).toBe(inserted)
        expect(view[1]).toBe(inserted)
        active = false
        expect(() => view.pop()).toThrow('active author callback')
      },
    )
    expect(proposals).toHaveLength(3)
  })
  it('validates complete inserted batches and preserves genuine author comparator errors', () => {
    const { views, view, declarations, proposals } = staging()
    views.run(
      owner,
      () => true,
      () => {
        expect(() =>
          view.splice(
            0,
            0,
            { name: 'ok', contentType: 'x' },
            // @ts-expect-error actual runtime declaration boundary
            { name: 'invalid', contentType: 17 },
          ),
        ).toThrow()
        expect(declarations).toEqual([])
        expect(proposals).toEqual([])
        view.push({ name: 'b', contentType: 'x' }, { name: 'a', contentType: 'x' })
        const count = proposals.length,
          cause = new Error('actual author comparator')
        try {
          view.sort(() => {
            throw cause
          })
        } catch (error) {
          expect(error).toBe(cause)
        }
        expect(proposals).toHaveLength(count)
        expect(view.map((item) => item.name)).toEqual(['b', 'a'])
        expect(() => {
          view.length = 257
        }).toThrow('slot capacity')
      },
    )
  })
  it('rejects stale author scopes and foreign attempt views before proposals', () => {
    const { views, view, proposals } = staging()
    expect(() => view.pop()).toThrow('active author callback')
    views.run(
      owner,
      () => false,
      () =>
        expect(() => {
          view.length = 1
        }).toThrow('active author callback'),
    )
    const retry = { ...owner, generation: '2' }
    views.open({ ...context, ...retry, attemptGeneration: '2' })
    views.run(
      retry,
      () => true,
      () =>
        expect(() => view.push({ name: 'foreign', contentType: 'x' })).toThrow(
          'another actual attempt',
        ),
    )
    expect(proposals).toEqual([])
  })
})

function publicationFixture(body: string | Buffer = 'bytes') {
  const sent: (string | number)[][] = [],
    fatal = vi.fn<(error: Error) => void>()
  const files = new NativeAttachmentCallbacks(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    fatal,
    () => undefined,
    { workerIndex: 1, pid },
    () => '0',
  )
  files.packet(['ATTACH_CONTEXT', '0', '1', '1', '1', '1', '1', String(pid)])
  const result = files.bind(owner, () => true)('ordinary', { body })
  files.packet(['ATTACH_MUTATION_RESULT', '1', '0', '1', '1', '1', '1', '-'])
  files.packet(['ATTACH_ADMITTED', '2', '0', '1', '1', '17', '1', '-'])
  files.packet(['ATTACH_CHUNK_OK', '2', '0', '1', '1', '17', '1'])
  files.packet([
    'ATTACH_PUBLISHED',
    '2',
    '0',
    '1',
    '1',
    '17',
    wire('ordinary'),
    wire('text/plain'),
    wire('/native/ordinary.txt'),
    '5',
    '91',
  ])
  return { files, result, sent, fatal }
}
describe('publication-view protocol observer (controlled native packets only)', () => {
  it('preserves the original ordinary Buffer alias while native wire bytes stay at admission', async () => {
    const input = Buffer.from('bytes'),
      { files, result, sent } = publicationFixture(input)
    input[0] = 17
    files.packet(['ATTACH_RESULT', '2', '0', '1', '1', '17', '1', '-'])
    files.packet(['ATTACH_MUTATION_RESULT', '3', '0', '1', '1', '2', '1', '-'])
    await expect(result).resolves.toBeUndefined()
    const entry = files.view(owner)[0]
    expect(entry?.body).toBe(input)
    expect(entry?.body?.[0]).toBe(17)
    if (!entry?.body) throw new Error('Missing ordinary body publication')
    entry.body[1] = 23
    expect(input[1]).toBe(23)
    const chunks = sent.filter((row) => row[0] === 'ATTACH_CHUNK')
    expect(chunks).toHaveLength(1)
    expect(chunks[0]?.at(-1)).toBe(Buffer.from('bytes').toString('hex'))
  })
  it('holds public completion until exact native view acceptance without claiming a writer receipt', async () => {
    const { files, result, sent, fatal } = publicationFixture()
    let resolved = false
    void result.then(() => {
      resolved = true
    })
    files.packet(['ATTACH_RESULT', '2', '0', '1', '1', '17', '1', '-'])
    await Promise.resolve()
    expect(resolved).toBe(false)
    expect(files.pendingMutationRequests).toBe(1)
    expect(sent.at(-1)).toEqual(['ATTACH_MUTATE', '3', '0', '1', '1', '1', '0', 'P', '91', '17'])
    expect(() => files.packet(['ATTACH_CONTEXT_RETIRED', '0', '1', '1'])).toThrow(
      'before actual mutation receipts',
    )
    files.packet(['ATTACH_MUTATION_RESULT', '3', '0', '1', '1', '2', '1', '-'])
    await expect(result).resolves.toBeUndefined()
    files.packet(['ATTACH_CONTEXT_RETIRED', '0', '1', '1'])
    expect(files.idleReady).toBe(true)
    expect(fatal).not.toHaveBeenCalled()
  })
  it('propagates the genuine native rejection rather than inventing successful view publication', async () => {
    const { files, result, fatal } = publicationFixture()
    const failure = expect(result).rejects.toThrow('original publication deadline expired')
    files.packet(['ATTACH_RESULT', '2', '0', '1', '1', '17', '1', '-'])
    files.packet([
      'ATTACH_MUTATION_RESULT',
      '3',
      '0',
      '1',
      '1',
      '1',
      '0',
      wire('original publication deadline expired'),
    ])
    await failure
    expect(files.pendingMutationRequests).toBe(0)
    expect(fatal).not.toHaveBeenCalled()
  })
})
