import { type SuiteCutAudioPluginReference, type SuiteCutJsonValue } from './schemas.js';
export type { SuiteCutAudioPluginReference, SuiteCutJsonValue } from './schemas.js';
/** The data passed to an external audio plugin for one narration event. */
export interface SuiteCutAudioSynthesisRequest {
    text: string;
    voice: string;
    speed: number;
    outputPath: string;
    options?: SuiteCutJsonValue;
}
/** An external narration engine loaded inside SuiteCut's worker. */
export interface SuiteCutAudioPlugin {
    /** Writes one mono PCM WAV file to the requested output path. */
    synthesize(request: SuiteCutAudioSynthesisRequest): Promise<void>;
    /** Releases models, workers, files, and subprocesses owned by the plugin. */
    dispose?(): Promise<void> | void;
}
/** Preserves an external plugin's type without adding runtime wrappers. */
export declare function defineSuiteCutAudioPlugin(plugin: SuiteCutAudioPlugin): SuiteCutAudioPlugin;
/** Encodes one mono floating-point waveform as 16-bit PCM WAV. */
export declare function encodePcm16Wav(samples: Float32Array, sampleRate: number): Uint8Array;
/** Validates a reference before SuiteCut sends it to the narration worker. */
export declare function audioPluginReference(reference: SuiteCutAudioPluginReference): SuiteCutAudioPluginReference;
//# sourceMappingURL=audio-plugin.d.ts.map