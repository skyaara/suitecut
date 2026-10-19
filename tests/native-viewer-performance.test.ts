// SPDX-License-Identifier: MIT
import { setTimeout as delay } from 'node:timers/promises'

import { afterEach, describe, expect, it } from 'vitest'

import { type NativeBrowserController } from '../src/native-browser-controller.js'
import { type NativeVideoFrame } from '../src/native-browser.js'
import { type NativePage } from '../src/native-page.js'
import { createBrowserViewer, type NativeBrowserViewer } from '../src/native-viewer.js'

const viewers: NativeBrowserViewer[] = []
afterEach(async () => {
  await Promise.all(viewers.splice(0).map((viewer) => viewer.close()))
})
async function fixture(framesPerSecond = 120) {
  const signal = new AbortController().signal
  const listeners = new Set<(frame: NativeVideoFrame) => void>()
  let current: NativeVideoFrame | undefined
  const source = {
    width: 64,
    height: 48,
    viewport: { width: 64, height: 48 },
    pixelFormat: 'bgra',
    processSignal: signal,
    pageId: 1,
    onFrame(listener: (frame: NativeVideoFrame) => void) {
      listeners.add(listener)
      if (current) listener(current)
      return () => listeners.delete(listener)
    },
  }
  const page = {
    source,
    isClosed: () => false,
    viewportSize: () => source.viewport,
    context: () => ({}),
    url: () => 'https://example.test/',
  } as object as NativePage
  const browser = {
    source,
    isConnected: () => true,
    contexts: () => [{ pages: () => [page] }],
  } as object as NativeBrowserController
  const viewer = await createBrowserViewer({ browser, page, framesPerSecond })
  viewers.push(viewer)
  const link = new URL(viewer.url)
  const request = (path: string, signal?: AbortSignal) =>
    fetch(link.origin + path, {
      headers: { authorization: `Bearer ${link.hash.slice(1)}` },
      ...(signal ? { signal } : {}),
    })
  const emit = () => {
    current = {
      data: Buffer.alloc(64 * 48 * 4, 255),
      pixelFormat: 'bgra',
      width: 64,
      height: 48,
      timestampMs: performance.now(),
      droppedFrames: 0,
      viewport: source.viewport,
      contentRect: { x: 0, y: 0, width: 64, height: 48 },
    }
    for (const listener of listeners) listener(current)
  }
  const frame = (after = 0, wait = true, signal?: AbortSignal) =>
    request(
      `/frame?after=${after}&revision=${viewer.state().revision}${wait ? '&wait=1' : ''}`,
      signal,
    )
  return { viewer, request, emit, frame, listeners, source }
}

describe('native viewer frame demand and lifecycle', () => {
  it('does no JPEG work before demand or after the requested frame is delivered', async () => {
    const f = await fixture()
    f.emit()
    await delay(30)
    expect(f.viewer.state().frameSequence).toBe(0)
    const first = await f.frame()
    expect(first.status).toBe(200)
    expect(first.headers.get('x-frame-sequence')).toBe('1')
    expect((await first.arrayBuffer()).byteLength).toBeGreaterThan(0)
    for (let i = 0; i < 20; ++i) {
      f.emit()
      await delay(5)
    }
    expect(f.viewer.state().frameSequence).toBe(1)
  })

  it('shares one requested JPEG across concurrent viewers and serves cached late paint', async () => {
    const f = await fixture()
    f.emit()
    await delay(40)
    const responses = await Promise.all(Array.from({ length: 8 }, () => f.frame()))
    expect(responses.every((response) => response.status === 200)).toBe(true)
    expect(new Set(responses.map((response) => response.headers.get('x-frame-sequence')))).toEqual(
      new Set(['1']),
    )
    await Promise.all(responses.map((response) => response.arrayBuffer()))
    expect(f.viewer.state().frameSequence).toBe(1)
  })

  it('waits for the next actual frame and wakes immediately without an empty poll', async () => {
    const f = await fixture()
    f.emit()
    await (await f.frame()).arrayBuffer()
    let completed = false
    const next = f.frame(1).then((response) => {
      completed = true
      return response
    })
    await delay(60)
    expect(completed).toBe(false)
    const started = performance.now()
    f.emit()
    const response = await next
    expect(response.status).toBe(200)
    expect(response.headers.get('x-frame-sequence')).toBe('2')
    expect(performance.now() - started).toBeLessThan(500)
    await response.arrayBuffer()
  })

  it('removes disconnected waiting clients instead of retaining their encoding demand', async () => {
    const f = await fixture()
    f.emit()
    await (await f.frame()).arrayBuffer()
    const controller = new AbortController()
    const cancelled = f.frame(1, true, controller.signal).catch(() => undefined)
    await delay(30)
    controller.abort()
    await cancelled
    await delay(30)
    f.emit()
    await delay(40)
    expect(f.viewer.state().frameSequence).toBe(1)
  })

  it('cancels a rate-limited codec job when its only waiting client disconnects', async () => {
    const f = await fixture(1)
    f.emit()
    await (await f.frame()).arrayBuffer()
    f.emit()
    const controller = new AbortController()
    const cancelled = f.frame(1, true, controller.signal).catch(() => undefined)
    await delay(30)
    controller.abort()
    await cancelled
    await delay(1050)
    expect(f.viewer.state().frameSequence).toBe(1)
  })

  it('keeps legacy immediate polling and stale revisions bounded', async () => {
    const f = await fixture()
    f.emit()
    expect((await f.request('/frame?after=0&revision=999&wait=1')).status).toBe(204)
    expect(f.viewer.state().frameSequence).toBe(0)
    expect((await f.frame(0, false)).status).toBe(204)
    await delay(40)
    const response = await f.frame(0, false)
    expect(response.status).toBe(200)
    await response.arrayBuffer()
    expect(f.viewer.state().frameSequence).toBe(1)
  })

  it('closes pending polls and detaches native frame observers', async () => {
    const f = await fixture()
    const pending = f.frame().catch(() => undefined)
    await delay(30)
    expect(f.listeners.size).toBe(1)
    await f.viewer.close()
    await pending
    expect(f.listeners.size).toBe(0)
  })

  it('wakes a stale pending frame on a viewport revision change', async () => {
    const f = await fixture()
    const pending = f.frame()
    await delay(30)
    f.source.viewport = { width: 60, height: 40 }
    expect(f.viewer.state().revision).toBe(2)
    const response = await pending
    expect(response.status).toBe(204)
    expect(f.viewer.state().frameSequence).toBe(0)
  })

  it('times out a quiet wait without encoding or losing the connection', async () => {
    const f = await fixture()
    const started = performance.now()
    expect((await f.frame()).status).toBe(204)
    expect(performance.now() - started).toBeGreaterThanOrEqual(900)
    expect(f.viewer.state().frameSequence).toBe(0)
    f.emit()
    const response = await f.frame()
    expect(response.status).toBe(200)
    await response.arrayBuffer()
  })
})
