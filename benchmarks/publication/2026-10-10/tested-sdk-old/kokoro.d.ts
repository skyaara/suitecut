import { type SuiteCutWordTiming } from './types.js';
export declare const SUITECUT_KOKORO_VOICES: readonly ["af_heart", "af_alloy", "af_aoede", "af_bella", "af_jessica", "af_kore", "af_nicole", "af_nova", "af_river", "af_sarah", "af_sky", "am_adam", "am_echo", "am_eric", "am_fenrir", "am_liam", "am_michael", "am_onyx", "am_puck", "am_santa", "bf_alice", "bf_emma", "bf_isabella", "bf_lily", "bm_daniel", "bm_fable", "bm_george", "bm_lewis"];
export type SuiteCutKokoroProgressStage = 'loading-model' | 'loading-voice' | 'synthesizing' | 'writing-audio';
export interface SuiteCutKokoroProgress {
    stage: SuiteCutKokoroProgressStage;
    completedChunks: number;
    totalChunks: number;
}
export interface SuiteCutKokoroSynthesisResult {
    text: string;
    durationMs: number;
    words: SuiteCutWordTiming[];
}
export declare function normalizeKokoroText(text: string): string;
export declare function splitKokoroText(text: string, maximumLength?: number): string[];
export declare function synthesizeKokoro(text: string, voice: string, speed: number, outputPath: string, onProgress?: (progress: SuiteCutKokoroProgress) => void): Promise<SuiteCutKokoroSynthesisResult>;
//# sourceMappingURL=kokoro.d.ts.map