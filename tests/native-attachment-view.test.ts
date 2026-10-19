// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it } from 'vitest'

import {
  NativeAttachmentViews,
  type NativeTestAttachment,
} from '../src/native-runner-attachment-view.js'
import { type NativeStepOwner } from '../src/native-runner-steps.js'

const body: NativeStepOwner = { token: '0', scope: '1', generation: '1' }
const hook: NativeStepOwner = { token: '7', scope: '4', generation: '1' }
const context = (owner = body, testId = '1', attemptGeneration = '1') => ({
  ...owner,
  testId,
  attemptGeneration,
  workerIndex: 1,
  pid: 101,
})
const fixture = () => {
  let request = 0
  const sent: { owner: NativeStepOwner; items: readonly NativeTestAttachment[] }[] = []
  const views = new NativeAttachmentViews(1, 101, (owner, items) => {
    sent.push({ owner, items })
    return items.map(() => String(++request))
  })
  views.open(context())
  return { views, sent }
}
describe('native attachment declaration view', () => {
  it('reflects absent body versus zero bytes and exact authored path without file claims', () => {
    const { views, sent } = fixture()
    const view = views.view(body)
    expect(Array.isArray(view)).toBe(true)
    views.run(
      body,
      () => true,
      () =>
        expect(
          view.push(
            { name: '', contentType: '' },
            { name: 'zero', contentType: 'text/plain', body: Buffer.from('') },
            { name: 'missing', contentType: 'image/png', path: 'exact missing \ud800 path.png' },
          ),
        ).toBe(3),
    )
    expect(view.map((item) => item.name)).toEqual(['', 'zero', 'missing'])
    expect('body' in (view[0] ?? {})).toBe(false)
    expect(view[1]?.body?.byteLength).toBe(0)
    expect(view[2]).toEqual({
      name: 'missing',
      contentType: 'image/png',
      path: 'exact missing \ud800 path.png',
    })
    expect(sent[0]?.items[2]?.path).toBe('exact missing \ud800 path.png')
    views.referenceAccepted(body, '1')
    views.referenceAccepted(body, '3')
    expect(() => views.referenceAccepted(body, '2')).toThrow('body attachment')
    views.published(body, '2', {
      id: '19',
      name: 'zero',
      contentType: 'text/plain',
      path: '/actual/native/zero.txt',
      bytes: 0,
    })
    expect(Object.keys(view[1] ?? {}).sort()).toEqual(['body', 'contentType', 'name'])
    views.retire(body)
  })
  it('shares one native attempt view across hook/body while selecting the real callback owner', () => {
    const { views, sent } = fixture()
    views.open(context(hook))
    const fromHook = views.view(hook)
    expect(fromHook).toBe(views.view(body))
    views.run(
      hook,
      () => true,
      () => fromHook.push({ name: 'hook', contentType: 'x', path: 'author-ref' }),
    )
    views.referenceAccepted(hook, '1')
    views.retire(hook)
    views.run(
      body,
      () => true,
      () => fromHook.push({ name: 'body', contentType: 'x' }),
    )
    expect(sent.map((row) => row.owner)).toEqual([hook, body])
    expect([...views.view(body)].map((item) => item.name)).toEqual(['hook', 'body'])
  })
  it('snapshots native bytes while preserving the original mutable author Buffer', () => {
    const { views, sent } = fixture(),
      source = Buffer.from('real')
    const view = views.view(body)
    views.run(
      body,
      () => true,
      () => view.push({ name: 'bytes', contentType: 'x', body: source }),
    )
    source.fill(0)
    view[0]?.body?.fill(1)
    expect(Buffer.from(sent[0]?.items[0]?.body ?? []).toString()).toBe('real')
    expect(Object.isFrozen(view[0])).toBe(false)
    expect(view[0]?.body).toBe(source)
  })
  it('validates the whole batch before requests or view mutation', () => {
    const { views, sent } = fixture(),
      view = views.view(body)
    views.run(
      body,
      () => true,
      () => {
        expect(() => {
          // @ts-expect-error exercise the actual runtime declaration boundary
          view.push({ name: 'valid', contentType: 'x' }, { name: 'bad', contentType: 17 })
        }).toThrow()
        expect(() =>
          view.push({
            name: 'too long',
            contentType: 'x',
            body: Buffer.from('x'),
            path: 'x'.repeat(8193),
          }),
        ).toThrow()
      },
    )
    expect(sent).toEqual([])
    expect(view.length).toBe(0)
  })
  it('rejects foreign-attempt use and stale surviving async owner contexts', async () => {
    const { views, sent } = fixture(),
      old = views.view(body)
    const retry = { ...body, generation: '2' }
    views.open(context(retry, '1', '2'))
    expect(views.view(retry)).not.toBe(old)
    views.run(
      retry,
      () => true,
      () =>
        expect(() => old.push({ name: 'foreign', contentType: 'x' })).toThrow(
          'another actual attempt',
        ),
    )
    let active = true
    await views.run(
      body,
      () => active,
      async () => {
        active = false
        await Promise.resolve()
        expect(() => old.push({ name: 'late', contentType: 'x' })).toThrow('active author callback')
      },
    )
    views.retire(body)
    views.run(
      retry,
      () => true,
      () => expect(() => old.push({ name: 'retired', contentType: 'x' })).toThrow(),
    )
    expect(sent).toEqual([])
  })
  it('requires terminal native receipts before context retirement and rejects duplicate/foreign publication', () => {
    const { views } = fixture(),
      view = views.view(body)
    views.run(
      body,
      () => true,
      () => view.push({ name: 'actual', contentType: 'x', body: Buffer.from('a') }),
    )
    expect(() => views.retire(body)).toThrow('before actual')
    views.open(context(hook))
    const receipt = {
      id: '9007199254740993',
      name: 'actual',
      contentType: 'x',
      path: '/actual/native/file',
      bytes: 1,
    }
    expect(() => views.published(hook, '1', receipt)).toThrow('foreign')
    expect(() => views.published(body, '1', { ...receipt, bytes: 0 })).toThrow('exact declared')
    views.published(body, '1', receipt)
    expect(() => views.published(body, '1', receipt)).toThrow('duplicate')
    views.retire(body)
  })
  it('retains authored failed declarations without manufacturing artifact fields', () => {
    const { views } = fixture(),
      view = views.view(body)
    views.run(
      body,
      () => true,
      () => view.push({ name: 'failed', contentType: 'x', body: Buffer.from('body') }),
    )
    views.failed(body, '1')
    expect(Object.keys(view[0] ?? {}).sort()).toEqual(['body', 'contentType', 'name'])
    expect(() => views.referenceAccepted(body, '1')).toThrow('duplicate')
    views.retire(body)
  })
  it('rejects unsupported non-append mutations without requests', () => {
    const { views, sent } = fixture(),
      view = views.view(body)
    expect(() => Reflect.set(view, 'length', 8)).toThrow('assignment')
    expect(() => Reflect.deleteProperty(view, '0')).toThrow('deletion')
    expect(() => Object.defineProperty(view, '0', { value: 'fake' })).toThrow('mutation')
    expect(() => Object.freeze(view)).toThrow('frozen or sealed')
    expect(() => Reflect.setPrototypeOf(view, {})).toThrow('prototype mutation')
    expect(() => {
      Array.prototype.pop.call(view)
    }).toThrow()
    expect(sent).toEqual([])
  })
  it('bounds the whole host catalog and validates actual native callback contexts', () => {
    const { views, sent } = fixture(),
      view = views.view(body)
    expect(() => views.open({ ...context(hook), pid: 102 })).toThrow('foreign')
    expect(() => views.open(context())).toThrow('duplicate')
    views.run(
      body,
      () => true,
      () => {
        expect(
          view.push(
            ...Array.from({ length: 128 }, (_, index) => ({
              name: String(index),
              contentType: 'x',
            })),
          ),
        ).toBe(128)
        expect(() => view.push({ name: 'overflow', contentType: 'x' })).toThrow('capacity')
      },
    )
    expect(sent.length).toBe(1)
  })
})
