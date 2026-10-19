// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeFileChooserEvents } from '../src/native-filechooser.js'
import { NativeElementHandle, NativePage } from '../src/native-page.js'
import { type NativeFileChooserInfo, type NativeFrameInfo } from '../src/native-protocol.js'
import { type UntrustedInput } from '../src/untrusted.js'

function deferred<T>() {
  let resolve!: (value: T) => void
  let reject!: (error: UntrustedInput) => void
  const promise = new Promise<T>((accept, refuse) => {
    resolve = accept
    reject = refuse
  })
  return { promise, resolve, reject }
}
const info: NativeFileChooserInfo = {
  id: 'child-chooser',
  frame: 'child',
  backendNodeId: 17,
  multiple: false,
}
const rootInfo: NativeFileChooserInfo = {
  id: 'root-chooser',
  frame: 'root',
  backendNodeId: 18,
  multiple: false,
}
const metadata = { handle: 42, guid: 'actual-input', preview: 'JSHandle@input' }
async function reactions() {
  for (let i = 0; i < 8; ++i) await Promise.resolve()
}
function fixture(subscriptionError?: Error) {
  let frames: NativeFrameInfo[] = [
    { id: 'root', parentId: '', name: '', url: 'about:blank' },
    { id: 'child', parentId: 'root', name: '', url: 'about:blank' },
  ]
  let emit!: (value: NativeFileChooserInfo) => void
  const runAutomation = vi.fn<NativeBrowser['runAutomation']>()
  if (subscriptionError) runAutomation.mockRejectedValue(subscriptionError)
  else runAutomation.mockResolvedValue(metadata)
  const source: NativeBrowser = {
    state: 'running',
    url: 'about:blank',
    signal: new AbortController().signal,
    width: 320,
    height: 180,
    pixelFormat: 'bgra',
    framesPerSecond: 60,
    cefVersion: 'controlled',
    failure: new Promise<never>(() => undefined),
    closed: new Promise<void>(() => undefined),
    close: () => Promise.resolve(),
    navigate: () => Promise.resolve(),
    sendDevToolsCommand: () => Promise.resolve({}),
    runAutomation,
    onFrame: () => () => undefined,
    onAudio: () => () => undefined,
    frameInfos: () => frames,
    onFileChooserMessage: (listener) => {
      emit = listener
      return () => undefined
    },
  }
  const page = new NativePage(source)
  const events = NativeFileChooserEvents.forPage(
    page,
    (owner) => new NativeElementHandle(owner, 42, 'JSHandle@input', 'actual-input'),
  )
  const detach = () => {
    frames = frames.filter((frame) => frame.id !== 'child')
  }
  return {
    events,
    runAutomation,
    emit: (value: NativeFileChooserInfo) => {
      emit(value)
    },
    detach,
  }
}

describe('native chooser delivery lifetime', () => {
  it('does not poison a healthy chooser when the previous event frame is already detached', async () => {
    const f = fixture(),
      listener = vi.fn()
    f.events.on(listener)
    f.detach()
    f.emit(info)
    await reactions()
    expect(listener).not.toHaveBeenCalled()
    f.events.on(vi.fn())
    f.emit(rootInfo)
    await reactions()
    expect(listener).toHaveBeenCalledOnce()
    expect(listener.mock.calls[0]?.[0]).toBeDefined()
  })

  it('keeps the same-event waiter rejection while a detach during adoption leaves future root waits usable', async () => {
    const f = fixture(),
      adoption = deferred<UntrustedInput>(),
      journal = deferred<UntrustedInput>()
    const original = new Error('Actual detached child adoption failed')
    f.runAutomation.mockImplementation((_command, params) => {
      if (params?.action === 'fileChooserElement' && params.backendNodeId === 17)
        return adoption.promise
      if (_command === 'waitEvent') return journal.promise
      return Promise.resolve(metadata)
    })
    const pending = f.events.wait(),
      rejected = expect(pending).rejects.toBe(original)
    await reactions()
    f.emit(info)
    journal.resolve(info)
    await reactions()
    f.detach()
    adoption.reject(original)
    await rejected
    await reactions()
    f.runAutomation.mockImplementation((_command) =>
      Promise.resolve(_command === 'waitEvent' ? rootInfo : metadata),
    )
    const chooser = await f.events.wait()
    expect(chooser.isMultiple()).toBe(false)
    expect(chooser.element()).toBeInstanceOf(NativeElementHandle)
    expect(f.runAutomation.mock.calls.filter((call) => call[1]?.backendNodeId === 17)).toHaveLength(
      1,
    )
  })

  it('preserves a genuine adoption error in a still-live frame instead of silently dropping it', async () => {
    const f = fixture(),
      original = new Error('Actual live native adoption failed')
    f.runAutomation.mockRejectedValue(original)
    f.events.on(vi.fn())
    f.emit(info)
    await reactions()
    expect(() => f.events.on(vi.fn())).toThrow(original)
  })

  it('preserves subscription failure independently of an event frame disappearing', async () => {
    const original = new Error('Native subscribe refused'),
      f = fixture(original)
    f.detach()
    await reactions()
    await expect(f.events.wait()).rejects.toBe(original)
    expect(() => f.events.on(vi.fn())).toThrow(original)
  })

  it('surfaces a user listener exception with its original identity and continues other listeners', async () => {
    const f = fixture(),
      thrown = new Error('Authored chooser listener failed')
    const callbacks: VoidFunction[] = []
    const spy = vi.spyOn(globalThis, 'queueMicrotask').mockImplementation((callback) => {
      callbacks.push(callback)
    })
    try {
      const second = vi.fn()
      f.events.on(() => {
        throw thrown
      })
      f.events.on(second)
      f.emit(rootInfo)
      await reactions()
      expect(second).toHaveBeenCalledOnce()
      expect(callbacks).toHaveLength(1)
      expect(callbacks[0]).toThrow(thrown)
      expect(() => f.events.on(vi.fn())).not.toThrow()
    } finally {
      spy.mockRestore()
    }
  })
})
