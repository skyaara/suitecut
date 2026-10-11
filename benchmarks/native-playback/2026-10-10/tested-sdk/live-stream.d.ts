import { type LiveAudioTransport } from './live-audio.js';
import { type SuiteCutCaptureSize, type SuiteCutCaptureViewport, type SuiteCutStreamOptions } from './schemas.js';
export interface SuiteCutLiveStream {
    readonly failure: Promise<never>;
    health(): {
        captureLagMs: number;
        outputLagMs: number;
        progressAgeMs: number;
    };
    setBitrate?(bitrateKbps: number): Promise<void>;
    ready(): Promise<void>;
    update(frame: Buffer, timestampMs?: number): void;
    stop(): Promise<void>;
}
export interface LiveEncodedOutput {
    health(): {
        outputLagMs: number;
        progressAgeMs: number;
        networkBlockedMs: number;
        networkQueuedBytes: number;
    };
    data(chunk: Buffer): Promise<void>;
    frame(index: number, captureAt: number): void;
}
/** Publishes the latest selected page frame on a continuous wall-clock timeline. */
export declare function createLiveStreamAttempt(ffmpegPath: string, options: SuiteCutStreamOptions, framesPerSecond: number, fallbackSize: SuiteCutCaptureSize, signal?: AbortSignal, processFrame?: (data: Buffer) => Promise<Buffer>, audio?: LiveAudioTransport, networkUrl?: string, networkPressure?: () => {
    networkBlockedMs: number;
    networkQueuedBytes: number;
}, encodedOutput?: LiveEncodedOutput, inputSize?: SuiteCutCaptureViewport, inputFormat?: 'mjpeg' | 'bgra' | 'i420'): SuiteCutLiveStream;
/** Keeps the browser running while replacing disconnected publishing processes. */
export declare function createLiveStream(ffmpegPath: string, options: SuiteCutStreamOptions, framesPerSecond: number, fallbackSize: SuiteCutCaptureSize, signal?: AbortSignal, processFrame?: (data: Buffer) => Promise<Buffer>, audio?: LiveAudioTransport, inputSize?: SuiteCutCaptureViewport, inputFormat?: 'mjpeg' | 'bgra' | 'i420'): SuiteCutLiveStream;
//# sourceMappingURL=live-stream.d.ts.map