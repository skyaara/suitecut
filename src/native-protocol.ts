import * as z from 'zod'

export const NATIVE_PROTOCOL_VERSION = 1
export const MAX_NATIVE_PAYLOAD = 3840 * 2160 * 4
const MAX_HEADER = 16_384

const base = {
  version: z.literal(NATIVE_PROTOCOL_VERSION),
  bytes: z.number().int().min(0).max(MAX_NATIVE_PAYLOAD),
}
const timestamp = z.number().finite().nonnegative()
export const NativePacketSchema = z.discriminatedUnion('type', [
  z.strictObject({
    ...base,
    type: z.literal('ready'),
    timestampMs: timestamp,
    cefVersion: z.string().max(128),
  }),
  z.strictObject({
    ...base,
    type: z.literal('frame'),
    pixelFormat: z.enum(['bgra', 'i420']).default('bgra'),
    timestampMs: timestamp,
    width: z.number().int().min(1).max(3840),
    height: z.number().int().min(1).max(2160),
    dropped: z.number().int().nonnegative(),
  }),
  z.strictObject({
    ...base,
    type: z.literal('audio'),
    timestampMs: timestamp,
    frames: z.number().int().min(1).max(4096),
  }),
  z.strictObject({
    ...base,
    type: z.literal('response'),
    id: z.number().int().positive(),
    ok: z.boolean(),
    error: z.string().max(4096).optional(),
  }),
  z.strictObject({ ...base, type: z.literal('error'), error: z.string().max(4096) }),
])
export type NativePacket = z.infer<typeof NativePacketSchema>

/** Incrementally decodes length-prefixed packets with bounded allocation and no quadratic concatenation. */
export class NativePacketDecoder {
  private target: Buffer = Buffer.alloc(4)
  private offset = 0
  private phase: 'length' | 'header' | 'payload' = 'length'
  private header: NativePacket | undefined

  constructor(private readonly receive: (header: NativePacket, payload: Buffer) => void) {}

  push(chunk: Buffer): void {
    let cursor = 0
    while (cursor < chunk.length) {
      const count = Math.min(this.target.length - this.offset, chunk.length - cursor)
      chunk.copy(this.target, this.offset, cursor, cursor + count)
      cursor += count
      this.offset += count
      if (this.offset !== this.target.length) continue
      if (this.phase === 'length') {
        const size = this.target.readUInt32LE()
        if (size < 2 || size > MAX_HEADER) throw new Error('Invalid native packet header length')
        this.target = Buffer.allocUnsafe(size)
        this.phase = 'header'
      } else if (this.phase === 'header') {
        const header = NativePacketSchema.parse(JSON.parse(this.target.toString('utf8')))
        if (header.type === 'frame') {
          if (header.pixelFormat === 'i420' && (header.width % 2 || header.height % 2))
            throw new Error('I420 frames require even dimensions')
          if (
            header.bytes !==
            header.width * header.height * (header.pixelFormat === 'bgra' ? 4 : 1.5)
          )
            throw new Error('Invalid native frame length')
        }
        if (header.type === 'audio' && header.bytes !== header.frames * 4)
          throw new Error('Invalid native PCM packet length')
        if ((header.type === 'ready' || header.type === 'error') && header.bytes !== 0)
          throw new Error('Unexpected native control payload')
        if (header.type === 'response' && header.bytes > 1_048_576)
          throw new Error('Native response exceeds 1 MiB')
        if (header.bytes === 0) {
          this.reset()
          this.receive(header, Buffer.alloc(0))
        } else {
          this.header = header
          this.target = Buffer.allocUnsafe(header.bytes)
          this.phase = 'payload'
        }
      } else {
        const header = this.header
        const payload = this.target
        this.reset()
        if (header) this.receive(header, payload)
      }
      this.offset = 0
    }
  }

  end(): void {
    if (this.phase !== 'length' || this.offset !== 0) throw new Error('Truncated native packet')
  }

  private reset(): void {
    this.target = Buffer.alloc(4)
    this.header = undefined
    this.phase = 'length'
  }
}
