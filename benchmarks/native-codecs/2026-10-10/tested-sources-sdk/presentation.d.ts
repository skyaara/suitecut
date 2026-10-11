import { type PresentationPage } from './browser-control.js';
import { type SuiteCutHighlightOptions, type SuiteCutPoint, type SuiteCutRect, type SuiteCutWordTiming } from './types.js';
interface PresentationHighlightInput {
    rect: SuiteCutRect;
    options: SuiteCutHighlightOptions;
    durationMs: number;
}
interface PresentationCaptionInput {
    text: string;
    durationMs: number;
    words?: SuiteCutWordTiming[];
}
interface PresentationCursorInput {
    point: SuiteCutPoint;
    durationMs: number;
}
/** Installs SuiteCut's non-interactive recording layer in the current document. */
export declare function installSuiteCutPresentation(): void;
/** Removes SuiteCut's recording layer from the current document. */
export declare function removeSuiteCutPresentation(): void;
/** Animates the injected cursor to a viewport point. */
export declare function moveSuiteCutCursor(input: PresentationCursorInput): Promise<void>;
/** Draws a click ripple at the injected cursor's current position. */
export declare function pulseSuiteCutCursor(durationMs: number): Promise<void>;
/** Shows a highlight inside the page so the screencast records its animation. */
export declare function showSuiteCutHighlight(input: PresentationHighlightInput): Promise<void>;
/** Shows a caption for an already measured narration clip. */
export declare function showSuiteCutCaption(input: PresentationCaptionInput): Promise<void>;
/** Waits for application animations without waiting for SuiteCut's own recording layer. */
export declare function waitForSuiteCutPageAnimations(timeoutMs: number): Promise<void>;
export declare function movePresentationCursor(page: PresentationPage, point: SuiteCutPoint, durationMs: number): Promise<void>;
export declare function pulsePresentationCursor(page: PresentationPage, durationMs: number): Promise<void>;
export declare function showPresentationHighlight(page: PresentationPage, rect: SuiteCutRect, options: SuiteCutHighlightOptions, durationMs: number): Promise<void>;
export declare function showPresentationCaption(page: PresentationPage, text: string, durationMs: number, words?: SuiteCutWordTiming[]): Promise<void>;
export declare function waitForPresentationAnimations(page: PresentationPage, timeoutMs: number): Promise<void>;
export {};
//# sourceMappingURL=presentation.d.ts.map