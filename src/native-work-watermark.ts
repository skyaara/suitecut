// SPDX-License-Identifier: MIT
// Private original-stdout observation bookkeeping, not Engine idle authority.
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

const uint64 = z.string().refine((value) => {
  if (!value || value.length > 20 || (value.length > 1 && value.startsWith('0'))) return false
  for (let i = 0; i < value.length; ++i) {
    const code = value.charCodeAt(i)
    if (code < 48 || code > 57) return false
  }
  return BigInt(value) <= 18446744073709551615n
})
const positive64 = uint64.refine((value) => value !== '0')
export const NativeWorkWatermarkSchema = z.strictObject({
  version: z.literal(1),
  type: z.literal('workwatermark'),
  bytes: z.literal(0),
  barrier: positive64,
  readerInstance: positive64,
  readerThrough: uint64,
  readerCut: positive64,
  writerInstance: positive64,
  writerThrough: uint64,
  writerCut: positive64,
})
export type NativeWorkWatermark = z.infer<typeof NativeWorkWatermarkSchema>
interface Entry {
  expected?: NativeWorkWatermark
  observed?: NativeWorkWatermark
  sent: boolean
}
const equal = (a: NativeWorkWatermark, b: NativeWorkWatermark): boolean =>
  a.barrier === b.barrier &&
  a.readerInstance === b.readerInstance &&
  a.readerThrough === b.readerThrough &&
  a.readerCut === b.readerCut &&
  a.writerInstance === b.writerInstance &&
  a.writerThrough === b.writerThrough &&
  a.writerCut === b.writerCut
/**
 * Control/pipe order can differ. Preserve both exact receipts until the real
 * native observation ACK. Retained history is bounded and never reset on reuse.
 * A ready entry proves a matching original packet was decoded, NOT global idle.
 * Only the bound Source transport may call expect/acknowledge; no author export.
 */
export class NativeWorkWatermarkInbox {
  private readonly entries = new Map<string, Entry>()
  private readonly completed = new Set<string>()
  private poisoned = false
  readonly sourceTicket: string
  constructor(ticket: string) {
    this.sourceTicket = positive64.parse(ticket)
  }
  get physical(): number {
    return this.entries.size
  }
  get observationSettled(): boolean {
    return !this.poisoned && this.entries.size === 0
  }
  expect(sourceTicket: string, value: UntrustedInput): void {
    if (sourceTicket !== this.sourceTicket)
      throw new Error('Foreign native Source watermark expectation')
    const header = NativeWorkWatermarkSchema.parse(value),
      entry = this.admit(header.barrier)
    if (entry.expected) throw new Error('Duplicate native Source watermark expectation')
    if (entry.observed && !equal(header, entry.observed)) {
      this.poisoned = true
      throw new Error('Original stdout watermark differs from native expectation')
    }
    entry.expected = Object.freeze(header)
  }
  /** Called only by the actual original Source stdout decoder after full payload. */
  observe(value: UntrustedInput): void {
    const header = NativeWorkWatermarkSchema.parse(value),
      entry = this.admit(header.barrier)
    if (entry.observed) throw new Error('Duplicate original stdout watermark')
    if (entry.expected && !equal(header, entry.expected)) {
      this.poisoned = true
      throw new Error('Original stdout watermark differs from native expectation')
    }
    entry.observed = Object.freeze(header)
  }
  take(barrier: string): NativeWorkWatermark | undefined {
    const entry = this.entries.get(barrier)
    if (this.poisoned || !entry?.expected || !entry.observed || entry.sent) return undefined
    entry.sent = true
    return entry.observed
  }
  /**
   * Transport calls this only after an authenticated matching native ACK;
   * timeout, cancel, send initiation or process pidfd readiness cannot call it.
   */
  acknowledge(sourceTicket: string, value: UntrustedInput): void {
    if (sourceTicket !== this.sourceTicket)
      throw new Error('Foreign native Source watermark acknowledgment')
    const header = NativeWorkWatermarkSchema.parse(value),
      entry = this.entries.get(header.barrier)
    if (
      this.poisoned ||
      !entry?.sent ||
      !entry.expected ||
      !entry.observed ||
      !equal(header, entry.expected)
    )
      throw new Error('Native Source watermark acknowledgment lacks exact original observation')
    if (this.completed.size === 64) {
      this.poisoned = true
      throw new Error('Native Source watermark history capacity exhausted')
    }
    this.completed.add(header.barrier)
    this.entries.delete(header.barrier)
  }
  private admit(barrier: string): Entry {
    if (this.poisoned || this.completed.has(barrier))
      throw new Error('Stale or poisoned native Source watermark')
    let entry = this.entries.get(barrier)
    if (!entry) {
      if (this.entries.size === 8) {
        this.poisoned = true
        throw new Error('Native Source watermark inflight capacity exhausted')
      }
      entry = { sent: false }
      this.entries.set(barrier, entry)
    }
    return entry
  }
}
