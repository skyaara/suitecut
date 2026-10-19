import { Buffer } from 'node:buffer'

import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

const MAX_BODY = 2 * 1024 * 1024
const MAX_RETAINED = 16 * 1024 * 1024
const MAX_OWNERS = 128
const MAX_CHUNK = 24 * 1024
const BodyPacket = z.strictObject({
  bodyId: z.string().regex(/^[1-9][0-9]{0,19}$/u),
  offset: z.number().int().min(0).max(MAX_BODY),
  totalBytes: z.number().int().min(0).max(MAX_BODY),
  canceled: z.boolean().optional(),
})
export type NativeNetworkBodyPacket = z.input<typeof BodyPacket>
export type NativeNetworkBodyAck = (bodyId: string, stage: 'received' | 'taken') => Promise<void>
interface BodyOwner {
  bytes: Buffer
  offset: number
  ready: boolean
}
/** A process transport shares bounded pending bytes and replay across pages. */
export class NativeNetworkBodyArchive {
  private serial = 0
  private retained = 0
  private pendingBytes = 0
  private pendingOwners = 0
  private readonly entries = new Map<string, Buffer>()
  scope(): number {
    return ++this.serial
  }
  reserve(bytes: number): boolean {
    if (this.pendingOwners >= MAX_OWNERS || bytes > MAX_RETAINED - this.pendingBytes) return false
    ++this.pendingOwners
    this.pendingBytes += bytes
    return true
  }
  release(bytes: number): void {
    --this.pendingOwners
    this.pendingBytes -= bytes
  }
  get(scope: number, id: string): Buffer | undefined {
    return this.entries.get(`${scope}:${id}`)
  }
  put(scope: number, id: string, bytes: Buffer): void {
    const key = `${scope}:${id}`
    this.retained -= this.entries.get(key)?.length ?? 0
    this.entries.set(key, bytes)
    this.retained += bytes.length
    while (this.entries.size > 4096 || this.retained > MAX_RETAINED) {
      const oldest = this.entries.keys().next().value
      if (oldest === undefined) break
      this.retained -= this.entries.get(oldest)?.length ?? 0
      this.entries.delete(oldest)
    }
  }
  remove(scope: number, id: string): void {
    const key = `${scope}:${id}`
    this.retained -= this.entries.get(key)?.length ?? 0
    this.entries.delete(key)
  }
  clear(scope: number): void {
    for (const key of this.entries.keys())
      if (key.startsWith(`${scope}:`)) {
        this.retained -= this.entries.get(key)?.length ?? 0
        this.entries.delete(key)
      }
  }
  diagnostics(scope: number): { bodies: number; retainedBytes: number } {
    let bodies = 0,
      retainedBytes = 0
    for (const [key, bytes] of this.entries)
      if (key.startsWith(`${scope}:`)) {
        ++bodies
        retainedBytes += bytes.length
      }
    return { bodies, retainedBytes }
  }
}
/** Bounded byte receiver for native-owned request-body publications. */
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
      if (owner) {
        this.retained -= owner.bytes.length
        this.archive.release(owner.bytes.length)
      }
      this.owners.delete(info.bodyId)
      this.consumed.delete(info.bodyId)
      this.archive.remove(this.scope, info.bodyId)
      return
    }
    if (chunk.length > MAX_CHUNK) throw new Error('Native network body chunk exceeds 24 KiB')
    let owner = this.owners.get(info.bodyId)
    if (!owner) {
      if (info.offset || this.consumed.has(info.bodyId))
        throw new Error('Native network body transfer starts at an invalid offset')
      if (chunk.length > info.totalBytes || (!chunk.length && info.totalBytes !== 0))
        throw new Error('Native network body transfer offset or size mismatch')
      if (this.owners.size >= MAX_OWNERS || info.totalBytes > MAX_RETAINED - this.retained)
        throw new Error('Native network body receiver capacity exceeded')
      if (!this.archive.reserve(info.totalBytes))
        throw new Error('Native process network body receiver capacity exceeded')
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
    this.archive.release(owner.bytes.length)
    this.owners.delete(bodyId)
    this.consumed.set(bodyId, new WeakRef(owner.bytes))
    // Native receipt can precede a control reply or subscriber binding. Keep a
    // separately bounded replay archive until canonical Request objects exist.
    this.archive.put(this.scope, bodyId, owner.bytes)
    // Weak replay entries do not own payload bytes. Dereferencing every entry
    // here both makes each take linear and keeps otherwise collectible bodies
    // alive until the end of this JavaScript turn. Bound identities by age;
    // dereference a payload only when its actual replay is requested.
    while (this.consumed.size > 4096) {
      const oldest = this.consumed.keys().next().value
      if (oldest === undefined) break
      this.consumed.delete(oldest)
    }
    this.sendAcknowledgment(bodyId, 'taken')
    return owner.bytes
  }
  close(): void {
    this.closed = true
    for (const owner of this.owners.values()) this.archive.release(owner.bytes.length)
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
