import { type NativePixelFormat } from './native-browser.js'

// Timestamped raw Matroska input lets FFmpeg encode only retained frames. A
// rawvideo pipe has implicit constant-rate timestamps and requires encoding
// every missed time slot, even when the browser already supplied newer pixels.
function size(value: number): Buffer {
  for (let bytes = 1; bytes <= 8; bytes++) {
    if (value >= 2 ** (7 * bytes) - 1) continue
    const result = Buffer.alloc(bytes)
    let remaining = BigInt(value)
    for (let i = bytes - 1; i >= 0; i--) {
      result[i] = Number(remaining & 255n)
      remaining >>= 8n
    }
    result[0] = (result[0] ?? 0) | (1 << (8 - bytes))
    return result
  }
  throw new Error('Native video element is too large')
}

function element(id: string, data: Buffer): Buffer {
  return Buffer.concat([Buffer.from(id, 'hex'), size(data.length), data])
}

function integer(value: number): Buffer {
  if (!Number.isSafeInteger(value) || value < 0) throw new Error('Invalid native video timestamp')
  let bytes = 1
  while (value >= 2 ** (8 * bytes)) bytes++
  const data = Buffer.alloc(bytes)
  data.writeUIntBE(value, 0, bytes)
  return data
}

/** Small Matroska headers; frame pixels are written separately without concatenation. */
export class NativeVideoContainer {
  readonly header: Buffer
  readonly frameBytes: number

  constructor(width: number, height: number, pixelFormat: NativePixelFormat, fps: number) {
    this.frameBytes = width * height * (pixelFormat === 'bgra' ? 4 : 1.5)
    const bitmap = Buffer.alloc(40)
    bitmap.writeUInt32LE(40, 0)
    bitmap.writeInt32LE(width, 4)
    // Uncompressed RGB DIBs need a negative height for top-left row order.
    bitmap.writeInt32LE(pixelFormat === 'bgra' ? -height : height, 8)
    bitmap.writeUInt16LE(1, 12)
    bitmap.writeUInt16LE(pixelFormat === 'bgra' ? 32 : 12, 14)
    if (pixelFormat === 'i420') bitmap.write('I420', 16, 'ascii')
    bitmap.writeUInt32LE(this.frameBytes, 20)
    const ebml = element(
      '1a45dfa3',
      Buffer.concat([
        element('4286', integer(1)),
        element('42f7', integer(1)),
        element('42f2', integer(4)),
        element('42f3', integer(8)),
        element('4282', Buffer.from('matroska')),
        element('4287', integer(4)),
        element('4285', integer(2)),
      ]),
    )
    const info = element(
      '1549a966',
      Buffer.concat([
        element('2ad7b1', integer(1_000_000)),
        element('4d80', Buffer.from('SuiteCut')),
        element('5741', Buffer.from('SuiteCut')),
      ]),
    )
    const track = element(
      'ae',
      Buffer.concat([
        element('d7', integer(1)),
        element('73c5', integer(1)),
        element('83', integer(1)),
        element('9c', integer(0)),
        element('86', Buffer.from('V_MS/VFW/FOURCC')),
        element('63a2', bitmap),
        element('23e383', integer(Math.round(1_000_000_000 / fps))),
        element(
          'e0',
          Buffer.concat([element('b0', integer(width)), element('ba', integer(height))]),
        ),
      ]),
    )
    // A stream cannot seek back to set Segment size. Each frame's Cluster has
    // an explicit size, so packet boundaries remain independently verifiable.
    this.header = Buffer.concat([
      ebml,
      Buffer.from('1853806701ffffffffffffff', 'hex'),
      info,
      element('1654ae6b', track),
    ])
  }

  frameHeader(timestampMs: number): Buffer {
    const timestamp = element('e7', integer(Math.round(timestampMs)))
    const block = Buffer.concat([
      Buffer.from('a3', 'hex'),
      size(this.frameBytes + 4),
      Buffer.from([0x81, 0, 0, 0x80]),
    ])
    return Buffer.concat([
      Buffer.from('1f43b675', 'hex'),
      size(timestamp.length + block.length + this.frameBytes),
      timestamp,
      block,
    ])
  }
}
