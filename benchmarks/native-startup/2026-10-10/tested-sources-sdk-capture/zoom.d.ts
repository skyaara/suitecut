import { type SuiteCutAnimationOptions, type SuiteCutEasing, type SuiteCutZoomOptions } from './schemas.js';
import { type SuiteCutPageId, type SuiteCutRect, type SuiteCutViewport, type SuiteCutZoomEvent } from './types.js';
interface ResolvedZoomAnimation {
    type: NonNullable<SuiteCutAnimationOptions['type']>;
    durationMs: number;
    easing: SuiteCutEasing;
}
export interface ResolvedSuiteCutZoomOptions {
    scale: number;
    paddingPx: number;
    enter: ResolvedZoomAnimation;
    holdMs: number;
    exit: ResolvedZoomAnimation;
    totalDurationMs: number;
}
export interface SuiteCutZoomFrame {
    scale: number;
    centerX: number;
    centerY: number;
}
export interface ResolvedSuiteCutCameraZoom {
    event: SuiteCutZoomEvent;
    resolved: ResolvedSuiteCutZoomOptions;
    frame: SuiteCutZoomFrame;
    startMs: number;
    enterEndMs: number;
    exitStartMs: number;
    endMs: number;
}
export interface SuiteCutCameraRun {
    pageId: SuiteCutPageId;
    zooms: ResolvedSuiteCutCameraZoom[];
    startMs: number;
    endMs: number;
}
/** Resolves the capture and renderer defaults for one camera zoom. */
export declare function resolveSuiteCutZoomOptions(options: SuiteCutZoomOptions): ResolvedSuiteCutZoomOptions;
/** Maps the public easing names to their CSS cubic-bezier curves. */
export declare function suiteCutEasingProgress(easing: SuiteCutEasing, progress: number): number;
/** Resolves an in-bounds camera frame that keeps the visible target and padding on screen. */
export declare function suiteCutZoomFrameForTarget(resolved: ResolvedSuiteCutZoomOptions, rect: SuiteCutRect, viewport: SuiteCutViewport): SuiteCutZoomFrame;
/** Groups close zooms so the renderer can glide between them without returning to 1x. */
export declare function resolveSuiteCutCameraRuns(events: readonly SuiteCutZoomEvent[]): SuiteCutCameraRun[];
/** Limits zoom so the visible target and available padding remain inside the crop. */
export declare function suiteCutZoomScaleForTarget(resolved: ResolvedSuiteCutZoomOptions, rect: SuiteCutRect, viewport: SuiteCutViewport): number;
/** Returns the sampled camera scale at an attempt timestamp. */
export declare function suiteCutZoomScaleAt(resolved: ResolvedSuiteCutZoomOptions, targetScale: number, relativeMs: number): number | undefined;
export {};
//# sourceMappingURL=zoom.d.ts.map