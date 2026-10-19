// SPDX-License-Identifier: MIT
import { setImmediate as yieldTurn } from 'node:timers/promises'

interface RGBFrame {
  data: Buffer
  width: number
  height: number
  contentRect?: { x: number; y: number; width: number; height: number }
}
interface RGBPixels {
  data: Buffer
  width: number
  height: number
}
function prepare(
  frame: RGBFrame,
  allocate: (bytes: number) => Buffer = (bytes) => Buffer.allocUnsafe(bytes),
): { crop: NonNullable<RGBFrame['contentRect']>; rgb: RGBPixels } {
  const crop = frame.contentRect ?? { x: 0, y: 0, width: frame.width, height: frame.height }
  if (
    !Number.isInteger(frame.width) ||
    !Number.isInteger(frame.height) ||
    frame.width < 1 ||
    frame.height < 1 ||
    frame.width * frame.height > 3840 * 2160 ||
    frame.data.length < frame.width * frame.height * 4 ||
    !Number.isInteger(crop.x) ||
    !Number.isInteger(crop.y) ||
    !Number.isInteger(crop.width) ||
    !Number.isInteger(crop.height) ||
    crop.x < 0 ||
    crop.y < 0 ||
    crop.width < 1 ||
    crop.height < 1 ||
    crop.x + crop.width > frame.width ||
    crop.y + crop.height > frame.height
  )
    throw new Error('Invalid browser viewer frame geometry')

  const bytes = crop.width * crop.height * 3
  const buffer = allocate(bytes)
  if (buffer.length < bytes) throw new Error('Browser viewer RGB buffer is too small')
  return { crop, rgb: { data: buffer.subarray(0, bytes), width: crop.width, height: crop.height } }
}
function copyPixels(
  frame: RGBFrame,
  crop: NonNullable<RGBFrame['contentRect']>,
  data: Buffer,
  first: number,
  end: number,
): void {
  const source = frame.data
  let output = first * 3
  while (first < end) {
    const row = Math.floor(first / crop.width),
      column = first % crop.width
    const count = Math.min(end - first, crop.width - column)
    const start = ((crop.y + row) * frame.width + crop.x + column) * 4
    const limit = start + count * 4
    for (let input = start; input < limit; input += 4) {
      data[output++] = source[input + 2] ?? 0
      data[output++] = source[input + 1] ?? 0
      data[output++] = source[input] ?? 0
    }
    first += count
  }
}
/** Copy fitted page pixels from native packed BGRA into the JPEG codec's RGB input. */
export function nativeViewerRGB(frame: RGBFrame): RGBPixels {
  const { crop, rgb } = prepare(frame)
  copyPixels(frame, crop, rgb.data, 0, crop.width * crop.height)
  return rgb
}

/** Yield between bounded pixel chunks so large captures do not delay browser control. */
export async function nativeViewerRGBAsync(
  frame: RGBFrame,
  isCurrent: () => boolean,
  allocate?: (bytes: number) => Buffer,
): Promise<RGBPixels | undefined> {
  if (!isCurrent()) return undefined
  const { crop, rgb } = prepare(frame, allocate)
  const count = crop.width * crop.height,
    chunk = 65536
  for (let first = 0; first < count; first += chunk) {
    if (!isCurrent()) return undefined
    copyPixels(frame, crop, rgb.data, first, Math.min(first + chunk, count))
    if (first + chunk < count) await yieldTurn()
  }
  return isCurrent() ? rgb : undefined
}
