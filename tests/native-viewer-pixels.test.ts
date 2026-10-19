// SPDX-License-Identifier: MIT
import imageEncoder from 'sharp'
import { describe, expect, it } from 'vitest'

import { nativeViewerRGB, nativeViewerRGBAsync } from '../src/native-viewer-pixels.js'

describe('native viewer fitted BGRA pixels', () => {
  it('matches the previous native transform and JPEG for randomized crops and unaligned buffers', async () => {
    let seed = 0x50784321
    const random = (limit: number): number => {
      seed ^= seed << 13
      seed ^= seed >>> 17
      seed ^= seed << 5
      return (seed >>> 0) % limit
    }
    for (let n = 0; n < 48; ++n) {
      const width = 1 + random(91)
      const height = 1 + random(67)
      const offset = 1 + random(7)
      const data = Buffer.alloc(width * height * 4 + offset).subarray(offset)
      for (let i = 0; i < data.length; i += 4) {
        data[i] = random(256)
        data[i + 1] = random(256)
        data[i + 2] = random(256)
        data[i + 3] = n % 3 === 0 ? 0 : n % 3 === 1 ? 255 : random(256)
      }
      const original = Buffer.from(data)
      const x = random(width),
        y = random(height)
      const crop =
        n % 4 === 0
          ? { x: 0, y: 0, width, height }
          : { x, y, width: 1 + random(width - x), height: 1 + random(height - y) }
      const frame = { data, width, height, ...(n % 4 === 0 ? {} : { contentRect: crop }) }
      const actual = nativeViewerRGB(frame)
      const previous = () =>
        imageEncoder(data, { raw: { width, height, channels: 4 } })
          .extract({ left: crop.x, top: crop.y, width: crop.width, height: crop.height })
          .recomb([
            [0, 0, 1],
            [0, 1, 0],
            [1, 0, 0],
          ])
          .removeAlpha()
      const reference = await previous().raw().toBuffer({ resolveWithObject: true })
      expect(actual.width).toBe(crop.width)
      expect(actual.height).toBe(crop.height)
      expect(actual.data.length).toBe(crop.width * crop.height * 3)
      expect(reference.info.channels).toBe(3)
      expect(actual.data).toEqual(reference.data)
      expect(await nativeViewerRGBAsync(frame, () => true)).toEqual(actual)
      const jpegOptions = { quality: 75, chromaSubsampling: '4:2:0' as const }
      const expectedJPEG = await previous().jpeg(jpegOptions).toBuffer()
      const actualJPEG = await imageEncoder(actual.data, {
        raw: { width: actual.width, height: actual.height, channels: 3 },
      })
        .jpeg(jpegOptions)
        .toBuffer()
      expect(actualJPEG).toEqual(expectedJPEG)
      expect(data).toEqual(original)
    }
  })

  it('copies only the fitted page and discards alpha without modifying shared native pixels', () => {
    const data = Buffer.alloc(3840 * 2160 * 4, 255)
    const offset = (127 * 3840 + 913) * 4
    data.set([7, 19, 251, 0, 203, 41, 13, 128], offset)
    const result = nativeViewerRGB({
      data,
      width: 3840,
      height: 2160,
      contentRect: { x: 913, y: 127, width: 2, height: 1 },
    })
    expect(result).toEqual({ data: Buffer.from([251, 19, 7, 13, 41, 203]), width: 2, height: 1 })
    expect(data.subarray(offset, offset + 8)).toEqual(
      Buffer.from([7, 19, 251, 0, 203, 41, 13, 128]),
    )
  })

  it('rejects malformed geometry before an unsafe output allocation', () => {
    const frame = { data: Buffer.alloc(16), width: 2, height: 2 }
    for (const contentRect of [
      { x: -1, y: 0, width: 1, height: 1 },
      { x: 0, y: 0, width: 0, height: 1 },
      { x: 1, y: 1, width: 2, height: 2 },
      { x: 0.5, y: 0, width: 1, height: 1 },
    ])
      expect(() => nativeViewerRGB({ ...frame, contentRect })).toThrow(
        'Invalid browser viewer frame geometry',
      )
    expect(() => nativeViewerRGB({ ...frame, data: Buffer.alloc(15) })).toThrow()
    expect(() => nativeViewerRGB({ ...frame, width: 3841, height: 2160 })).toThrow()
  })

  it('yields on large crops and cancels between chunks without changing source pixels', async () => {
    const frame = { data: Buffer.alloc(257 * 263 * 4, 19), width: 257, height: 263 }
    let yielded = false
    setImmediate(() => {
      yielded = true
    })
    expect(await nativeViewerRGBAsync(frame, () => true)).toEqual(nativeViewerRGB(frame))
    expect(yielded).toBe(true)
    let checks = 0
    expect(await nativeViewerRGBAsync(frame, () => ++checks < 3)).toBeUndefined()
    expect(checks).toBe(3)
    expect(frame.data.equals(Buffer.alloc(frame.data.length, 19))).toBe(true)
    expect(
      await nativeViewerRGBAsync({ ...frame, data: Buffer.alloc(0) }, () => false),
    ).toBeUndefined()
  })

  it('can reuse one RGB workspace after real sharp jobs settle without stale cached pixels', async () => {
    const workspace = Buffer.alloc(73 * 51 * 3)
    let seed = 0x71325
    const random = (max: number): number => {
      seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0
      return seed % max
    }
    for (let n = 0; n < 24; ++n) {
      const offset = 1 + (n % 7)
      const data = Buffer.alloc(73 * 51 * 4 + offset).subarray(offset)
      for (let i = 0; i < data.length; ++i) data[i] = random(256)
      const crop = { x: random(33), y: random(21), width: 1 + random(40), height: 1 + random(30) }
      const rgb = await nativeViewerRGBAsync(
        { data, width: 73, height: 51, contentRect: crop },
        () => true,
        () => workspace,
      )
      expect(rgb?.data.buffer).toBe(workspace.buffer)
      if (!rgb) throw new Error('Unexpected cancelled pixels')
      const actual = await imageEncoder(rgb.data, {
        raw: { width: rgb.width, height: rgb.height, channels: 3 },
      })
        .jpeg({ quality: 75, chromaSubsampling: '4:2:0' })
        .toBuffer()
      // Reuse immediately after settlement, while libvips' operation cache remains enabled.
      workspace.fill(n)
      const expected = await imageEncoder(data, { raw: { width: 73, height: 51, channels: 4 } })
        .extract({ left: crop.x, top: crop.y, width: crop.width, height: crop.height })
        .recomb([
          [0, 0, 1],
          [0, 1, 0],
          [1, 0, 0],
        ])
        .removeAlpha()
        .jpeg({ quality: 75, chromaSubsampling: '4:2:0' })
        .toBuffer()
      expect(actual).toEqual(expected)
    }
  })
})
