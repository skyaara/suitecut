import { type EpochMilliseconds, type Milliseconds } from './types.js';
export declare function toAttemptTime(epochMs: EpochMilliseconds, originEpochMs: EpochMilliseconds): Milliseconds;
export declare function deriveSourceStartedAt(firstFrameEpochMs: EpochMilliseconds, originEpochMs: EpochMilliseconds): Milliseconds;
export declare function toSourceVideoTime(attemptTimeMs: Milliseconds, sourceStartedAtMs: Milliseconds): Milliseconds;
//# sourceMappingURL=timing.d.ts.map