import { type Page } from 'playwright';
export interface SuiteCutCapturedFrame {
    data: Buffer;
    timestamp: number;
}
export interface SuiteCutFrameCapture {
    readonly firstFrame: Promise<void>;
    stop(): Promise<void>;
}
/** Captures device-pixel JPEGs because Chromium's screencast stream is CSS-pixel sized. */
export declare function startDeviceScaleFrameCapture(page: Page, framesPerSecond: number, quality: number, onFrame: (frame: SuiteCutCapturedFrame) => Promise<void> | void, signal: AbortSignal): SuiteCutFrameCapture;
//# sourceMappingURL=device-scale-capture.d.ts.map