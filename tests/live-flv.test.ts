import { expect, it } from 'vitest'

import { LIVE_FLV_HEADER, LiveFlvParser, flvTimestamp, stampFlv } from '../src/live-flv.js'

it('parses fragmented FLV and preserves payloads across extended timestamp rewriting', async () => {
  const tag = Buffer.alloc(20)
  tag[0] = 9
  tag.writeUIntBE(5, 1, 3)
  tag.set([0x17, 1, 0, 0, 0], 11)
  tag.writeUInt32BE(16, 16)
  const stamped = stampFlv(tag, 0x12345678)
  expect(flvTimestamp(stamped)).toBe(0x12345678)
  expect(stamped.subarray(11)).toEqual(tag.subarray(11))
  const bytes = Buffer.concat([LIVE_FLV_HEADER, stamped, tag])
  const parsed: Buffer[] = []
  const parser = new LiveFlvParser()

  for (const byte of bytes)
    await parser.push(Buffer.from([byte]), (t) => {
      parsed.push(t)

      return Promise.resolve()
    })
  expect(parsed).toEqual([stamped, tag])
})

it('rejects malformed headers, oversized tags, and corrupt trailing sizes', async () => {
  await expect(new LiveFlvParser().push(Buffer.alloc(13), () => Promise.resolve())).rejects.toThrow(
    'header',
  )
  const tag = Buffer.alloc(11)
  tag.writeUIntBE(0xffffff, 1, 3)
  await expect(
    new LiveFlvParser().push(Buffer.concat([LIVE_FLV_HEADER, tag]), () => Promise.resolve()),
  ).rejects.toThrow('buffer limit')
  await expect(
    new LiveFlvParser().push(Buffer.concat([LIVE_FLV_HEADER, Buffer.alloc(15)]), () =>
      Promise.resolve(),
    ),
  ).rejects.toThrow('tag')
})

it.each([1, 7, 11, 13, 1024, 16 * 1024, 64 * 1024])(
  'preserves multiple tags split into %i-byte fragments',
  async (fragmentSize) => {
    const tags = [0, 5, 64 * 1024].map((length, index) => {
      const tag = Buffer.alloc(length + 15, index)
      tag[0] = index === 1 ? 8 : 9
      tag.writeUIntBE(length, 1, 3)
      tag.writeUInt32BE(tag.length - 4, tag.length - 4)

      return tag
    })

    const bytes = Buffer.concat([LIVE_FLV_HEADER, ...tags])
    const parsed: Buffer[] = []
    const parser = new LiveFlvParser()

    for (let offset = 0; offset < bytes.length; offset += fragmentSize) {
      await parser.push(bytes.subarray(offset, offset + fragmentSize), (tag) => {
        parsed.push(tag)

        return Promise.resolve()
      })
    }

    expect(parsed).toEqual(tags)
  },
)

it('owns buffered bytes and waits for the consumer before delivering the next tag', async () => {
  const tag = Buffer.alloc(20, 1)
  tag.writeUIntBE(5, 1, 3)
  tag.writeUInt32BE(16, 16)
  const parser = new LiveFlvParser()
  const prefix = Buffer.concat([LIVE_FLV_HEADER, tag.subarray(0, 14)])
  const parsed: Buffer[] = []
  await parser.push(prefix, () => {
    throw new Error('Incomplete tag was emitted')
  })
  prefix.fill(0)

  let release: () => void = () => {
    throw new Error('Consumer has not started')
  }

  const blocked = new Promise<void>((resolve) => {
    release = resolve
  })

  const pending = parser.push(Buffer.concat([tag.subarray(14), tag]), async (received) => {
    parsed.push(received)
    await blocked
  })

  expect(parsed).toEqual([tag])
  release()
  await pending
  expect(parsed).toEqual([tag, tag])
})

it('accepts a tag at the size limit and rejects the first byte beyond it', async () => {
  const tag = Buffer.alloc(8 * 1024 * 1024)
  tag.writeUIntBE(tag.length - 15, 1, 3)
  tag.writeUInt32BE(tag.length - 4, tag.length - 4)
  const parsed: Buffer[] = []
  await new LiveFlvParser().push(Buffer.concat([LIVE_FLV_HEADER, tag]), (received) => {
    parsed.push(received)

    return Promise.resolve()
  })
  expect(parsed).toHaveLength(1)
  expect(parsed[0]?.equals(tag)).toBe(true)
  tag.writeUIntBE(tag.length - 14, 1, 3)
  await expect(
    new LiveFlvParser().push(Buffer.concat([LIVE_FLV_HEADER, tag.subarray(0, 11)]), () => {
      throw new Error('Oversized tag was emitted')
    }),
  ).rejects.toThrow('buffer limit')
})
