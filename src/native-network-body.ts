// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

// Bodies are as large as the page sends. Each is kept until its page closes.
const BodyPacket = z.strictObject({
  bodyId: z.string().regex(/^[1-9][0-9]{0,19}$/u),
  offset: z.number().int().min(0),
  totalBytes: z.number().int().min(0),
  canceled: z.boolean().optional(),
})
export type NativeNetworkBodyPacket = z.input<typeof BodyPacket>
export type NativeNetworkBodyAck = (bodyId: string, stage: 'received' | 'taken') => Promise<void>
interface BodyOwner {
  bytes: Buffer
  offset: number
  ready: boolean
}
/** A process transport shares body replay across pages, indexed by receiver scope. */
export class NativeNetworkBodyArchive {
  private serial = 0
  private readonly scopes = new Map<number, Map<string, Buffer>>()
  scope(): number {
    return ++this.serial
  }
  get(scope: number, id: string): Buffer | undefined {
    return this.scopes.get(scope)?.get(id)
  }
  put(scope: number, id: string, bytes: Buffer): void {
    let entries = this.scopes.get(scope)
    if (!entries) {
      entries = new Map()
      this.scopes.set(scope, entries)
    }
    entries.set(id, bytes)
  }
  remove(scope: number, id: string): void {
    const entries = this.scopes.get(scope)
    if (!entries) return
    entries.delete(id)
    if (!entries.size) this.scopes.delete(scope)
  }
  clear(scope: number): void {
    this.scopes.delete(scope)
  }
  diagnostics(scope: number): { bodies: number; retainedBytes: number } {
    const entries = this.scopes.get(scope)
    let retainedBytes = 0
    for (const bytes of entries?.values() ?? []) retainedBytes += bytes.length
    return { bodies: entries?.size ?? 0, retainedBytes }
  }
}
/** Byte receiver for native-owned request-body publications. */
export class NativeNetworkBodyReceiver {
  private readonly owners = new Map<string, BodyOwner>()
  private readonly consumed = new Map<string, WeakRef<Buffer>>()
  private readonly scope: number
  private retained = 0
  private closed = false
  constructor(
    private readonly acknowledge: NativeNetworkBodyAck,
    private readonly failure: (error: Error) => void,
    private readonly archive = new NativeNetworkBodyArchive(),
  ) {
    this.scope = archive.scope()
  }
  diagnostics(): { owners: number; retainedBytes: number } {
    return { owners: this.owners.size, retainedBytes: this.retained }
  }
  archiveDiagnostics(): { bodies: number; retainedBytes: number } {
    return this.archive.diagnostics(this.scope)
  }
  receive(packet: NativeNetworkBodyPacket, chunk: Buffer): void {
    if (this.closed) return
    const info = BodyPacket.parse(packet)
    if (info.canceled) {
      if (info.offset || info.totalBytes || chunk.length)
        throw new Error('Invalid canceled native network body packet')
      const owner = this.owners.get(info.bodyId)
      if (owner) this.retained -= owner.bytes.length
      this.owners.delete(info.bodyId)
      this.consumed.delete(info.bodyId)
      this.archive.remove(this.scope, info.bodyId)
      return
    }
    let owner = this.owners.get(info.bodyId)
    if (!owner) {
      if (info.offset || this.consumed.has(info.bodyId))
        throw new Error('Native network body transfer starts at an invalid offset')
      if (chunk.length > info.totalBytes || (!chunk.length && info.totalBytes !== 0))
        throw new Error('Native network body transfer offset or size mismatch')
      owner = { bytes: Buffer.allocUnsafe(info.totalBytes), offset: 0, ready: false }
      this.owners.set(info.bodyId, owner)
      this.retained += info.totalBytes
    }
    if (
      owner.ready ||
      owner.bytes.length !== info.totalBytes ||
      owner.offset !== info.offset ||
      chunk.length > owner.bytes.length - owner.offset ||
      (!chunk.length && info.totalBytes !== 0)
    )
      throw new Error('Native network body transfer offset or size mismatch')
    chunk.copy(owner.bytes, owner.offset)
    owner.offset += chunk.length
    if (owner.offset === owner.bytes.length) {
      owner.ready = true
      this.sendAcknowledgment(info.bodyId, 'received')
    }
  }
  consume(bodyId: string): void {
    if (this.owners.has(bodyId)) this.take(bodyId)
  }
  take(bodyId: string): Buffer {
    if (this.closed) throw new Error('Native network body receiver is closed')
    const owner = this.owners.get(bodyId)
    if (!owner) {
      const existing = this.archive.get(this.scope, bodyId) ?? this.consumed.get(bodyId)?.deref()
      if (existing) return existing
      throw new Error('Native network request body bytes are unavailable')
    }
    if (!owner.ready) throw new Error('Native network request body publication is incomplete')
    this.retained -= owner.bytes.length
    this.owners.delete(bodyId)
    this.consumed.set(bodyId, new WeakRef(owner.bytes))
    // Native receipt can precede a control reply or subscriber binding. Keep a
    // replay archive until the page closes.
    this.archive.put(this.scope, bodyId, owner.bytes)
    this.sendAcknowledgment(bodyId, 'taken')
    return owner.bytes
  }
  close(): void {
    this.closed = true
    this.owners.clear()
    this.consumed.clear()
    this.archive.clear(this.scope)
    this.retained = 0
  }
  private sendAcknowledgment(bodyId: string, stage: 'received' | 'taken'): void {
    void this.acknowledge(bodyId, stage).catch((error: UntrustedInput) => {
      if (this.closed) return
      this.close()
      this.failure(
        error instanceof Error ? error : new Error('Native network body acknowledgment failed'),
      )
    })
  }
}
