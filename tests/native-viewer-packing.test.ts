// SPDX-License-Identifier: MIT
import { setTimeout as delay } from 'node:timers/promises'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { type NativeBrowserController } from '../src/native-browser-controller.js'
import { type NativeVideoFrame } from '../src/native-browser.js'
import { type NativePage } from '../src/native-page.js'
import { createBrowserViewer, type NativeBrowserViewer } from '../src/native-viewer.js'

import type * as PixelMethods from '../src/native-viewer-pixels.js'
import type * as TimerMethods from 'node:timers/promises'

const probes = vi.hoisted(() => ({
  packingStarts: 0,
  packingActive: 0,
  packingMax: 0,
  yielded: undefined as (() => void) | undefined,
  hold: undefined as Promise<void> | undefined,
  inputs: [] as Buffer[],
  encode: vi.fn<() => Promise<Buffer>>(),
}))
vi.mock('node:timers/promises', async (original) => {
  const actual = await original<typeof TimerMethods>()
  return {
    ...actual,
    setImmediate: async () => {
      probes.yielded?.()
      await probes.hold
      return actual.setImmediate()
    },
  }
})
vi.mock('../src/native-viewer-pixels.js', async (original) => {
  const actual = await original<typeof PixelMethods>()
  return {
    ...actual,
    nativeViewerRGBAsync: async (...args: Parameters<typeof actual.nativeViewerRGBAsync>) => {
      ++probes.packingStarts
      probes.packingMax = Math.max(probes.packingMax, ++probes.packingActive)
      try {
        return await actual.nativeViewerRGBAsync(...args)
      } finally {
        --probes.packingActive
      }
    },
  }
})
// Isolate job ownership; exact real-codec bytes are covered by native-viewer-pixels.test.ts.
vi.mock('sharp', () => ({
  default: (input: Buffer) => {
    probes.inputs.push(input)
    return {
      jpeg() {
        return this
      },
      toBuffer: () => probes.encode(),
    }
  },
}))

function deferred() {
  let resolve = (): void => undefined
  const promise = new Promise<void>((done) => {
    resolve = done
  })
  return { promise, resolve }
}
function sameInputRange(a: Buffer | undefined, b: Buffer | undefined): boolean {
  return (
    !!a && !!b && a.buffer === b.buffer && a.byteOffset === b.byteOffset && a.length === b.length
  )
}
const viewers: NativeBrowserViewer[] = [],
  releases: (() => void)[] = []
beforeEach(() => {
  probes.packingStarts = probes.packingActive = probes.packingMax = 0
  probes.yielded = undefined
  probes.hold = undefined
  probes.inputs = []
  probes.encode.mockReset().mockResolvedValue(Buffer.from([255, 216, 255, 217]))
})
afterEach(async () => {
  for (const release of releases.splice(0)) release()
  await Promise.all(viewers.splice(0).map((viewer) => viewer.close()))
})
function pausePacking() {
  const started = deferred(),
    resume = deferred()
  probes.yielded = started.resolve
  probes.hold = resume.promise
  releases.push(resume.resolve)
  return { started: started.promise, resume: resume.resolve }
}
async function fixture(size = { width: 3840, height: 2160 }) {
  const processSignal = new AbortController().signal
  const data = Buffer.alloc(3840 * 2160 * 4, 255)
  const sources = [1, 2].map((pageId) => {
    const viewport = { ...size }
    const listeners = new Set<(frame: NativeVideoFrame) => void>()
    return {
      pageId,
      width: 3840,
      height: 2160,
      pixelFormat: 'bgra',
      processSignal,
      viewport,
      listeners,
      onFrame(listener: (frame: NativeVideoFrame) => void) {
        listeners.add(listener)
        return () => listeners.delete(listener)
      },
      runAutomation: (command: string) =>
        Promise.resolve(
          command === 'viewerIdle'
            ? { viewerIdleVersion: 1, idle: true }
            : { pressedKeys: 0, keyModifiers: 0, mouseButtons: 0, touchContactHeld: false },
        ),
    }
  })
  const pages = sources.map(
    (source) =>
      ({
        source,
        isClosed: () => false,
        viewportSize: () => source.viewport,
        context: () => ({}),
        url: () => 'https://example.test/',
      }) as object as NativePage,
  )
  const browser = {
    source: sources[0],
    isConnected: () => true,
    contexts: () => [{ pages: () => pages }],
  } as object as NativeBrowserController
  const page = pages[0]
  if (!page) throw new Error('Missing fixture page')
  const viewer = await createBrowserViewer({ browser, page, framesPerSecond: 120 })
  viewers.push(viewer)
  const url = new URL(viewer.url)
  const request = (path: string, body?: object) =>
    fetch(url.origin + path, {
      headers: {
        authorization: `Bearer ${url.hash.slice(1)}`,
        ...(body ? { 'content-type': 'application/json' } : {}),
      },
      ...(body ? { method: 'POST', body: JSON.stringify(body) } : {}),
    })
  const session = (await (await request('/session', {})).json()) as { clientId: string }
  const action = async (command: object) => {
    const response = await request('/action', {
      clientId: session.clientId,
      revision: viewer.state().revision,
      ...command,
    })
    expect(response.status).toBe(200)
    await response.arrayBuffer()
  }
  const emit = (index = 0, pixels = data) => {
    const source = sources[index]
    if (!source) throw new Error('Missing fixture source')
    const frame: NativeVideoFrame = {
      data: pixels,
      width: source.width,
      height: source.height,
      viewport: source.viewport,
      contentRect: { x: 0, y: 0, ...source.viewport },
      timestampMs: performance.now(),
      droppedFrames: 0,
      pixelFormat: 'bgra',
    }
    for (const listener of source.listeners) listener(frame)
  }
  const frame = (after = 0) =>
    request(`/frame?after=${after}&revision=${viewer.state().revision}&wait=1`)
  return { viewer, emit, frame, action, sources }
}

describe('native viewer cooperative packing ownership', () => {
  it('awaits and cancels packing on close before starting a codec job', async () => {
    const f = await fixture(),
      packing = pausePacking()
    f.emit()
    const poll = f.frame().catch(() => undefined)
    await packing.started
    let closed = false
    const closing = f.viewer.close().then(() => {
      closed = true
    })
    await delay(0)
    expect(closed).toBe(false)
    expect(f.sources[0]?.listeners.size).toBe(0)
    expect(probes.encode).not.toHaveBeenCalled()
    packing.resume()
    await closing
    await poll
    expect(probes.packingActive).toBe(0)
    expect(probes.encode).not.toHaveBeenCalled()
  })

  it('cancels an obsolete page conversion and serves the newly selected page', async () => {
    const f = await fixture(),
      packing = pausePacking()
    await f.action({ type: 'takeover' })
    f.emit()
    const poll = f.frame()
    await packing.started
    await f.action({ type: 'selectPage', pageId: 2 })
    packing.resume()
    expect((await poll).status).toBe(204)
    expect(probes.encode).not.toHaveBeenCalled()
    expect(f.viewer.state().pageId).toBe(2)
    f.emit(1)
    const next = await f.frame()
    expect(next.status).toBe(200)
    await next.arrayBuffer()
    expect(probes.encode).toHaveBeenCalledTimes(1)
    expect(probes.packingMax).toBe(1)
  })

  it('owns one job across packing yields and the pending codec despite client and frame churn', async () => {
    const f = await fixture(),
      packing = pausePacking(),
      encoded = deferred(),
      resumeCodec = deferred()
    releases.push(resumeCodec.resolve)
    probes.encode.mockImplementation(async () => {
      encoded.resolve()
      await resumeCodec.promise
      return Buffer.from([255, 216, 255, 217])
    })
    f.emit()
    const polls = Array.from({ length: 8 }, () => f.frame())
    await packing.started
    for (let n = 0; n < 8; ++n) f.emit()
    await delay(10)
    expect(probes.packingStarts).toBe(1)
    packing.resume()
    await encoded.promise
    const nextPixels = Buffer.alloc(3840 * 2160 * 4, 17)
    for (let n = 0; n < 8; ++n) f.emit(0, nextPixels)
    await delay(10)
    expect(probes.packingStarts).toBe(1)
    expect(probes.encode).toHaveBeenCalledTimes(1)
    expect(probes.inputs[0]?.subarray(0, 12)).toEqual(Buffer.alloc(12, 255))
    resumeCodec.resolve()
    const responses = await Promise.all(polls)
    expect(responses.every((response) => response.status === 200)).toBe(true)
    expect(new Set(responses.map((response) => response.headers.get('x-frame-sequence')))).toEqual(
      new Set(['1']),
    )
    await Promise.all(responses.map((response) => response.arrayBuffer()))
    expect(probes.packingMax).toBe(1)
    expect(probes.packingActive).toBe(0)
    f.emit(0, nextPixels)
    const next = await f.frame(1)
    await next.arrayBuffer()
    expect(sameInputRange(probes.inputs[1], probes.inputs[0])).toBe(true)
    expect(probes.inputs[1]?.subarray(0, 12)).toEqual(Buffer.alloc(12, 17))
  })

  it('drops scratch ownership after no-demand idle and when switching pages', async () => {
    const f = await fixture({ width: 64, height: 48 })
    await f.action({ type: 'takeover' })
    for (let after = 0; after < 2; ++after) {
      f.emit()
      await (await f.frame(after)).arrayBuffer()
    }
    expect(sameInputRange(probes.inputs[1], probes.inputs[0])).toBe(true)
    await delay(350)
    f.emit()
    await (await f.frame(2)).arrayBuffer()
    expect(sameInputRange(probes.inputs[2], probes.inputs[1])).toBe(false)
    await f.action({ type: 'selectPage', pageId: 2 })
    f.emit(1)
    await (await f.frame(3)).arrayBuffer()
    expect(sameInputRange(probes.inputs[3], probes.inputs[2])).toBe(false)
  })
})
