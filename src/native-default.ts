import {
  launchNativeBrowser,
  type NativeBrowser,
  type NativeBrowserOptions,
} from './native-browser.js'
import {
  recordNative,
  type NativeRecordingContext,
  type NativeRecordingOptions,
  type SuiteCutRecordResult,
} from './native-recording.js'
import {
  createNativeBroadcast,
  type NativeBroadcast,
  type NativeBroadcastOptions,
} from './native.js'
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
export type SuiteCutBrowserOptions = Omit<NativeBrowserOptions, 'executablePath'> & {
  executablePath?: string
}
export interface SuiteCutNativeRecordOptions extends Omit<NativeRecordingOptions, 'source'> {
  backend?: 'native'
  /** Native launch settings; executablePath also accepts SUITECUT_NATIVE_EXECUTABLE. */
  native?: Omit<SuiteCutBrowserOptions, 'signal'>
}
export type SuiteCutRecordingResult = SuiteCutRecordResult & { backend: 'native' }
export type SuiteCutNativeRecordingCallback = (
  context: NativeRecordingContext,
) => void | Promise<void>

/** Launches SuiteCut's I420 native browser. */
export function launchBrowser(options: SuiteCutBrowserOptions = {}): Promise<NativeBrowser> {
  const executablePath = options.executablePath ?? process.env.SUITECUT_NATIVE_EXECUTABLE
  if (!executablePath)
    throw new Error(
      'SuiteCut 2.0 requires a native executable. Set SUITECUT_NATIVE_EXECUTABLE or native.executablePath.',
    )
  return launchNativeBrowser({ pixelFormat: 'i420', ...options, executablePath })
}

/** Records with native capture by default; each call owns and closes its primary browser. */
export async function record(
  name: string,
  callback: SuiteCutNativeRecordingCallback,
  options: SuiteCutNativeRecordOptions = {},
): Promise<SuiteCutRecordingResult> {
  const { backend, native, ...settings } = options
  if (backend !== undefined && backend !== 'native') throw new Error('Unsupported SuiteCut backend')
  // Validate before spawning; recordNative validates the remaining recording settings.
  for (const key of Object.keys(settings)) {
    if (!['ffmpegPath', 'capture', 'audioPlugins', 'output', 'signal'].includes(key))
      throw new Error(`Unsupported native option: ${key}`)
  }
  const source = await launchBrowser({
    ...native,
    ...(settings.signal ? { signal: settings.signal } : {}),
  })
  try {
    return {
      ...(await recordNative(name, callback, {
        ...settings,
        source,
      })),
      backend: 'native',
    }
  } finally {
    await source.close()
  }
}

/** Creates a native-default recorder with reusable launch and recording settings. */
export function defineSuiteCut(defaults: SuiteCutNativeRecordOptions = {}) {
  return (
    name: string,
    callback: SuiteCutNativeRecordingCallback,
    overrides: SuiteCutNativeRecordOptions = {},
  ): Promise<SuiteCutRecordingResult> =>
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
