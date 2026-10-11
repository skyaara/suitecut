import { type SuiteCutCaptureOptions, type SuiteCutCaptureSize, type SuiteCutCaptureViewport } from './schemas.js';
export declare const DEFAULT_SUITE_CUT_VIEWPORT: {
    readonly width: 1600;
    readonly height: 900;
};
export declare const DEFAULT_SUITE_CUT_CAPTURE_FRAMES_PER_SECOND = 30;
export declare const DEFAULT_SUITE_CUT_CHECKPOINT_DURATION_MS = 500;
export interface SuiteCutCaptureLayout {
    captureSize: SuiteCutCaptureSize;
    deviceScaleFactor: number;
    layoutScale: number;
    layoutViewport: SuiteCutCaptureViewport;
    physicalFrameSize: SuiteCutCaptureViewport;
    surfaceViewport: SuiteCutCaptureViewport;
}
/** Resolves the page composition, physical browser surface, and encoded source size. */
export declare function resolveCaptureLayout(options: SuiteCutCaptureOptions, fallbackViewport: SuiteCutCaptureViewport): SuiteCutCaptureLayout;
/** Builds a high-quality scale filter and only letterboxes differing aspect ratios. */
export declare function createCaptureScaleFilter(input: SuiteCutCaptureViewport, output: SuiteCutCaptureSize): string;
//# sourceMappingURL=capture.d.ts.map