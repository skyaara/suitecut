// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it } from 'vitest'

import { NativePacketDecoder } from '../src/native-protocol.js'
import {
  NativeSourceWorkCallbacks,
  type NativeSourceWorkChallenge,
} from '../src/native-source-work.js'
import { type NativeWorkWatermark } from '../src/native-work-watermark.js'
import { type UntrustedInput } from '../src/untrusted.js'

const challenge: NativeSourceWorkChallenge = {
  ticket: '7',
  nonce: '3',
  borrowLease: '9',
  borrowGeneration: '11',
  deadline: '1000.125',
}
const watermark: NativeWorkWatermark = {
  version: 1,
  type: 'workwatermark',
  bytes: 0,
  barrier: '3',
  readerInstance: '2',
  readerThrough: '13',
  readerCut: '4',
  writerInstance: '1',
  writerThrough: '17',
  writerCut: '8',
}
function fixture() {
  let resolve!: (value: UntrustedInput) => void
  let reject!: (error: Error) => void
  const raw = new Promise<UntrustedInput>((yes, no) => {
    resolve = yes
    reject = no
  })
  const ordered: NativeSourceWorkChallenge[] = [],
    observed: NativeWorkWatermark[] = [],
    failures: Error[] = []
  const callbacks = new NativeSourceWorkCallbacks('7', {
    ordered: (value) => {
      ordered.push(value)
      return raw
    },
    observed: (_ticket, value) => {
      observed.push(value)
      return Promise.resolve()
    },
    failed: (_value, error) => {
      failures.push(error)
    },
    changed: () => undefined,
  })
  const decoder = new NativePacketDecoder((header) => {
    if (header.type === 'workwatermark') callbacks.observe(header)
  })
  const header = Buffer.from(JSON.stringify(watermark)),
    prefix = Buffer.alloc(4)
  prefix.writeUInt32LE(header.length)
  return {
    callbacks,
    decoder,
    bytes: Buffer.concat([prefix, header]),
    resolve,
    reject,
    ordered,
    observed,
    failures,
  }
}

describe('actual original decoder and native Source receipt bridge (component scope)', () => {
  it('queues the original command synchronously and gates ordinary author admission', () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    expect(f.ordered).toEqual([challenge])
    expect(f.callbacks.physical).toBe(1)
    expect(() => f.callbacks.beforeAuthorAdmission()).toThrow('not resumed')
  })
  it('does not publish an observation from a partial original pipe packet', () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    f.callbacks.expect('7', watermark)
    for (const byte of f.bytes.subarray(0, -1)) f.decoder.push(Buffer.from([byte]))
    expect(f.observed).toEqual([])
    expect(f.callbacks.idleReady).toBe(false)
    f.decoder.push(f.bytes.subarray(-1))
    expect(f.observed).toEqual([watermark])
  })
  it('accepts original stdout observation before the authenticated expectation', () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    f.decoder.push(f.bytes)
    expect(f.observed).toEqual([])
    f.callbacks.expect('7', watermark)
    expect(f.observed).toEqual([watermark])
    expect(f.callbacks.idleReady).toBe(false)
  })
  it('rejects native ACK before actual stdout decoding without losing the pending raw command', () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    f.callbacks.expect('7', watermark)
    expect(() => f.callbacks.acknowledge('7', watermark)).toThrow()
    expect(f.callbacks.physical).toBe(2)
    f.decoder.push(f.bytes)
    f.callbacks.acknowledge('7', watermark)
    expect(f.callbacks.physical).toBe(1)
  })
  it('requires actual raw command settlement after the native full-write ACK', async () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    f.callbacks.expect('7', watermark)
    f.decoder.push(f.bytes)
    f.callbacks.acknowledge('7', watermark)
    expect(f.callbacks.idleReady).toBe(false)
    f.resolve({ acceptedWork: true })
    await Promise.resolve()
    expect(f.callbacks.idleReady).toBe(true)
  })
  it('does not infer native ACK from a successful raw command response', async () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    f.resolve({ acceptedWork: true })
    await Promise.resolve()
    expect(f.callbacks.idleReady).toBe(false)
    expect(() => f.callbacks.resume(challenge, '12')).toThrow('exact completed')
  })
  it('rejects foreign Source ownership and altered cut metadata', () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    expect(() => f.callbacks.expect('8', watermark)).toThrow()
    f.callbacks.expect('7', watermark)
    f.decoder.push(f.bytes)
    expect(() => f.callbacks.acknowledge('7', { ...watermark, writerCut: '9' })).toThrow()
    expect(f.callbacks.idleReady).toBe(false)
  })
  it('reopens admission only after the exact native transferred generation', async () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    f.callbacks.expect('7', watermark)
    f.decoder.push(f.bytes)
    f.resolve({ acceptedWork: true })
    await Promise.resolve()
    f.callbacks.acknowledge('7', watermark)
    expect(() => f.callbacks.resume(challenge, '11')).toThrow()
    expect(() => f.callbacks.resume({ ...challenge, borrowLease: '10' }, '12')).toThrow()
    f.callbacks.resume(challenge, '12')
    expect(() => f.callbacks.beforeAuthorAdmission()).not.toThrow()
    expect(() => f.callbacks.begin(challenge)).toThrow('stale')
  })
  it('retains a genuine native refusal for fresh physical cleanup without inventing browser failure', async () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    f.callbacks.refuse('7', '3')
    f.resolve({ acceptedWork: false, reason: 'unattestedProducer' })
    await Promise.resolve()
    expect(f.failures).toEqual([])
    expect(f.callbacks.idleReady).toBe(false)
    expect(() => f.callbacks.beforeAuthorAdmission()).toThrow()
    expect(() => f.callbacks.resume(challenge, '12')).toThrow()
  })
  it('keeps a real transport rejection failed instead of manufacturing settlement success', async () => {
    const f = fixture(),
      error = new Error('actual pipe refused')
    f.callbacks.begin(challenge)
    f.reject(error)
    await Promise.resolve()
    expect(f.failures).toEqual([error])
    expect(f.callbacks.idleReady).toBe(false)
  })
  it('observes a late exact native ACK after actual close without granting idle or resume', async () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    f.decoder.push(f.bytes)
    f.callbacks.close()
    f.callbacks.expect('7', watermark)
    f.callbacks.acknowledge('7', watermark)
    f.resolve(null)
    await Promise.resolve()
    expect(f.callbacks.idleReady).toBe(false)
    expect(() => f.callbacks.resume(challenge, '12')).toThrow()
  })
  it('retains one exact late native refusal after real raw rejection and close, rejecting foreign/replay receipts', async () => {
    const f = fixture(),
      error = new Error('actual native close')
    f.callbacks.begin(challenge)
    f.reject(error)
    await Promise.resolve()
    f.callbacks.close()
    expect(() => f.callbacks.refuse('8', '3')).toThrow()
    expect(() => f.callbacks.refuse('7', '4')).toThrow()
    expect(() => f.callbacks.refuse('7', '3')).not.toThrow()
    expect(() => f.callbacks.refuse('7', '3')).toThrow()
    expect(f.failures).toEqual([error])
    expect(f.callbacks.physical).toBe(0)
    expect(f.callbacks.idleReady).toBe(false)
    expect(() => f.callbacks.resume(challenge, '12')).toThrow()
    expect(() => f.callbacks.beforeAuthorAdmission()).toThrow()
  })
  it('rejects nonzero watermark payload through the actual production decoder', () => {
    const f = fixture()
    f.callbacks.begin(challenge)
    const header = Buffer.from(JSON.stringify({ ...watermark, bytes: 1 })),
      prefix = Buffer.alloc(4)
    prefix.writeUInt32LE(header.length)
    expect(() => f.decoder.push(Buffer.concat([prefix, header, Buffer.from('x')]))).toThrow()
    expect(f.observed).toEqual([])
    expect(f.callbacks.idleReady).toBe(false)
  })
})
