// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeBrowserContext, type NativeContextTransport } from '../src/native-context.js'
import { NativeDownload, type NativeDownloadInfo } from '../src/native-download.js'
import { type UntrustedInput } from '../src/untrusted.js'

function fixture() {
  const process = new AbortController()
  const runContext =
    vi.fn<
      (
        action: string,
        params?: Record<string, UntrustedInput>,
        signal?: AbortSignal,
      ) => Promise<UntrustedInput>
    >()
  const source = {
    pageId: 3,
    signal: process.signal,
    runAutomation: vi.fn(() => Promise.resolve(null)),
    runContext,
  } as UntrustedInput as NativeBrowser
  const transport: NativeContextTransport = {
    signal: process.signal,
    runContext,
    pageSource: () => source,
    onContextEvent: () => () => undefined,
  }
  const context = new NativeBrowserContext(transport, 2)
  const page = context.adoptPage(3)
  const info: NativeDownloadInfo = {
    event: 'state',
    id: 'download-test-1',
    pageId: 3,
    contextId: 2,
    url: 'https://example.test/file',
    originalURL: 'https://example.test/file',
    suggestedFilename: 'file.txt',
    mimeType: 'text/plain',
    totalBytes: 0,
    receivedBytes: 0,
    accepted: true,
    state: 'failed',
    failure: 'CEF download interrupted (reason 20)',
  }
  return { runContext, context, page, info, download: new NativeDownload(page, info) }
}

describe('native download context lifetime', () => {
  it('cancels pending file transport operations without changing terminal browser metadata', async () => {
    const f = fixture()
    const signals: AbortSignal[] = []
    f.runContext.mockImplementation((action, params, signal) => {
      if (action === 'download' && (params?.action === 'path' || params?.action === 'saveAs')) {
        if (!signal) throw new Error('File request lost its owning context lifetime')
        signals.push(signal)
        return new Promise((_, reject) => {
          signal.addEventListener(
            'abort',
            () => {
              const reason = signal.reason as UntrustedInput
              reject(reason instanceof Error ? reason : new Error('Unexpected cancellation'))
            },
            { once: true },
          )
        })
      }
      return Promise.resolve(action === 'download' ? { failure: f.info.failure } : null)
    })
    const path = f.download.path()
    const copy = f.download.saveAs('/native-saved-file')
    const pathCheck = expect(path).rejects.toThrow('download.path: canceled')
    const copyCheck = expect(copy).rejects.toThrow('download.saveAs: canceled')
    await f.context.close()
    await Promise.all([pathCheck, copyCheck])
    expect(signals).toHaveLength(2)
    expect(signals.every((signal) => signal.aborted)).toBe(true)
    expect(await f.download.failure()).toBe('CEF download interrupted (reason 20)')
  })

  it('retains completed file operations after closing a context-owned page', async () => {
    const f = fixture()
    f.download.update({ ...f.info, state: 'complete', failure: null })
    f.runContext.mockImplementation((action, params, signal) => {
      if (action === 'download') {
        expect(signal?.aborted).toBe(false)
        return Promise.resolve(params?.action === 'path' ? { path: '/native-body' } : null)
      }
      return Promise.resolve(null)
    })
    await f.page.close()
    expect(await f.download.path()).toBe('/native-body')
    await f.download.saveAs('/native-saved-file')
    expect(f.runContext).toHaveBeenCalledWith(
      'download',
      expect.objectContaining({ action: 'saveAs', path: '/native-saved-file' }),
      expect.any(AbortSignal),
    )
  })

  it('rejects new file operations after context closure while retaining native failure metadata', async () => {
    const f = fixture()
    f.runContext.mockImplementation((action) =>
      Promise.resolve(action === 'download' ? { failure: f.info.failure } : null),
    )
    await f.context.close()
    await expect(f.download.path()).rejects.toThrow(
      'Target page, context or browser has been closed',
    )
    await expect(f.download.saveAs('/late-copy')).rejects.toThrow(
      'Target page, context or browser has been closed',
    )
    expect(await f.download.failure()).toBe(f.info.failure)
    expect(f.runContext.mock.calls.filter(([action]) => action === 'download')).toHaveLength(1)
  })
})
