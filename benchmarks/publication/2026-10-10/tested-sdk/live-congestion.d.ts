/** Measurements never contain publisher URLs, process arguments, or encoder stderr. */
export interface LiveStreamDiagnostic {
    event: 'recovering' | 'caught-up' | 'resynchronized' | 'persistent-congestion' | 'stalled' | 'bitrate-adjusted' | 'bitrate-change-failed' | 'bitrate-changing';
    bottleneck: 'network-backpressure' | 'frame-processing' | 'video-input-backpressure' | 'audio-input-backpressure' | 'unknown';
    lagMs: number;
    processingMs: number;
    videoWriteMs: number;
    audioWriteMs: number;
    skippedMs: number;
    outputLagMs?: number;
    progressAgeMs?: number;
    bitrateKbps?: number;
    previousBitrateKbps?: number;
    networkQueuedBytes?: number;
    networkBlockedMs?: number;
    sentKbps?: number;
    incidents: number;
}
export declare const LIVE_RECOVERY_WINDOW_MS = 10000;
export declare const LIVE_NO_PROGRESS_MS = 20000;
export declare const LIVE_FRAME_BUFFER_BYTES: number;
/** Bounded incident history with hysteresis; one slow frame is not a new incident. */
export declare class LiveCongestion {
    private readonly report?;
    private incidents;
    private recoveringAt;
    private healthyAt;
    private persistent;
    private measurements;
    constructor(report?: ((event: LiveStreamDiagnostic) => void) | undefined);
    measure(values: Partial<typeof this.measurements>): void;
    private emit;
    /** Returns how much capture time to discard, never a request to restart FFmpeg. */
    update(now: number, lagMs: number, forced?: boolean): number;
    stalled(lagMs: number): void;
}
//# sourceMappingURL=live-congestion.d.ts.map