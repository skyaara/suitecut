// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  NativeWorkWatermarkInbox,
  NativeWorkWatermarkSchema,
} from '../src/native-work-watermark.js'

const packet = (barrier = '1') => ({
  version: 1 as const,
  type: 'workwatermark' as const,
  bytes: 0 as const,
  barrier,
  readerInstance: '1',
  readerThrough: '0',
  readerCut: '1',
  writerInstance: '2',
  writerThrough: '7',
  writerCut: '1',
})
describe('private original stdout watermark receipts (no Engine idle proof)', () => {
  it('retains observation-first ordering until authentic matching native ACK', () => {
    const inbox = new NativeWorkWatermarkInbox('4'),
      value = packet()
    inbox.observe(value)
    expect(inbox.physical).toBe(1)
    expect(inbox.take('1')).toBeUndefined()
    inbox.expect('4', value)
    expect(inbox.take('1')).toEqual(value)
    expect(inbox.take('1')).toBeUndefined()
    expect(inbox.observationSettled).toBe(false)
    inbox.acknowledge('4', value)
    expect(inbox.observationSettled).toBe(true)
  })
  it('retains expectation-first ordering with no fabricated original receipt', () => {
    const inbox = new NativeWorkWatermarkInbox('4'),
      value = packet()
    inbox.expect('4', value)
    expect(inbox.take('1')).toBeUndefined()
    expect(() => inbox.acknowledge('4', value)).toThrow()
    expect(inbox.physical).toBe(1)
    inbox.observe(value)
    expect(inbox.take('1')).toEqual(value)
    inbox.acknowledge('4', value)
  })
  it('rejects foreign source expectations and acknowledgments without losing ownership', () => {
    const inbox = new NativeWorkWatermarkInbox('4'),
      value = packet()
    expect(() => inbox.expect('5', value)).toThrow()
    expect(inbox.physical).toBe(0)
    inbox.expect('4', value)
    inbox.observe(value)
    inbox.take('1')
    expect(() => inbox.acknowledge('5', value)).toThrow()
    expect(inbox.physical).toBe(1)
    inbox.acknowledge('4', value)
  })
  it('rejects a different native cut and retains both pending ownership and poison', () => {
    const inbox = new NativeWorkWatermarkInbox('4')
    inbox.expect('4', packet())
    expect(() => inbox.observe({ ...packet(), writerThrough: '6' })).toThrow()
    expect(inbox.physical).toBe(1)
    expect(inbox.take('1')).toBeUndefined()
    expect(inbox.observationSettled).toBe(false)
  })
  it('rejects mutated ACK fields while retaining the genuine observation', () => {
    const inbox = new NativeWorkWatermarkInbox('4'),
      value = packet()
    inbox.expect('4', value)
    inbox.observe(value)
    inbox.take('1')
    expect(() => inbox.acknowledge('4', { ...value, readerCut: '2' })).toThrow()
    expect(inbox.physical).toBe(1)
    inbox.acknowledge('4', value)
  })
  it('rejects duplicate original packets and duplicate control expectations', () => {
    const inbox = new NativeWorkWatermarkInbox('4'),
      value = packet()
    inbox.expect('4', value)
    expect(() => inbox.expect('4', value)).toThrow()
    inbox.observe(value)
    expect(() => inbox.observe(value)).toThrow()
    expect(inbox.physical).toBe(1)
  })
  it('rejects replay after native acknowledgment', () => {
    const inbox = new NativeWorkWatermarkInbox('4'),
      value = packet()
    inbox.expect('4', value)
    inbox.observe(value)
    inbox.take('1')
    inbox.acknowledge('4', value)
    expect(() => inbox.observe(value)).toThrow()
    expect(() => inbox.expect('4', value)).toThrow()
    expect(() => inbox.acknowledge('4', value)).toThrow()
  })
  it('enforces zero payload and exact schema without silently accepting unknown fields', () => {
    expect(NativeWorkWatermarkSchema.safeParse({ ...packet(), bytes: 1 }).success).toBe(false)
    expect(NativeWorkWatermarkSchema.safeParse({ ...packet(), idle: true }).success).toBe(false)
    expect(NativeWorkWatermarkSchema.safeParse({ ...packet(), pageId: 1 }).success).toBe(false)
  })
  it('preserves exact uint64 values without Number conversion', () => {
    const value = { ...packet('18446744073709551615'), writerThrough: '18446744073709551615' }
    expect(NativeWorkWatermarkSchema.parse(value)).toEqual(value)
  })
  it('rejects malformed/noncanonical/overflow unsigned values and zero identities', () => {
    for (const value of ['', '01', '-1', '+1', ' 1', '1.0', '1e2', '18446744073709551616'])
      expect(
        NativeWorkWatermarkSchema.safeParse({ ...packet(), writerThrough: value }).success,
      ).toBe(false)
    for (const key of ['barrier', 'readerInstance', 'readerCut', 'writerInstance', 'writerCut'])
      expect(NativeWorkWatermarkSchema.safeParse({ ...packet(), [key]: '0' }).success).toBe(false)
  })
  it('retains all eight existing requests at inflight capacity refusal', () => {
    const inbox = new NativeWorkWatermarkInbox('4')
    for (let n = 1; n <= 8; ++n) inbox.expect('4', packet(String(n)))
    expect(() => inbox.observe(packet('9'))).toThrow()
    expect(inbox.physical).toBe(8)
    expect(inbox.observationSettled).toBe(false)
  })
  it('fails closed at bounded terminal history without resetting live-PID state', () => {
    const inbox = new NativeWorkWatermarkInbox('4')
    for (let n = 1; n <= 64; ++n) {
      const value = packet(String(n))
      inbox.expect('4', value)
      inbox.observe(value)
      inbox.take(value.barrier)
      inbox.acknowledge('4', value)
    }
    const value = packet('65')
    inbox.expect('4', value)
    inbox.observe(value)
    inbox.take('65')
    expect(() => inbox.acknowledge('4', value)).toThrow()
    expect(inbox.physical).toBe(1)
    expect(inbox.observationSettled).toBe(false)
  })
})
