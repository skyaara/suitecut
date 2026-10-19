// SPDX-License-Identifier: MIT
import { type NativeBrowserCapabilities } from './native-browser-capabilities.js'
import {
  launchNativeBrowser,
  type NativeBrowser,
  type NativeBrowserOptions,
} from './native-browser.js'
import {
  NativeBrowserContexts,
  type NativeBrowserContext,
  type NativeBrowserContextOptions,
  type NativeContextTransport,
} from './native-context.js'
import { type NativePage } from './native-page.js'

type NativeBrowserProcess = NativeBrowser &
  NativeContextTransport & {
    readonly processSignal: AbortSignal
    readonly processClosed: Promise<void>
  }

function browserProcess(source: NativeBrowser): source is NativeBrowserProcess {
  return (
    source.processSignal === source.signal &&
    source.processClosed === source.closed &&
    typeof source.runContext === 'function' &&
    typeof source.pageSource === 'function' &&
    typeof source.onContextEvent === 'function'
  )
}

/** Owns the real native helper process and its context/page lifetimes. */
export class NativeBrowserController {
  readonly backend = 'cef' as const
  private readonly controller: NativeBrowserContexts
  private closePromise: Promise<void> | undefined
  readonly source: NativeBrowserProcess

  /** Use the process source returned by launchNativeBrowser, not page.source. */
  constructor(source: NativeBrowser) {
    if (!browserProcess(source))
      throw new Error('NativeBrowserController requires a native browser process transport')
    this.source = source
    this.controller = new NativeBrowserContexts(source)
  }

  /** The actual pinned CEF version reported by the native ready message. */
  version(): string {
    return this.source.cefVersion
  }

  /** Native-reported vendor-dependent features; undefined for older helpers. */
  capabilities(): NativeBrowserCapabilities | undefined {
    return this.source.backendCapabilities?.()
  }

  /** False once native process shutdown or failure begins. */
  isConnected(): boolean {
    return !this.source.processSignal.aborted && this.source.state === 'running'
  }

  /** Settles only when the actual helper process has closed. */
  get closed(): Promise<void> {
    return this.source.processClosed
  }

  contexts(): NativeBrowserContext[] {
    return this.controller.contexts()
  }

  defaultContext(): NativeBrowserContext {
    return this.controller.defaultContext()
  }

  newContext(options: NativeBrowserContextOptions = {}): Promise<NativeBrowserContext> {
    return this.controller.newContext(options)
  }

  /** Uses the native atomic page-owned context creation and cleanup path. */
  newPage(options: NativeBrowserContextOptions = {}): Promise<NativePage> {
    return this.controller.newPage(options)
  }

  /** Concurrent callers share shutdown and wait for actual process closure. */
  close(): Promise<void> {
    return (this.closePromise ??= this.closeProcess())
  }

  private async closeProcess(): Promise<void> {
    await this.source.close()
    await this.source.processClosed
  }
}

/** Launches a native process and returns browser/context/page control together. */
export async function launchBrowser(
  options: NativeBrowserOptions,
): Promise<NativeBrowserController> {
  const source = await launchNativeBrowser({
    ...options,
    frameTransport: options.frameTransport ?? 'on-demand',
  })
  try {
    return new NativeBrowserController(source)
  } catch (error) {
    try {
      await source.close()
    } catch (cleanupError) {
      throw new AggregateError(
        [error, cleanupError],
        'Native browser controller initialization and cleanup failed',
        // eslint-disable-next-line preserve-caught-error -- Keep initialization as the primary cause; both errors are in the aggregate.
        { cause: error },
      )
    }
    throw error
  }
}
