/** Incremental FLV framing with strict size limits for encoder output. */
export class LiveFlvParser {
  private pending = Buffer.alloc(0)
  private header = false

  async push(chunk: Buffer, consume: (tag: Buffer) => Promise<void>): Promise<void> {
    this.pending = Buffer.concat([this.pending, chunk])
    if (!this.header) {
      if (this.pending.length < 13) return
      if (this.pending.toString('ascii', 0, 3) !== 'FLV' || this.pending.readUInt32BE(5) !== 9)
        throw new Error('Invalid live FLV header')
      this.pending = this.pending.subarray(13)
      this.header = true
    }
    while (this.pending.length >= 11) {
      const length = this.pending.readUIntBE(1, 3) + 15
      if (length > 8 * 1024 * 1024) throw new Error('Live encoded frame exceeds buffer limit')
      if (this.pending.length < length) return
      const tag = Buffer.from(this.pending.subarray(0, length))
      this.pending = this.pending.subarray(length)
      if (tag.readUInt32BE(length - 4) !== length - 4) throw new Error('Invalid live FLV tag')
      await consume(tag)
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
