// SPDX-License-Identifier: MIT
import { type NativeWorkerEvent, type NativeWorkerInfo } from './native-worker-protocol.js'

export interface NativeWorkerRetirement {
  id: string
  type: NativeWorkerInfo['type']
  event: 'close' | 'channelclosed'
  sequence: number
  reason?: string | undefined
}
export interface NativeWorkerReadyReceipt {
  id: string
  type: NativeWorkerInfo['type']
  sequence: number
}
export interface NativeWorkerRetirementSnapshot {
  records: NativeWorkerRetirement[]
  ready?: NativeWorkerReadyReceipt[] | undefined
}
interface NativeWorkerScope {
  records: Map<string, NativeWorkerRetirement>
  ready: Map<string, NativeWorkerReadyReceipt>
}
/**
 * Real owner-scoped lifecycle receipts; readiness never overrides target death.
 * Receipts are indexed by scope so a snapshot reads only its owner, and an
 * owner's receipts are released when that page or context closes.
 */
export class NativeWorkerRetirements {
  private readonly scopes = new Map<string, NativeWorkerScope>()
  restore(snapshot: NativeWorkerRetirementSnapshot | undefined): void {
    if (!snapshot) return
    for (const record of snapshot.records) this.store('', record)
    for (const record of snapshot.ready ?? []) this.storeReady('', record)
  }
  private scope(scope: string): NativeWorkerScope {
    let entry = this.scopes.get(scope)
    if (!entry) {
      entry = { records: new Map(), ready: new Map() }
      this.scopes.set(scope, entry)
    }
    return entry
  }
  private store(scope: string, record: NativeWorkerRetirement): boolean {
    const entry = this.scope(scope)
    const prior = entry.records.get(record.id)
    const ready = entry.ready.get(record.id)
    if ((prior && prior.sequence > record.sequence) || (ready && ready.sequence > record.sequence))
      return false
    if (prior?.event === 'close' && record.event === 'channelclosed') return false
    entry.records.set(record.id, { ...record })
    entry.ready.delete(record.id)
    return true
  }
  private storeReady(scope: string, record: NativeWorkerReadyReceipt): boolean {
    const entry = this.scope(scope)
    const prior = entry.records.get(record.id)
    const ready = entry.ready.get(record.id)
    if (
      prior?.event === 'close' ||
      (prior && prior.sequence >= record.sequence) ||
      (ready && ready.sequence > record.sequence)
    )
      return false
    entry.records.delete(record.id)
    entry.ready.set(record.id, { ...record })
    return true
  }
  observe(scope: string, event: NativeWorkerEvent): boolean {
    if (event.event === 'close' || event.event === 'channelclosed')
      return this.store(scope, {
        id: event.worker.id,
        type: event.worker.type,
        event: event.event,
        sequence: event.sequence,
        reason: event.reason,
      })
    if (event.event === 'worker')
      return this.storeReady(scope, {
        id: event.worker.id,
        type: event.worker.type,
        sequence: event.sequence,
      })
    return true
  }
  get(id: string): NativeWorkerRetirement | undefined {
    return this.scopes.get('')?.records.get(id)
  }
  readySequence(id: string): number | undefined {
    return this.scopes.get('')?.ready.get(id)?.sequence
  }
  latestSequence(id: string): number | undefined {
    return this.get(id)?.sequence ?? this.readySequence(id)
  }
  snapshot(scope: string): NativeWorkerRetirementSnapshot {
    const entry = this.scopes.get(scope)
    return {
      records: [...(entry?.records.values() ?? [])].map((record) => ({ ...record })),
      ready: [...(entry?.ready.values() ?? [])].map((record) => ({ ...record })),
    }
  }
  /** Releases a closed owner's receipts once nothing can observe that owner again. */
  release(scope: string): void {
    this.scopes.delete(scope)
  }
  clear(): void {
    this.scopes.clear()
  }
}
