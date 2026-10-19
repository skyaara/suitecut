// SPDX-License-Identifier: MIT
// Native packets below are controlled receipts, not an actual writer proof.
import { describe, expect, it } from 'vitest'

import {
  nativeRetryRequestInvocation,
  runNativeRetryInvocation,
  type NativeRetryInvocation,
} from '../src/native-callback-operations.js'
import { NativeFormActions } from '../src/native-form.js'
import { NativeAttachmentCallbacks } from '../src/native-runner-attachments.js'
import { type UntrustedInput } from '../src/untrusted.js'

const owner = { token: '0', scope: '1', generation: '1' }
const wire = (value: string) => Buffer.from(value, 'utf16le').toString('hex') || '-'
function fixture() {
  const sent: (string | number)[][] = [],
    failures: UntrustedInput[] = []
  const invocation: NativeRetryInvocation = {
    owner,
    assertionID: '11',
    invocationNonce: '3',
    step: '12',
    signal: new AbortController().signal,
    timeoutMs: 100,
    accepting: true,
    retired: false,
    releases: new Map(),
    operations: new Set(),
    operationsDrained: () => undefined,
    producerFailure: (error) => failures.push(error),
  }
  const files = new NativeAttachmentCallbacks(
    (...fields) => {
      sent.push(fields)
      return Promise.resolve()
    },
    (error) => {
      throw error
    },
    () => undefined,
    { workerIndex: 1, pid: 99 },
    () => '0',
  )
  files.packet(['ATTACH_CONTEXT', '0', '1', '1', '1', '1', '1', '99'])
  return { sent, failures, invocation, files }
}
describe('private native retry provenance preserves public file owners', () => {
  it('keeps TestInfo attach test-level and captured explicit step attach on its original step', () => {
    const f = fixture(),
      info = f.files.bind(owner, () => true),
      outer = f.files.bindStep(owner, '8', () => true)
    runNativeRetryInvocation(f.invocation, () => {
      void info('testfile', { body: 'a' }).catch(() => undefined)
      void outer('outerfile', { body: 'b' }).catch(() => undefined)
    })
    const first = f.sent.find((fields) => fields[0] === 'ATTACH_BEGIN'),
      second = f.sent.find((fields) => fields[0] === 'ATTACH_STEP_BEGIN')
    expect(first?.slice(0, 5)).toEqual(['ATTACH_BEGIN', '2', '0', '1', '1'])
    expect(first?.length).toBe(13)
    expect(first?.slice(-3)).toEqual(['11', '3', '12'])
    expect(second?.[5]).toBe('8')
    expect(second?.length).toBe(14)
    expect(second?.slice(-3)).toEqual(['11', '3', '12'])
  })
  it('permits only actual admitted publication provenance after raw author closure', () => {
    const f = fixture()
    let result!: Promise<void>
    runNativeRetryInvocation(f.invocation, () => {
      result = f.files.bind(owner, () => true)('file', { body: 'x' })
    })
    void result.catch(() => undefined)
    f.files.packet(['ATTACH_MUTATION_RESULT', '1', '0', '1', '1', '1', '1', '-'])
    f.files.packet(['ATTACH_ADMITTED', '2', '0', '1', '1', '17', '1', '-'])
    f.files.packet(['ATTACH_CHUNK_OK', '2', '0', '1', '1', '17', '1'])
    f.invocation.accepting = false
    runNativeRetryInvocation(f.invocation, () => {
      expect(() => f.files.bind(owner, () => true)('late', { body: 'no' })).toThrow(
        'no longer admits',
      )
      f.files.packet([
        'ATTACH_PUBLISHED',
        '2',
        '0',
        '1',
        '1',
        '17',
        wire('file'),
        wire('text/plain'),
        wire('/native/file.txt'),
        '1',
        '91',
      ])
    })
    const publication = f.sent.find((fields) => fields[0] === 'ATTACH_MUTATE' && fields[7] === 'P')
    expect(publication?.slice(8, 10)).toEqual(['91', '17'])
    expect(publication?.slice(-3)).toEqual(['11', '3', '12'])
  })
  it('latches only exact native infrastructure classification while ordinary errors stay ordinary', async () => {
    for (const classification of ['O', 'I']) {
      const f = fixture()
      let result!: Promise<void>
      runNativeRetryInvocation(f.invocation, () => {
        result = f.files.bind(owner, () => true)('file', { path: '/ordinary/missing' })
      })
      void result.catch(() => undefined)
      f.files.packet(['ATTACH_MUTATION_RESULT', '1', '0', '1', '1', '1', '1', '-'])
      f.files.packet(['ATTACH_ADMITTED', '2', '0', '1', '1', '17', '1', '-'])
      f.files.packet([
        'ATTACH_RESULT',
        '2',
        '0',
        '1',
        '1',
        '17',
        '0',
        wire('same message'),
        classification,
      ])
      await expect(result).rejects.toThrow('same message')
      expect(f.failures.length).toBe(classification === 'I' ? 1 : 0)
    }
  })
  it('keeps path/reference append metadata without inventing body chunks', () => {
    const f = fixture()
    runNativeRetryInvocation(f.invocation, () =>
      f.files.run(
        owner,
        () => true,
        () => {
          f.files
            .view(owner)
            .push({ name: 'ref', path: '/author/reference', contentType: 'text/plain' })
        },
      ),
    )
    const append = f.sent.find((fields) => fields[0] === 'ATTACH_APPEND_BEGIN')
    expect(append?.slice(-3)).toEqual(['11', '3', '12'])
    expect(append?.[7]).toBe(0)
    expect(f.sent.some((row) => row[0] === 'ATTACH_CHUNK')).toBe(false)
  })
  it('does not lend a live input-upload continuation to an authored filename getter', async () => {
    const f = fixture()
    let releaseBegin!: (value: UntrustedInput) => void,
      releaseGetter!: () => void,
      late!: Promise<void>
    const getterGate = new Promise<void>((resolve) => {
      releaseGetter = resolve
    })
    const actions = new NativeFormActions((params) =>
      params.action === 'uploadBegin'
        ? new Promise((resolve) => {
            releaseBegin = resolve
          })
        : Promise.resolve(null),
    )
    const payload = {
      get name() {
        late = (async () => {
          await getterGate
          nativeRetryRequestInvocation()
        })()
        void late.catch(() => undefined)
        return 'real.txt'
      },
      mimeType: 'text/plain',
      buffer: Buffer.from('one'),
    }
    const result = runNativeRetryInvocation(f.invocation, () => actions.setInputFiles(payload))
    void result.catch(() => undefined)
    f.invocation.accepting = false
    releaseGetter()
    await expect(late).rejects.toThrow('no longer admits')
    expect(f.failures).toHaveLength(1)
    releaseBegin('actual-upload-token')
    await result
    expect(f.invocation.operations.size).toBe(0)
  })
})
