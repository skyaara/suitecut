import imageSharp from 'sharp'
import { afterEach, expect, it, vi } from 'vitest'

import { createLiveCamera } from '../src/live-zoom.js'
import { SuiteCutCaptureOptionsSchema } from '../src/schemas.js'
import { type SuiteCutZoomEvent } from '../src/types.js'
import { parseCaptureOptions } from '../src/validation.js'

afterEach(() => vi.restoreAllMocks())

it('crops JPEG pixels on the selected page and releases the camera on stop', async () => {
  let now = 0
  vi.spyOn(performance, 'now').mockImplementation(() => now)
  const camera = createLiveCamera()
  const input = await imageSharp(
    Buffer.from(
      '<svg width="200" height="100"><rect width="200" height="100" fill="black"/><rect x="80" y="35" width="40" height="30" fill="yellow"/></svg>',
    ),
  )
    .jpeg()
    .toBuffer()
  const event: SuiteCutZoomEvent = {
    id: 'zoom',
    type: 'zoom',
    pageId: 'main',
    atMs: 0,
    rect: { x: 80, y: 35, width: 40, height: 30 },
    viewport: { width: 200, height: 100, scrollX: 0, scrollY: 1000 },
    options: {
      scale: 1.25,
      paddingPx: 0,
      enter: { durationMs: 300 },
      holdMs: 10000,
      exit: { durationMs: 300 },
    },
  }
  const finished = camera.zoom(event).catch(() => undefined)
  try {
    now = 500
    expect(await camera.render(input, 'other')).toBe(input)
    const output = await camera.render(input, 'main')
    expect(output).not.toEqual(input)
    expect(await camera.render(input, 'main')).toBe(output)
    const { data, info } = await imageSharp(output).raw().toBuffer({ resolveWithObject: true })
    let left = info.width,
      right = -1
    for (let x = 0; x < info.width; x++) {
      const index = (50 * info.width + x) * info.channels
      if (
        (data[index] ?? 0) > 180 &&
        (data[index + 1] ?? 0) > 180 &&
        (data[index + 2] ?? 255) < 80
      ) {
        left = Math.min(left, x)
        right = Math.max(right, x)
      }
    }
    expect(right - left + 1).toBeGreaterThanOrEqual(48)
    now = 20_000
    expect(await camera.render(input, 'main')).toBe(input)
  } finally {
    camera.stop()
    await finished
  }
  expect(await camera.render(input, 'main')).toBe(input)
})

it('accepts a stream destination without a recording flag and validates retry limits', () => {
  expect(() =>
    SuiteCutCaptureOptionsSchema.parse({
      record: true,
      stream: { url: 'rtmp://localhost/live/key' },
    }),
  ).toThrow()
  expect(
    parseCaptureOptions({
      stream: { url: 'rtmp://localhost/live/key', reconnect: { maxAttempts: 0 } },
    }).stream?.url,
  ).toBe('rtmp://localhost/live/key')
  expect(() =>
    parseCaptureOptions({
      stream: { url: 'rtmp://localhost/live/key', reconnect: { maxAttempts: -1 } },
    }),
  ).toThrow()
})
