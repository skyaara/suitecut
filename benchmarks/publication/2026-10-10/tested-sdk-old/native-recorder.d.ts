import { type NativeBrowser } from './native-browser.js';
import { type SuiteCutCaptureSize } from './schemas.js';
/** Bounded latest-frame encoder; repeats held frames to preserve wall-clock timing. */
export declare function createNativeVideoRecorder(options: {
    source: Pick<NativeBrowser, 'width' | 'height' | 'pixelFormat' | 'framesPerSecond'>;
    ffmpegPath: string;
    outputPath: string;
    size: SuiteCutCaptureSize;
    audio?: boolean;
}): {
    failure: Promise<never>;
    push: (data: Buffer, atMs: number) => void;
    pushAudio: (data: Buffer, atMs: number) => void;
    readonly firstAtMs: number | undefined;
    stop(atMs: number): Promise<void>;
};
//# sourceMappingURL=native-recorder.d.ts.map