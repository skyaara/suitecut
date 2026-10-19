// SPDX-License-Identifier: MIT
import { resolve } from 'node:path'

import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { createNativeContextBoundPage } from '../src/native-context-page.js'
import {
  NativeBrowserContexts,
  type NativeContextTransport,
  type NativeContextEvent,
} from '../src/native-context.js'
import { NativeContextEventSchema } from '../src/native-protocol.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const process = new AbortController()
  const listeners = new Set<(event: NativeContextEvent) => void>()
  const pages = new Map<number, NativeBrowser>()
  const runContext = vi.fn<NativeContextTransport['runContext']>()
  const transport: NativeContextTransport = {
    signal: process.signal,
    runContext,
    pageSource(id) {
      let source = pages.get(id)
      if (!source) {
        const page = new AbortController()
        source = {
          pageId: id,
          signal: page.signal,
          pageSignal: page.signal,
          processSignal: process.signal,
          runAutomation: vi.fn(() => Promise.resolve(undefined)),
        } as UntrustedInput as NativeBrowser
        pages.set(id, source)
      }
      return source
    },
    onContextEvent(listener) {
      listeners.add(listener)
      return () => listeners.delete(listener)
    },
  }
  const browser = new NativeBrowserContexts(transport)
  return {
    browser,
    runContext,
    process,
    emit(event: NativeContextEvent) {
      for (const listener of [...listeners]) listener(NativeContextEventSchema.parse(event))
    },
  }
}
function deferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (error: Error) => void
  const promise = new Promise<T>((accept, fail) => {
    resolve = accept
    reject = fail
  })
  return { promise, resolve, reject }
}

describe('native context video projection', () => {
  it('keeps recording disabled without additional requests or guessed video handles', async () => {
    const f = fixture()
    expect(f.browser.defaultContext().pages()[0]!.video()).toBeNull()
    f.runContext
      .mockResolvedValueOnce({ contextId: 2 })
      .mockResolvedValueOnce({ contextId: 2, pageId: 2 })
    const context = await f.browser.newContext()
    const page = await context.newPage()
    expect(page.video()).toBeNull()
    expect(f.runContext.mock.calls).toEqual([
      ['create', {}],
      ['newPage', { contextId: 2 }],
    ])
  })
  it('passes only validated configuration and resolves the native-owned output directory', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    await f.browser.newContext({
      recordVideo: { dir: 'recordings-😀-é', size: { width: 800, height: 600 } },
    })
    expect(f.runContext).toHaveBeenCalledExactlyOnceWith('create', {
      recordVideo: { dir: resolve('recordings-😀-é'), size: { width: 800, height: 600 } },
    })
    const invalid = fixture()
    for (const recordVideo of [
      { dir: '' },
      { dir: 'a\0b' },
      { dir: 'a\ud800b' },
      { dir: 'a\udc00b' },
      { dir: 'x', size: { width: 800, height: 2162 } },
    ])
      await expect(invalid.browser.newContext({ recordVideo })).rejects.toThrow()
    expect(invalid.runContext).not.toHaveBeenCalled()
  })
  it('forwards odd video canvas sizes without rounding away the native geometry', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    await f.browser.newContext({
      recordVideo: { dir: 'odd-video', size: { width: 801, height: 599 } },
    })
    expect(f.runContext).toHaveBeenCalledExactlyOnceWith('create', {
      recordVideo: { dir: resolve('odd-video'), size: { width: 801, height: 599 } },
    })
  })
  it('uses the actual new-owned-page reply and shares its stable handle across page wrappers', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2, pageId: 2, video: { id: 'native-video-2' } })
    const page = await f.browser.newPage({ recordVideo: { dir: 'recordings' } })
    const video = page.video()
    expect(video).not.toBeNull()
    expect(page.video()).toBe(video)
    const wrapper = createNativeContextBoundPage(page.source, new AbortController().signal)
    expect(wrapper?.video()).toBe(video)
    expect(f.runContext).toHaveBeenCalledTimes(1)
  })
  it('installs genuine popup video metadata before exposing the native page announcement', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2 })
    const context = await f.browser.newContext({ recordVideo: { dir: 'recordings' } })
    const announced = vi.fn()
    context.on('page', (page) => {
      announced(page.video())
    })
    f.emit({ event: 'page-created', contextId: 2, pageId: 3, video: { id: 'popup-video' } })
    const page = context.pageById(3)!
    expect(announced).toHaveBeenCalledExactlyOnceWith(page.video())
    expect(page.video()).not.toBeNull()
    expect(() =>
      f.emit({ event: 'page-created', contextId: 2, pageId: 3, video: { id: 'foreign-video' } }),
    ).toThrow('different actual video')
    f.runContext.mockResolvedValueOnce({ path: '/native/popup.webm' })
    await expect(page.video()!.path()).resolves.toBe('/native/popup.webm')
    expect(f.runContext).toHaveBeenLastCalledWith('video', {
      action: 'path',
      contextId: 2,
      videoId: 'popup-video',
      timeoutMs: 0,
    })
  })
  it('keeps path pending until the actual native result and survives page and context closure', async () => {
    const f = fixture()
    f.runContext
      .mockResolvedValueOnce({ contextId: 2 })
      .mockResolvedValueOnce({ contextId: 2, pageId: 2, video: { id: 'actual-video' } })
    const context = await f.browser.newContext({ recordVideo: { dir: 'recordings' } })
    const page = await context.newPage(),
      video = page.video()!
    const finalized = deferred<UntrustedInput>()
    f.runContext.mockReturnValueOnce(finalized.promise)
    let settled = false
    const path = video.path().then((value) => {
      settled = true
      return value
    })
    await Promise.resolve()
    expect(settled).toBe(false)
    f.emit({ event: 'page-closed', contextId: 2, pageId: 2 })
    f.emit({ event: 'context-closed', contextId: 2 })
    expect(page.video()).toBe(video)
    expect(settled).toBe(false)
    expect(f.runContext).toHaveBeenLastCalledWith('video', {
      action: 'path',
      contextId: 2,
      videoId: 'actual-video',
      timeoutMs: 0,
    })
    finalized.resolve({ path: '/native/actual-😀-é.webm' })
    await expect(path).resolves.toBe('/native/actual-😀-é.webm')
  })
  it('forwards save and delete after context closure without host filesystem work', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2, pageId: 2, video: { id: 'actual-video' } })
    const page = await f.browser.newPage({ recordVideo: { dir: 'recordings' } }),
      video = page.video()!
    f.emit({ event: 'context-closed', contextId: 2 })
    const saved = deferred<UntrustedInput>()
    f.runContext.mockReturnValueOnce(saved.promise)
    const saving = video.saveAs('copied-😀-é.webm')
    expect(f.runContext).toHaveBeenLastCalledWith('video', {
      action: 'saveAs',
      contextId: 2,
      videoId: 'actual-video',
      timeoutMs: 0,
      path: resolve('copied-😀-é.webm'),
    })
    saved.resolve(undefined)
    await saving
    f.runContext.mockResolvedValueOnce(undefined)
    await video.delete()
    expect(f.runContext).toHaveBeenLastCalledWith('video', {
      action: 'delete',
      contextId: 2,
      videoId: 'actual-video',
      timeoutMs: 0,
    })
    for (const path of ['bad\0path', 'bad\ud800path', 'bad\udc00path'])
      await expect(video.saveAs(path)).rejects.toThrow()
    expect(f.runContext).toHaveBeenCalledTimes(3)
  })
  it('retains actual native failures and rejects stale process use without a false cached path', async () => {
    const f = fixture()
    f.runContext.mockResolvedValueOnce({ contextId: 2, pageId: 2, video: { id: 'actual-video' } })
    const page = await f.browser.newPage(),
      video = page.video()!
    const failure = new Error('actual native encoder failure')
    f.runContext.mockRejectedValueOnce(failure)
    await expect(video.path()).rejects.toBe(failure)
    for (const path of [7, '/native/\ud800.webm', '/native/\udc00.webm']) {
      f.runContext.mockResolvedValueOnce({ path })
      await expect(video.path()).rejects.toThrow()
    }
    f.process.abort(failure)
    await expect(video.path()).rejects.toBe(failure)
    await expect(video.delete()).rejects.toBe(failure)
    expect(f.runContext).toHaveBeenCalledTimes(5)
  })
})
