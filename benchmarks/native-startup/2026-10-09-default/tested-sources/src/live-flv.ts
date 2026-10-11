/** Incremental FLV framing with strict size limits for encoder output. */
export class LiveFlvParser {
  private pending = Buffer.allocUnsafe(13)
  private filled = 0
  private header = false

  /** Consumes fragments in order, awaiting each complete tag before continuing. */
  async push(chunk: Buffer, consume: (tag: Buffer) => Promise<void>): Promise<void> {
    let offset = 0

    while (offset < chunk.length) {
      const copied = chunk.copy(
        this.pending,
        this.filled,
        offset,
        offset + Math.min(chunk.length - offset, this.pending.length - this.filled),
      )

      offset += copied
      this.filled += copied

      if (this.filled < this.pending.length) return

      if (!this.header) {
        if (this.pending.toString('ascii', 0, 3) !== 'FLV' || this.pending.readUInt32BE(5) !== 9)
          throw new Error('Invalid live FLV header')
        this.header = true
      } else if (this.pending.length === 11) {
        const length = this.pending.readUIntBE(1, 3) + 15

        if (length > 8 * 1024 * 1024) throw new Error('Live encoded frame exceeds buffer limit')
        // Once the tag header is complete, allocate its bounded payload once.
        const tag = Buffer.allocUnsafe(length)
        this.pending.copy(tag)
        this.pending = tag
        continue
      } else {
        const tag = this.pending

        if (tag.readUInt32BE(tag.length - 4) !== tag.length - 4)
          throw new Error('Invalid live FLV tag')
        this.pending = Buffer.allocUnsafe(11)
        this.filled = 0
        await consume(tag)
        continue
      }

      this.pending = Buffer.allocUnsafe(11)
      this.filled = 0
    }
  }
}

export function flvTimestamp(tag: Buffer): number {
  return tag.readUIntBE(4, 3) + (tag[7] ?? 0) * 0x1000000
}

export function stampFlv(tag: Buffer, timestamp: number): Buffer {
  const result = Buffer.from(tag)
  const value = Math.max(0, Math.round(timestamp)) >>> 0
  result.writeUIntBE(value & 0xffffff, 4, 3)
  result[7] = value >>> 24

  return result
}

export const LIVE_FLV_HEADER = Buffer.from([70, 76, 86, 1, 5, 0, 0, 0, 9, 0, 0, 0, 0])
