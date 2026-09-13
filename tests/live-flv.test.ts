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
