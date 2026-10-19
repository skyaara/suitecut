// SPDX-License-Identifier: MIT
import { mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { afterEach, describe, expect, it, vi } from 'vitest'

import { createCDPFileControls } from '../src/browser-cdp-files.js'
import { type ProtocolDownload } from '../src/browser-protocol-download.js'
import { type CDPPageRequest } from '../src/browser-protocol-types.js'
import { BrowserWire } from '../src/browser-wire.js'
import { type UntrustedInput } from '../src/untrusted.js'

class Socket extends EventTarget {
  static readonly OPEN = 1
  static instance: Socket
  readyState = 0
  bufferedAmount = 0
  commands: Record<string, UntrustedInput>[] = []
  constructor() {
    super()
    Socket.instance = this
    queueMicrotask(() => {
      this.readyState = 1
      this.dispatchEvent(new Event('open'))
    })
  }
  send(text: string): void {
    const packet = JSON.parse(text) as Record<string, UntrustedInput>
    this.commands.push(packet)
    queueMicrotask(() => this.event({ id: packet.id, result: {} }))
  }
  event(packet: Record<string, UntrustedInput>): void {
    this.dispatchEvent(new MessageEvent('message', { data: JSON.stringify(packet) }))
  }
  close(): void {
    this.readyState = 3
    queueMicrotask(() => this.dispatchEvent(new Event('close')))
  }
}

const teardown: (() => Promise<void>)[] = []
afterEach(async () => {
  for (const cleanup of teardown.splice(0).reverse()) await cleanup()
  vi.unstubAllGlobals()
})

async function fixture(request?: CDPPageRequest): Promise<{
  wire: BrowserWire
  socket: Socket
  controls: ReturnType<typeof createCDPFileControls>
  emit(method: string, params: Record<string, UntrustedInput>): void
  downloadDirectory(): string
}> {
  vi.stubGlobal('WebSocket', Socket)
  const wire = await BrowserWire.connect('ws://localhost/session')
  const socket = Socket.instance
  const controls = createCDPFileControls(wire, request ?? (() => Promise.resolve({})), {
    pageInfo: (id) =>
      id === 'page' ? { id, contextId: 'context', url: 'https://example.test' } : undefined,
    pageForFrame: (id) => (id === 'frame' || id === 'child-frame' ? 'page' : undefined),
    preparePage: () => Promise.resolve(),
  })
  teardown.push(
    () => wire.close(),
    () => controls.dispose(),
  )
  return {
    wire,
    socket,
    controls,
    emit: (method, params) => socket.event({ method, params }),
    downloadDirectory: () => {
      const packet = socket.commands.find(
        (command) => command.method === 'Browser.setDownloadBehavior',
      )
      const params = packet?.params as Record<string, UntrustedInput>
      if (typeof params.downloadPath !== 'string') throw new Error('Download path not configured')
      return params.downloadPath
    },
  }
}

describe('CDP upload and download ownership', () => {
  it('passes absolute file paths without reading bytes and releases a frame-scoped DOM object', async () => {
    const calls: {
      method: string
      params: Record<string, UntrustedInput>
      frameId: string | undefined
    }[] = []
    const request: CDPPageRequest = (_page, method, params = {}, frameId) => {
      calls.push({ method, params, frameId })
      if (method === 'Runtime.evaluate')
        return Promise.resolve({ result: { objectId: 'node', subtype: 'node' } })
      if (method === 'Runtime.callFunctionOn') return Promise.resolve({ result: { value: true } })
      return Promise.resolve({})
    }
    const { controls } = await fixture(request)
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-upload-test-'))
    teardown.push(() => rm(directory, { recursive: true, force: true }))
    const path = join(directory, 'payload.bin')
    await writeFile(path, Buffer.alloc(64 * 1024, 7))
    await controls.setInputFiles('page', 'document.querySelector("input")', [path], 'child-frame')
    expect(calls.map((call) => call.method)).toEqual([
      'Runtime.evaluate',
      'Runtime.callFunctionOn',
      'DOM.setFileInputFiles',
      'Runtime.releaseObject',
    ])
    expect(calls.every((call) => call.frameId === 'child-frame')).toBe(true)
    expect(calls[2]?.params).toEqual({ objectId: 'node', files: [path] })
    await controls.setInputFiles('page', 'document.querySelector("input")', [])
    expect(calls.at(-2)?.method).toBe('Runtime.callFunctionOn')
    expect(calls.at(-2)?.params.functionDeclaration).toContain('this.value=""')
    const previous = calls.length
    await expect(controls.setInputFiles('page', 'input', ['relative.txt'])).rejects.toThrow(
      'absolute',
    )
    await expect(controls.setInputFiles('page', 'input', [directory])).rejects.toThrow('regular')
    expect(calls).toHaveLength(previous)
  })

  it('releases an upload object even when a non-file node is rejected', async () => {
    const seen: string[] = []
    const { controls } = await fixture((_page, method) => {
      seen.push(method)
      return Promise.resolve(
        method === 'Runtime.evaluate'
          ? { result: { objectId: 'node', subtype: 'node' } }
          : { result: { value: false } },
      )
    })
    await expect(controls.setInputFiles('page', 'document.body', [])).rejects.toThrow('file input')
    expect(seen).toEqual(['Runtime.evaluate', 'Runtime.callFunctionOn', 'Runtime.releaseObject'])
  })

  it('releases non-node remote objects and protocol-rejected upload objects', async () => {
    for (const subtype of ['object', 'node']) {
      const seen: string[] = []
      const test = await fixture((_page, method) => {
        seen.push(method)
        if (method === 'Runtime.evaluate')
          return Promise.resolve({ result: { objectId: 'remote', subtype } })
        if (method === 'Runtime.callFunctionOn')
          return seen.filter((entry) => entry === method).length === 1
            ? Promise.resolve({ result: { value: true } })
            : Promise.reject(new Error('input detached'))
        return Promise.resolve({})
      })
      await expect(test.controls.setInputFiles('page', 'expression', [])).rejects.toThrow(
        subtype === 'node' ? 'detached' : 'file input',
      )
      expect(seen.at(-1)).toBe('Runtime.releaseObject')
      await test.controls.dispose()
      await test.wire.close()
    }
  })

  it('settles files from back-to-back cross-frame events and copies bytes on disk', async () => {
    const test = await fixture()
    let download: ProtocolDownload | undefined
    await test.controls.subscribeDownloads('page', (value) => {
      download = value
    })
    const directory = test.downloadDirectory()
    const params = test.socket.commands[0]?.params
    expect(params).toMatchObject({
      behavior: 'allowAndName',
      browserContextId: 'context',
      eventsEnabled: true,
    })
    test.emit('Browser.downloadWillBegin', {
      guid: 'guid-1',
      frameId: 'child-frame',
      url: 'https://example.test/export',
      suggestedFilename: 'export.csv',
    })
    expect(download).toBeDefined()
    const value = download!
    let completed = false
    const pathPromise = value.path().then((path) => {
      completed = true
      return path
    })
    await Promise.resolve()
    expect(completed).toBe(false)
    await writeFile(join(directory, 'guid-1'), 'a,b\n1,2\n')
    test.emit('Browser.downloadProgress', { guid: 'guid-1', state: 'completed' })
    expect(await pathPromise).toBe(join(directory, 'guid-1'))
    expect(await value.failure()).toBeNull()
    expect(value.url()).toBe('https://example.test/export')
    expect(value.suggestedFilename()).toBe('export.csv')
    const target = join(directory, 'saved', 'report.csv')
    await value.saveAs(target)
    expect(await readFile(target, 'utf8')).toBe('a,b\n1,2\n')
    await test.controls.dispose()
    await expect(stat(directory)).rejects.toThrow()
    await expect(value.path()).rejects.toThrow('storage is closed')
    expect(test.wire.stats().eventListeners).toBe(0)
  })

  it('deduplicates context setup, isolates listener failures, and handles cancellation once', async () => {
    const test = await fixture()
    const seen: ProtocolDownload[] = []
    const stops = await Promise.all([
      test.controls.subscribeDownloads('page', () => {
        throw new Error('bad consumer')
      }),
      test.controls.subscribeDownloads('page', (download) => seen.push(download)),
    ])
    expect(
      test.socket.commands.filter((packet) => packet.method === 'Browser.setDownloadBehavior'),
    ).toHaveLength(1)
    test.emit('Browser.downloadWillBegin', { guid: 'cancel-me', frameId: 'frame' })
    expect(test.wire.isConnected()).toBe(true)
    await Promise.all([seen[0]!.cancel(), seen[0]!.cancel()])
    expect(await seen[0]!.failure()).toBe('cancelled')
    expect(
      test.socket.commands.filter((packet) => packet.method === 'Browser.cancelDownload'),
    ).toHaveLength(1)
    await expect(seen[0]!.path()).rejects.toThrow('cancelled')
    for (const stop of stops) stop()
    test.emit('Browser.downloadWillBegin', { guid: 'ignored', frameId: 'frame' })
    expect(seen).toHaveLength(1)
  })

  it('deletes completed files and restores attached-browser behavior before removing storage', async () => {
    const test = await fixture()
    const seen: ProtocolDownload[] = []
    await test.controls.subscribeDownloads('page', (download) => seen.push(download))
    test.emit('Browser.downloadWillBegin', { guid: 'remove-file', frameId: 'frame' })
    const path = join(test.downloadDirectory(), 'remove-file')
    await writeFile(path, 'payload')
    test.emit('Browser.downloadProgress', { guid: 'remove-file', state: 'completed' })
    await seen[0]!.delete()
    await seen[0]!.delete()
    await expect(stat(path)).rejects.toThrow()
    await expect(seen[0]!.path()).rejects.toThrow('deleted')
    const disposal = test.controls.dispose()
    expect(test.controls.dispose()).toBe(disposal)
    await disposal
    expect(test.socket.commands.at(-1)).toMatchObject({
      method: 'Browser.setDownloadBehavior',
      params: { behavior: 'default', eventsEnabled: false, browserContextId: 'context' },
    })
  })

  it('fails pending downloads on page close and wire close without unhandled rejected promises', async () => {
    const test = await fixture()
    const seen: ProtocolDownload[] = []
    await test.controls.subscribeDownloads('page', (download) => seen.push(download))
    test.emit('Browser.downloadWillBegin', { guid: 'closed-page', frameId: 'frame' })
    test.emit('Target.targetDestroyed', { targetId: 'page' })
    expect(await seen[0]!.failure()).toMatch(/page closed/u)
    await test.controls.subscribeDownloads('page', (download) => seen.push(download))
    test.emit('Browser.downloadWillBegin', { guid: 'closed-wire', frameId: 'frame' })
    await test.wire.close()
    expect(await seen[1]!.failure()).toMatch(/storage closed/u)
    await test.controls.dispose()
    expect(test.wire.stats().eventListeners).toBe(0)
  })

  it('bounds active downloads and accepts new work after completion', async () => {
    const test = await fixture()
    const seen: ProtocolDownload[] = []
    await test.controls.subscribeDownloads('page', (download) => seen.push(download))
    for (let index = 0; index < 1025; index++)
      test.emit('Browser.downloadWillBegin', { guid: `download-${index}`, frameId: 'frame' })
    expect(seen).toHaveLength(1024)
    expect(
      test.socket.commands.filter((packet) => packet.method === 'Browser.cancelDownload'),
    ).toHaveLength(1)
    test.emit('Browser.downloadProgress', { guid: 'download-0', state: 'completed' })
    test.emit('Browser.downloadWillBegin', { guid: 'accepted-again', frameId: 'frame' })
    expect(seen).toHaveLength(1025)
    await test.controls.dispose()
    expect(await seen.at(-1)!.failure()).toMatch(/closed/u)
  })
})
