// SPDX-License-Identifier: MIT
import { constants } from 'node:fs'
import { access, mkdtemp, rm, stat } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { basename, isAbsolute, join } from 'node:path'

import { ProtocolDownload } from './browser-protocol-download.js'
import { type CDPPageRequest, type DriverPage } from './browser-protocol-types.js'
import { type BrowserWire, protocolRecord } from './browser-wire.js'

interface Hooks {
  pageInfo(id: string): DriverPage | undefined
  pageForFrame(frameId: string): string | undefined
  preparePage(id: string): Promise<void>
}

/** Uploads use native file paths; downloads use event completion and driver-owned disk storage. */
export function createCDPFileControls(
  wire: BrowserWire,
  request: CDPPageRequest,
  hooks: Hooks,
): {
  setInputFiles(
    pageId: string,
    expression: string,
    files: string[],
    frameId?: string,
  ): Promise<void>
  subscribeDownloads(
    pageId: string,
    listener: (download: ProtocolDownload) => void,
  ): Promise<() => void>
  retireContext(contextId: string): void
  dispose(): Promise<void>
} {
  const listeners = new Map<string, Set<(download: ProtocolDownload) => void>>()
  const contexts = new Map<string, Promise<void>>()
  const configurations = new Set<Promise<void>>()
  const configuredContexts = new Set<string>()
  const active = new Map<
    string,
    { download: ProtocolDownload; pageId: string; contextId: string }
  >()
  let directory: Promise<string> | undefined
  let directoryPath: string | undefined
  let disposed = false
  const storageState = { closed: false }
  let disposal: Promise<void> | undefined

  const storage = (): Promise<string> =>
    (directory ??= mkdtemp(join(tmpdir(), 'suitecut-downloads-')).then((path) => {
      directoryPath = path
      return path
    }))
  function assertOpen(): void {
    if (disposed || !wire.isConnected()) throw new Error('Browser file controls are closed')
  }
  async function configure(contextId: string): Promise<void> {
    assertOpen()
    const existing = contexts.get(contextId)
    if (existing) return existing
    if (contexts.size >= 1024) throw new Error('Browser download context limit reached')
    const pending: Promise<void> = Promise.resolve().then(async () => {
      const downloadPath = await storage()
      assertOpen()
      if (contexts.get(contextId) !== pending)
        throw new Error('Browser context closed while configuring downloads')
      await wire.request('Browser.setDownloadBehavior', {
        behavior: 'allowAndName',
        downloadPath,
        eventsEnabled: true,
        ...(contextId ? { browserContextId: contextId } : {}),
      })
      assertOpen()
      if (contexts.get(contextId) !== pending)
        throw new Error('Browser context closed while configuring downloads')
      configuredContexts.add(contextId)
    })
    contexts.set(contextId, pending)
    configurations.add(pending)
    try {
      await pending
    } catch (error) {
      if (contexts.get(contextId) === pending) contexts.delete(contextId)
      throw error
    } finally {
      configurations.delete(pending)
    }
  }
  function cancel(guid: string, contextId: string): Promise<void> {
    return wire
      .request('Browser.cancelDownload', {
        guid,
        ...(contextId ? { browserContextId: contextId } : {}),
      })
      .then(() => undefined)
  }
  function discard(
    guid: string,
    entry: { download: ProtocolDownload; contextId: string },
    reason: string,
  ): void {
    active.delete(guid)
    entry.download.finish(reason)
    if (wire.isConnected()) void cancel(guid, entry.contextId).catch(() => undefined)
  }
  const removeBegin = wire.on('Browser.downloadWillBegin', (event) => {
    if (disposed || typeof event.guid !== 'string' || !/^[\w-]{1,128}$/u.test(event.guid)) return
    const guid = event.guid
    if (active.has(guid)) return
    const pageId = typeof event.frameId === 'string' ? hooks.pageForFrame(event.frameId) : undefined
    const page = pageId ? hooks.pageInfo(pageId) : undefined
    if (!page || !pageId || !directoryPath || !listeners.get(pageId)?.size) return
    if (active.size >= 1024) {
      void cancel(guid, page.contextId).catch(() => undefined)
      return
    }
    const download = new ProtocolDownload(
      typeof event.url === 'string' ? event.url : '',
      typeof event.suggestedFilename === 'string' ? basename(event.suggestedFilename) : 'download',
      join(directoryPath, guid),
      async () => {
        await cancel(guid, page.contextId)
        active.delete(guid)
      },
      storageState,
    )
    active.set(guid, { download, pageId, contextId: page.contextId })
    // User callbacks never escape into BrowserWire's packet parser and disconnect the wire.
    for (const listener of listeners.get(pageId) ?? []) {
      try {
        listener(download)
      } catch {
        /* A failing consumer does not own the connection. */
      }
    }
  })
  const removeProgress = wire.on('Browser.downloadProgress', (event) => {
    if (typeof event.guid !== 'string') return
    const entry = active.get(event.guid)
    if (!entry) return
    if (event.state === 'completed' || event.state === 'canceled') {
      active.delete(event.guid)
      entry.download.finish(event.state === 'completed' ? null : 'cancelled')
    }
  })
  const removeClosedPage = wire.on('Target.targetDestroyed', (event) => {
    if (typeof event.targetId !== 'string') return
    listeners.delete(event.targetId)
    for (const [guid, entry] of active) {
      if (entry.pageId === event.targetId) discard(guid, entry, 'Browser page closed')
    }
  })
  const controls = {
    retireContext(contextId: string): void {
      // Context IDs are not reused by CDP. Remove the live budget entry immediately;
      // an in-flight setup checks identity before recording its completion.
      contexts.delete(contextId)
      configuredContexts.delete(contextId)
      for (const pageId of listeners.keys())
        if (hooks.pageInfo(pageId)?.contextId === contextId) listeners.delete(pageId)
      for (const [guid, entry] of active)
        if (entry.contextId === contextId) discard(guid, entry, 'Browser context closed')
    },
    async setInputFiles(
      pageId: string,
      expression: string,
      files: string[],
      frameId?: string,
    ): Promise<void> {
      assertOpen()
      if (!Array.isArray(files) || files.length > 4096) throw new Error('Invalid upload file list')
      // Validate sequentially to bound filesystem requests without buffering any file bytes.
      for (const file of files) {
        if (typeof file !== 'string' || !isAbsolute(file) || file.includes('\0'))
          throw new Error('Upload file paths must be absolute')
        if (!(await stat(file)).isFile()) throw new Error('Upload paths must name regular files')
        await access(file, constants.R_OK)
      }
      assertOpen()
      const response = await request(
        pageId,
        'Runtime.evaluate',
        {
          expression,
          returnByValue: false,
          awaitPromise: true,
          userGesture: true,
        },
        frameId,
      )
      if (response.exceptionDetails) throw new Error('Upload input locator evaluation failed')
      const result = protocolRecord(response.result, 'upload element')
      if (typeof result.objectId !== 'string')
        throw new Error('Upload locator must resolve to a file input element')
      const objectId = result.objectId
      try {
        if (result.subtype !== 'node')
          throw new Error('Upload locator must resolve to a file input element')
        const inspected = await request(
          pageId,
          'Runtime.callFunctionOn',
          {
            objectId,
            functionDeclaration: `function(){return this.tagName === "INPUT" && this.type === "file" && (${files.length} <= 1 || this.multiple)}`,
            returnByValue: true,
          },
          frameId,
        )
        if (protocolRecord(inspected.result, 'upload input type').value !== true)
          throw new Error('Upload locator must resolve to a file input element')
        if (files.length)
          await request(pageId, 'DOM.setFileInputFiles', { objectId, files }, frameId)
        else {
          // Chromium's DOM.setFileInputFiles([]) can preserve the previous selection.
          // An empty value is the browser-supported reset; notify application listeners.
          const cleared = await request(
            pageId,
            'Runtime.callFunctionOn',
            {
              objectId,
              functionDeclaration:
                'function(){this.value="";this.dispatchEvent(new Event("input",{bubbles:true,composed:true}));this.dispatchEvent(new Event("change",{bubbles:true}));return this.files.length===0}',
              returnByValue: true,
            },
            frameId,
          )
          if (
            cleared.exceptionDetails ||
            protocolRecord(cleared.result, 'cleared upload input').value !== true
          )
            throw new Error('Browser file input could not be cleared')
        }
      } finally {
        await request(pageId, 'Runtime.releaseObject', { objectId }, frameId).catch(() => undefined)
      }
    },
    async subscribeDownloads(
      pageId: string,
      listener: (download: ProtocolDownload) => void,
    ): Promise<() => void> {
      assertOpen()
      await hooks.preparePage(pageId)
      const page = hooks.pageInfo(pageId)
      if (!page) throw new Error('Unknown or closed browser page')
      await configure(page.contextId)
      assertOpen()
      if (!hooks.pageInfo(pageId)) throw new Error('Browser page closed while subscribing')
      let group = listeners.get(pageId)
      if (!group) {
        group = new Set()
        listeners.set(pageId, group)
      }
      group.add(listener)
      return () => {
        group.delete(listener)
        if (!group.size && listeners.get(pageId) === group) listeners.delete(pageId)
      }
    },
    dispose(): Promise<void> {
      if (disposal) return disposal
      disposed = true
      storageState.closed = true
      removeBegin()
      removeProgress()
      removeClosedPage()
      listeners.clear()
      disposal = (async () => {
        const cancellations: Promise<void>[] = []
        for (const [guid, entry] of active) {
          entry.download.finish('Browser download storage closed')
          if (wire.isConnected())
            cancellations.push(cancel(guid, entry.contextId).catch(() => undefined))
        }
        active.clear()
        await Promise.allSettled([...configurations, ...cancellations])
        contexts.clear()
        if (wire.isConnected())
          await Promise.allSettled(
            [...configuredContexts].map((contextId) =>
              wire.request('Browser.setDownloadBehavior', {
                behavior: 'default',
                eventsEnabled: false,
                ...(contextId ? { browserContextId: contextId } : {}),
              }),
            ),
          )
        configuredContexts.clear()
        if (directory) await rm(await directory, { recursive: true, force: true })
      })()
      return disposal
    },
  }
  void wire.closed.then(() => controls.dispose()).catch(() => undefined)
  return controls
}
