import { type SuiteCutAttempt, type SuiteCutPageId } from './types.js';
export interface SuiteCutSequenceItem {
    kind: 'play' | 'hold';
    pageId: SuiteCutPageId;
    executionStartMs: number;
    executionEndMs: number;
    durationMs: number;
}
/** Builds the page playback order for a recorded attempt. */
export declare function buildSuiteCutSequence(attempt: SuiteCutAttempt, extraTailMs: number, framesPerSecond: number): SuiteCutSequenceItem[];
//# sourceMappingURL=timeline.d.ts.map