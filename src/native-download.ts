// SPDX-License-Identifier: MIT
import { createReadStream, type ReadStream } from 'node:fs'
import { resolve } from 'node:path'

import * as z from 'zod'

import { type NativeBrowser } from './native-browser.js'
import { nativeDownloadContextSignal } from './native-download-lifecycle.js'
import {
  waitNativeEventPredicate,
  type NativeEventPredicate,
  type NativeEventPredicateOptions,
} from './native-event-predicate.js'
import { type NativePage } from './native-page.js'
import { NativeTimeoutError } from './native-timeouts.js'
import { type UntrustedInput } from './untrusted.js'

export const NativeDownloadInfoSchema = z.strictObject({
  event: z.enum(['created', 'state']),
  id: z.string().min(1).max(128),
  pageId: z.number().int().positive(),
  contextId: z.number().int().positive(),
  url: z.string().max(8192).nullable(),
  originalURL: z.string().max(8192).nullable(),
  suggestedFilename: z.string().max(4096).nullable(),
  mimeType: z.string().max(1024).nullable(),
  totalBytes: z.number().int().min(-1).max(Number.MAX_SAFE_INTEGER).nullable(),
  receivedBytes: z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER),
  accepted: z.boolean(),
  state: z.enum(['inProgress', 'complete', 'failed']),
  failure: z.string().max(4096).nullable(),
})
export type NativeDownloadInfo = z.infer<typeof NativeDownloadInfoSchema>

/** Metadata comes from CEF; native operations retain file and cancellation ownership. */
export class NativeDownload {
  constructor(
    private readonly owner: NativePage,
    private info: NativeDownloadInfo,
  ) {}
  update(info: NativeDownloadInfo): void {
    if (info.event === 'created' && this.info.event === 'state') return
    this.info = info
  }
  page(): NativePage {
    return this.owner
  }
  url(): string {
    if (this.info.url === null)
      throw new Error(`download.url: ${this.info.failure ?? 'Native metadata is unavailable'}`)
    return this.info.url
  }
  originalURL(): string {
    if (this.info.originalURL === null)
      throw new Error(
        `download.originalURL: ${this.info.failure ?? 'Native metadata is unavailable'}`,
      )
    return this.info.originalURL
  }
  suggestedFilename(): string {
    if (this.info.suggestedFilename === null)
      throw new Error(
        `download.suggestedFilename: ${this.info.failure ?? 'Native metadata is unavailable'}`,
      )
    return this.info.suggestedFilename
  }
  async status(): Promise<NativeDownloadInfo> {
    this.info = NativeDownloadInfoSchema.parse(await this.dispatch('status'))
    return { ...this.info }
  }
  private async dispatch(
    action: string,
    params: Record<string, UntrustedInput> = {},
    method = action,
  ): Promise<UntrustedInput> {
    // Context operations survive closing the originating page. The process
    // transport still applies native deadlines and forwards native cancellation.
    const transport = this.owner.source
    if (!transport.runContext) throw new Error('Native download process transport unavailable')
    try {
      const signal =
        action === 'path' || action === 'saveAs'
          ? nativeDownloadContextSignal(this.owner.context())
          : undefined
      return await transport.runContext(
        'download',
        {
          action,
          downloadId: this.info.id,
          timeoutMs: this.owner.defaultTimeout(),
          ...params,
        },
        signal,
      )
    } catch (error) {
      const cause =
        error instanceof Error
          ? error
          : new Error(typeof error === 'string' ? error : 'Native download operation failed', {
              cause: error,
            })
      const Type = cause.name === 'TimeoutError' ? NativeTimeoutError : Error
      throw new Type(`download.${method}: ${cause.message}`, { cause })
    }
  }
  async path(): Promise<string> {
    return z.object({ path: z.string() }).parse(await this.dispatch('path')).path
  }
  async saveAs(path: string): Promise<void> {
    const destination = z
      .string()
      .min(1)
      .max(4096)
      .refine((value) => !value.includes('\0'))
      .parse(path)
    await this.dispatch('saveAs', { path: resolve(destination) })
  }
  async createReadStream(): Promise<ReadStream> {
    const { path } = z
      .object({ path: z.string() })
      .parse(await this.dispatch('path', {}, 'createReadStream'))
    return createReadStream(path)
  }
  async cancel(): Promise<void> {
    await this.dispatch('cancel')
  }
  async delete(): Promise<void> {
    await this.dispatch('delete')
  }
  async failure(): Promise<string | null> {
    return z.object({ failure: z.string().nullable() }).parse(await this.dispatch('failure'))
      .failure
  }
}

type DownloadListener = (download: NativeDownload) => void
type DownloadSource = NativeBrowser & {
  onDownloadMessage?: (listener: (info: NativeDownloadInfo) => void) => () => void
}
const controllers = new WeakMap<NativeBrowser, NativeDownloadEvents>()

export class NativeDownloadEvents {
  private readonly listeners: DownloadListener[] = []
  private readonly downloads = new Map<string, NativeDownload>()
  private constructor(private readonly page: NativePage) {
    const source: DownloadSource = page.source
    if (!source.onDownloadMessage) throw new Error('Native download events unavailable')
    const detach = source.onDownloadMessage((info) => {
      const download = this.adopt(info)
      if (info.event !== 'created') return
      for (const listener of [...this.listeners])
        if (this.listeners.includes(listener)) listener(download)
    })
    ;(source.pageSignal ?? source.signal).addEventListener(
      'abort',
      () => {
        detach()
        this.listeners.length = 0
        this.downloads.clear()
      },
      { once: true },
    )
  }
  static forPage(page: NativePage): NativeDownloadEvents {
    let controller = controllers.get(page.source)
    if (!controller) {
      controller = new NativeDownloadEvents(page)
      controllers.set(page.source, controller)
    }
    return controller
  }
  on(listener: DownloadListener, prepend = false): void {
    this.page.source.signal.throwIfAborted()
    if (prepend) this.listeners.unshift(listener)
    else this.listeners.push(listener)
  }
  off(listener: DownloadListener): void {
    const index = this.listeners.lastIndexOf(listener)
    if (index >= 0) this.listeners.splice(index, 1)
  }
  private adopt(info: NativeDownloadInfo): NativeDownload {
    const existing = this.downloads.get(info.id)
    if (existing) {
      existing.update(info)
      return existing
    }
    const download = new NativeDownload(this.page, info)
    if (this.downloads.size >= 256) {
      const first = this.downloads.keys().next().value
      if (first !== undefined) this.downloads.delete(first)
    }
    this.downloads.set(info.id, download)
    return download
  }
  async wait(
    options: NativeEventPredicateOptions & {
      predicate?: NativeEventPredicate<NativeDownload>
    } = {},
  ): Promise<NativeDownload> {
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
        predicate: z
          .custom<NativeEventPredicate<NativeDownload>>((value) => typeof value === 'function')
          .optional(),
      })
      .parse(options)
    const afterSequence = this.page.source.eventSequence?.()
    const timeout = config.timeout ?? this.page.defaultTimeout()
    try {
      if (config.predicate)
        return await waitNativeEventPredicate(
          this.page.source,
          'download',
          (value) => this.adopt(NativeDownloadInfoSchema.parse(value)),
          config.predicate,
          { ...config, timeout, afterSequence },
        )
      const result = await this.page.runAutomation(
        'waitEvent',
        {
          event: 'download',
          timeoutMs: timeout,
          ...(afterSequence === undefined ? {} : { afterSequence }),
        },
        config.signal,
      )
      return this.adopt(NativeDownloadInfoSchema.parse(result))
    } catch (error) {
      if (config.signal?.aborted) {
        const aborted = new Error('Operation was aborted', {
          cause: config.signal.reason as UntrustedInput,
        })
        aborted.name = 'AbortError'
        throw aborted
      }
      throw error
    }
  }
}
