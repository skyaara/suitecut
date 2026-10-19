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
  truncated: boolean
}
/** Real owner-scoped lifecycle receipts; readiness never overrides target death. */
export class NativeWorkerRetirements {
  private readonly records = new Map<string, { scope: string; record: NativeWorkerRetirement }>()
  private readonly ready = new Map<string, { scope: string; record: NativeWorkerReadyReceipt }>()
  private lostHistory = false
  restore(snapshot: NativeWorkerRetirementSnapshot | undefined): void {
    if (!snapshot) return
    this.lostHistory ||= snapshot.truncated
    for (const record of snapshot.records) this.store('', record)
    for (const record of snapshot.ready ?? []) this.storeReady('', record)
  }
  private trim<T>(records: Map<string, T>): void {
    while (records.size > 1024) {
      const first = records.keys().next().value
      if (first === undefined) break
      records.delete(first)
      this.lostHistory = true
    }
  }
  private store(scope: string, record: NativeWorkerRetirement): boolean {
    const key = `${scope}\n${record.id}`
    const prior = this.records.get(key)?.record
    const ready = this.ready.get(key)?.record
    if ((prior && prior.sequence > record.sequence) || (ready && ready.sequence > record.sequence))
      return false
    if (prior?.event === 'close' && record.event === 'channelclosed') return false
    this.records.set(key, { scope, record: { ...record } })
    this.ready.delete(key)
    this.trim(this.records)
    return true
  }
  private storeReady(scope: string, record: NativeWorkerReadyReceipt): boolean {
    const key = `${scope}\n${record.id}`
    const prior = this.records.get(key)?.record
    const ready = this.ready.get(key)?.record
    if (
      prior?.event === 'close' ||
      (prior && prior.sequence >= record.sequence) ||
      (ready && ready.sequence > record.sequence)
    )
      return false
    this.records.delete(key)
    this.ready.set(key, { scope, record: { ...record } })
    this.trim(this.ready)
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
    return this.records.get(`\n${id}`)?.record
  }
  readySequence(id: string): number | undefined {
    return this.ready.get(`\n${id}`)?.record.sequence
  }
  latestSequence(id: string): number | undefined {
    return this.get(id)?.sequence ?? this.readySequence(id)
  }
  get truncated(): boolean {
    return this.lostHistory
  }
  snapshot(scope: string): NativeWorkerRetirementSnapshot {
    return {
      records: [...this.records.values()]
        .filter((item) => item.scope === scope)
        .map((item) => ({ ...item.record })),
      ready: [...this.ready.values()]
        .filter((item) => item.scope === scope)
        .map((item) => ({ ...item.record })),
      truncated: this.lostHistory,
    }
  }
  clear(): void {
    this.records.clear()
    this.ready.clear()
    this.lostHistory = false
  }
}
