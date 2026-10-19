// SPDX-License-Identifier: MIT
// Original-pipe/native-channel receipt marshalling. Native C++ alone issues
// challenges, decides actual work readiness and transfers live process leases.
import {
  NativeWorkWatermarkInbox,
  NativeWorkWatermarkSchema,
  type NativeWorkWatermark,
} from './native-work-watermark.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeSourceWorkChallenge {
  readonly ticket: string
  readonly nonce: string
  readonly borrowLease: string
  readonly borrowGeneration: string
  /** Exact native deadline text, forwarded without a new SDK timer/budget. */
  readonly deadline: string
  readonly purpose?: 'callbackIteration'
}
export interface NativeSourceWorkConnection {
  begin(challenge: NativeSourceWorkChallenge): void
  expect(ticket: string, watermark: NativeWorkWatermark): void
  acknowledge(ticket: string, watermark: NativeWorkWatermark): void
  refuse(ticket: string, nonce: string): void
  resume(challenge: NativeSourceWorkChallenge, nextGeneration: string): void
  readonly physical: number
  readonly idleReady: boolean
  onChange?(listener: () => void): () => void
}
interface Pending {
  readonly challenge: NativeSourceWorkChallenge
  rawReturned: boolean
  acknowledged: boolean
  failed: boolean
  refused: boolean
}
export interface NativeSourceWorkHooks {
  /** Actual ordered NativeBrowser request, not another process/driver. */
  readonly ordered: (challenge: NativeSourceWorkChallenge) => Promise<UntrustedInput>
  /** Actual native Runner transport; resolves only transport write admission. */
  readonly observed: (ticket: string, watermark: NativeWorkWatermark) => Promise<void>
  readonly failed: (challenge: NativeSourceWorkChallenge, error: Error) => void
  readonly changed: () => void
}
export class NativeSourceWorkCallbacks implements NativeSourceWorkConnection {
  private readonly inbox: NativeWorkWatermarkInbox
  private pending: Pending | undefined
  private lastNonce = 0n
  private closed = false
  private readonly listeners = new Set<() => void>()
  onChange(listener: () => void): () => void {
    this.listeners.add(listener)
    return () => {
      this.listeners.delete(listener)
    }
  }
  private changed(): void {
    this.hooks.changed()
    for (const listener of this.listeners) listener()
  }
  constructor(
    readonly ticket: string,
    private readonly hooks: NativeSourceWorkHooks,
  ) {
    this.inbox = new NativeWorkWatermarkInbox(ticket)
  }
  get physical(): number {
    const pending = this.pending
    return (pending && !pending.rawReturned ? 1 : 0) + this.inbox.physical
  }
  get idleReady(): boolean {
    const pending = this.pending
    return (
      !this.closed &&
      this.inbox.observationSettled &&
      (!pending || (!pending.failed && pending.rawReturned && pending.acknowledged))
    )
  }
  /** A granted old borrow stays gated until the actual native next admission. */
  beforeAuthorAdmission(): void {
    if (this.closed || (this.pending && this.pending.challenge.purpose !== 'callbackIteration'))
      throw new Error('Native Source borrow has not resumed automation admission')
  }
  begin(challenge: NativeSourceWorkChallenge): void {
    if (
      this.closed ||
      challenge.ticket !== this.ticket ||
      this.pending ||
      BigInt(challenge.nonce) <= this.lastNonce
    )
      throw new Error('Native Source work challenge has foreign or stale ownership')
    this.lastNonce = BigInt(challenge.nonce)
    const pending: Pending = {
      challenge: Object.freeze({ ...challenge }),
      rawReturned: false,
      acknowledged: false,
      failed: false,
      refused: false,
    }
    this.pending = pending
    // Calling ordered synchronously puts this actual command after all earlier
    // sends on the same NativeBrowser stdin transport. It does not prove idle.
    let raw: Promise<UntrustedInput>
    try {
      raw = this.hooks.ordered(pending.challenge)
    } catch (error) {
      this.fail(
        pending,
        error instanceof Error ? error : new Error('Native Source ordered transport threw'),
      )
      return
    }
    void raw.then(
      () => {
        pending.rawReturned = true
        this.changed()
      },
      (error: UntrustedInput) => {
        pending.rawReturned = true
        this.fail(pending, error)
      },
    )
    this.changed()
  }
  expect(ticket: string, watermark: NativeWorkWatermark): void {
    this.match(ticket, watermark)
    this.inbox.expect(ticket, watermark)
    this.publish(watermark.barrier)
  }
  /** Actual original stdout decoder invokes this only after the full packet. */
  observe(value: UntrustedInput): void {
    const watermark = NativeWorkWatermarkSchema.parse(value)
    this.match(this.ticket, watermark)
    this.inbox.observe(watermark)
    this.publish(watermark.barrier)
  }
  acknowledge(ticket: string, watermark: NativeWorkWatermark): void {
    const pending = this.match(ticket, watermark)
    this.inbox.acknowledge(ticket, watermark)
    pending.acknowledged = true
    this.changed()
  }
  /** Exact native refusal selects physical fresh-host cleanup, not browser failure. */
  refuse(ticket: string, nonce: string): void {
    const pending = this.pending
    if (
      !pending ||
      pending.refused ||
      ticket !== this.ticket ||
      nonce !== pending.challenge.nonce ||
      pending.acknowledged
    )
      throw new Error('Native Source refusal lacks its exact pending challenge')
    // A real raw close/rejection may precede this exact native receipt.
    // Observe it once without reopening admission or manufacturing an ACK.
    pending.refused = true
    pending.failed = true
    this.changed()
  }
  resume(challenge: NativeSourceWorkChallenge, nextGeneration: string): void {
    const pending = this.pending
    if (
      pending?.challenge.ticket !== challenge.ticket ||
      pending.challenge.nonce !== challenge.nonce ||
      pending.challenge.borrowLease !== challenge.borrowLease ||
      pending.challenge.borrowGeneration !== challenge.borrowGeneration ||
      pending.challenge.deadline !== challenge.deadline ||
      pending.challenge.purpose !== challenge.purpose ||
      !this.idleReady ||
      BigInt(nextGeneration) <= BigInt(challenge.borrowGeneration)
    )
      throw new Error('Native Source resume lacks its exact completed borrow receipt')
    this.pending = undefined
    this.changed()
  }
  /** Actual process/channel failure retains pending receipts for native cleanup. */
  close(): void {
    this.closed = true
    this.changed()
  }
  private match(ticket: string, watermark: NativeWorkWatermark): Pending {
    const pending = this.pending
    if (
      !pending ||
      pending.failed ||
      ticket !== this.ticket ||
      pending.challenge.nonce !== watermark.barrier
    )
      throw new Error('Native Source watermark has no exact active challenge')
    return pending
  }
  private publish(barrier: string): void {
    const value = this.inbox.take(barrier)
    if (!value) return
    const pending = this.pending
    if (!pending) throw new Error('Native Source original observation lost its actual owner')
    void this.hooks.observed(this.ticket, value).catch((error: UntrustedInput) => {
      this.fail(pending, error)
    })
    this.changed()
  }
  private fail(pending: Pending, error: UntrustedInput): void {
    if (pending.failed) return
    pending.failed = true
    this.hooks.failed(
      pending.challenge,
      error instanceof Error ? error : new Error('Native Source work transport failed'),
    )
    this.changed()
  }
}
