import {
  launchNativeBrowser,
  type NativeBrowser,
  type NativeBrowserOptions,
} from './native-browser.js'
import {
  recordNative,
  type NativeRecordingContext,
  type NativeRecordingOptions,
} from './native-recording.js'
import {
  createNativeBroadcast,
  type NativeBroadcast,
  type NativeBroadcastOptions,
} from './native.js'
import {
  record as recordPlaywright,
  type SuiteCutRecordOptions,
  type SuiteCutRecordingCallback,
  type SuiteCutRecordResult,
} from './playwright.js'
import { resolveFfmpeg } from './process.js'

export { renderSuiteCut } from './render.js'
export { createNativePage } from './native-page.js'
export type { NativePage, NativeLocator } from './native-page.js'
export type { NativeBrowser, NativeVideoFrame, NativeAudioPacket } from './native-browser.js'
export type { NativeRecordingCaptureOptions, NativeRecordingContext } from './native-recording.js'
export type { NativeBroadcast, NativeBroadcastOptions, NativeStreamOptions } from './native.js'
export type { SuiteCutStreamOptions } from './schemas.js'
export type { SuiteCutRenderRequest, SuiteCutRenderConfig } from './render.js'

export const DEFAULT_BACKEND = 'native' as const
export type BetaBrowserOptions = Omit<NativeBrowserOptions, 'executablePath'> & {
  executablePath?: string
}
export interface BetaNativeRecordOptions extends Omit<NativeRecordingOptions, 'source'> {
  backend?: 'native'
  /** Native launch settings; executablePath also accepts SUITECUT_NATIVE_EXECUTABLE. */
  native?: Omit<BetaBrowserOptions, 'signal'>
}
export interface BetaPlaywrightRecordOptions extends SuiteCutRecordOptions {
  backend: 'playwright'
}
export type BetaRecordResult = SuiteCutRecordResult & { backend: 'native' | 'playwright' }
export type BetaRecordingCallback = (context: NativeRecordingContext) => void | Promise<void>

/** Launches beta's default I420 native browser; never silently falls back to Playwright. */
export function launchBrowser(options: BetaBrowserOptions = {}): Promise<NativeBrowser> {
  const executablePath = options.executablePath ?? process.env.SUITECUT_NATIVE_EXECUTABLE
  if (!executablePath)
    throw new Error(
      'SuiteCut beta requires a native executable. Set SUITECUT_NATIVE_EXECUTABLE or native.executablePath; choose backend: "playwright" explicitly for fallback.',
    )
  return launchNativeBrowser({ pixelFormat: 'i420', ...options, executablePath })
}

/** Records with native capture by default; each call owns and closes its primary browser. */
export function record(
  name: string,
  callback: BetaRecordingCallback,
  options?: BetaNativeRecordOptions,
): Promise<BetaRecordResult>
export function record(
  name: string,
  callback: SuiteCutRecordingCallback,
  options: BetaPlaywrightRecordOptions,
): Promise<BetaRecordResult>
export async function record(
  name: string,
  callback: BetaRecordingCallback | SuiteCutRecordingCallback,
  options: BetaNativeRecordOptions | BetaPlaywrightRecordOptions = {},
): Promise<BetaRecordResult> {
  if (options.backend === 'playwright') {
    const { backend, ...settings } = options
    return {
      ...(await recordPlaywright(name, callback as SuiteCutRecordingCallback, settings)),
      backend,
    }
  }
  const { backend, native, ...settings } = options
  if (backend !== undefined && backend !== 'native')
    throw new Error('Unsupported SuiteCut beta backend')
  // Validate before spawning; recordNative validates the remaining recording settings.
  for (const key of Object.keys(settings)) {
    if (!['ffmpegPath', 'capture', 'audioPlugins', 'output', 'signal'].includes(key))
      throw new Error(`Unsupported native beta option: ${key}`)
  }
  const source = await launchBrowser({
    ...native,
    ...(settings.signal ? { signal: settings.signal } : {}),
  })
  try {
    return {
      ...(await recordNative(name, callback as BetaRecordingCallback, { ...settings, source })),
      backend: 'native',
    }
  } finally {
    await source.close()
  }
}

/** Creates a native-default recorder with reusable launch and recording settings. */
export function defineSuiteCut(defaults: BetaNativeRecordOptions = {}) {
  return (
    name: string,
    callback: BetaRecordingCallback,
    overrides: BetaNativeRecordOptions = {},
  ): Promise<BetaRecordResult> =>
    record(name, callback, {
      ...defaults,
      ...overrides,
      native: { ...defaults.native, ...overrides.native },
      capture: { ...defaults.capture, ...overrides.capture },
      output: { ...defaults.output, ...overrides.output },
    })
}

/** Broadcasts a caller-owned native source using SuiteCut's configured encoder. */
export async function createBroadcast(
  options: Omit<NativeBroadcastOptions, 'ffmpegPath'> & { ffmpegPath?: string },
): Promise<NativeBroadcast> {
  return createNativeBroadcast({ ...options, ffmpegPath: await resolveFfmpeg(options.ffmpegPath) })
}
