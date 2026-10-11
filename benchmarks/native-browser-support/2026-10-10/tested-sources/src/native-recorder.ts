import { createNativeOutput } from './engine.js'
import { type NativeBrowser } from './native-browser.js'
import { type SuiteCutCaptureSize } from './schemas.js'

/** Thin recording adapter: C++ captures, encodes, synchronizes and finalizes media. */
export function createNativeVideoRecorder(options: {
  source: NativeBrowser
  /** Retained for source compatibility; the native engine does not launch this executable. */
  ffmpegPath: string
  outputPath: string
  size: SuiteCutCaptureSize
  audio?: boolean
}) {
  const output = createNativeOutput(options.source, {
    destination: options.outputPath,
    ...options.size,
    fps: options.source.framesPerSecond,
    audio: options.audio ?? false,
    videoCodec: 'libvpx-vp9',
    audioCodec: 'libopus',
  })
  let firstAtMs: number | undefined
  const ready: Promise<void> = output.then(() => undefined)
  const failure = output.then((value) => value.failure)
  void failure.catch(() => undefined)
  void ready.catch(() => undefined)
  let stopping: Promise<void> | undefined
  return {
    ready,
    failure,
    pause: async (paused: boolean) => {
      await (await output).pause(paused)
    },
    /** Stores manifest timing only; accepts no raw media. */
    start(atMs: number) {
      firstAtMs ??= atMs
    },
    get firstAtMs() {
      return firstAtMs
    },
    stop(_atMs: number): Promise<void> {
      void _atMs
      return (stopping ??= output.then(async (value) => {
        const status = await value.stop()
        if (status.error) throw new Error(status.error)
      }))
    },
  }
}
