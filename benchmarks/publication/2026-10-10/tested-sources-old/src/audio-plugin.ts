import {
  type SuiteCutAudioPluginReference,
  SuiteCutAudioPluginReferenceSchema,
  type SuiteCutJsonValue,
} from './schemas.js'

export type { SuiteCutAudioPluginReference, SuiteCutJsonValue } from './schemas.js'

/** The data passed to an external audio plugin for one narration event. */
export interface SuiteCutAudioSynthesisRequest {
  text: string
  voice: string
  speed: number
  outputPath: string
  options?: SuiteCutJsonValue
}

/** An external narration engine loaded inside SuiteCut's worker. */
export interface SuiteCutAudioPlugin {
  /** Writes one mono PCM WAV file to the requested output path. */
  synthesize(request: SuiteCutAudioSynthesisRequest): Promise<void>
  /** Releases models, workers, files, and subprocesses owned by the plugin. */
  dispose?(): Promise<void> | void
}

/** Preserves an external plugin's type without adding runtime wrappers. */
export function defineSuiteCutAudioPlugin(plugin: SuiteCutAudioPlugin): SuiteCutAudioPlugin {
  return plugin
}

/** Encodes one mono floating-point waveform as 16-bit PCM WAV. */
export function encodePcm16Wav(samples: Float32Array, sampleRate: number): Uint8Array {
  if (samples.length === 0) throw new Error('Cannot encode an empty waveform')
  if (!Number.isSafeInteger(sampleRate) || sampleRate <= 0)
    throw new Error('WAV sample rate must be a positive integer')
  if (!samples.every(Number.isFinite)) throw new Error('Cannot encode non-finite waveform samples')
  const bytes = new Uint8Array(44 + samples.length * 2)
  const view = new DataView(bytes.buffer)
  const writeAscii = (offset: number, value: string): void => {
    for (let index = 0; index < value.length; index += 1)
      view.setUint8(offset + index, value.charCodeAt(index))
  }
  writeAscii(0, 'RIFF')
  view.setUint32(4, 36 + samples.length * 2, true)
  writeAscii(8, 'WAVE')
  writeAscii(12, 'fmt ')
  view.setUint32(16, 16, true)
  view.setUint16(20, 1, true)
  view.setUint16(22, 1, true)
  view.setUint32(24, sampleRate, true)
  view.setUint32(28, sampleRate * 2, true)
  view.setUint16(32, 2, true)
  view.setUint16(34, 16, true)
  writeAscii(36, 'data')
  view.setUint32(40, samples.length * 2, true)
  for (let index = 0; index < samples.length; index += 1) {
    const sourceSample = samples[index]
    if (sourceSample === undefined) throw new Error(`Waveform sample ${index} is missing`)
    const sample = Math.max(-1, Math.min(1, sourceSample))
    view.setInt16(44 + index * 2, Math.round(sample < 0 ? sample * 32768 : sample * 32767), true)
  }
  return bytes
}

/** Validates a reference before SuiteCut sends it to the narration worker. */
export function audioPluginReference(
  reference: SuiteCutAudioPluginReference,
): SuiteCutAudioPluginReference {
  return SuiteCutAudioPluginReferenceSchema.parse(reference)
}
