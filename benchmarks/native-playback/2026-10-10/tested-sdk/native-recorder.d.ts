import { type NativeBrowser } from './native-browser.js';
import { type SuiteCutCaptureSize } from './schemas.js';
/** Thin recording adapter: C++ captures, encodes, synchronizes and finalizes media. */
export declare function createNativeVideoRecorder(options: {
    source: NativeBrowser;
    /** Retained for source compatibility; the native engine does not launch this executable. */
    ffmpegPath: string;
    outputPath: string;
    size: SuiteCutCaptureSize;
    audio?: boolean;
}): {
    ready: Promise<void>;
    failure: Promise<never>;
    pause: (paused: boolean) => Promise<void>;
    /** Stores manifest timing only; accepts no raw media. */
    start(atMs: number): void;
    readonly firstAtMs: number | undefined;
    stop(_atMs: number): Promise<void>;
};
//# sourceMappingURL=native-recorder.d.ts.map