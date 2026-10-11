import { type SuiteCutCaptureOptions, type SuiteCutCheckpointOptions, type SuiteCutHighlightOptions, type SuiteCutNarrationOptions, type SuiteCutPointerActionOptions, type SuiteCutRect, type SuiteCutScrollOptions, type SuiteCutTypeOptions, type SuiteCutViewport, type SuiteCutZoomOptions } from './schemas.js';
export declare function parseNarrationInput(text: string, options: SuiteCutNarrationOptions | undefined): {
    text: string;
    options?: {
        provider?: string;
        voice?: string;
        speed?: number;
        caption?: string;
    };
};
export declare function parseCheckpointInput(label: string, options: SuiteCutCheckpointOptions | undefined): {
    label: string;
    options?: {
        durationMs?: number;
    };
};
export declare function parseHoldInput(durationMs: number): number;
export declare function parseHighlightOptions(options: SuiteCutHighlightOptions): {
    zoom?: boolean | {
        scale?: number;
        geometry?: "element" | "content";
        paddingPx?: number;
        holdMs?: number;
        enter?: {
            type?: "none" | "fade" | "scale" | "fade-scale";
            durationMs?: number;
            easing?: "linear" | "ease-in" | "ease-out" | "ease-in-out";
        };
        exit?: {
            type?: "none" | "fade" | "scale" | "fade-scale";
            durationMs?: number;
            easing?: "linear" | "ease-in" | "ease-out" | "ease-in-out";
        };
    };
    durationMs?: number;
    mode?: "outline" | "spotlight" | "fill";
    geometry?: "element" | "content";
    paddingPx?: number;
    borderWidthPx?: number;
    borderStyle?: "solid" | "dashed";
    borderColor?: string;
    borderRadiusPx?: number;
    fillColor?: string;
    fillOpacity?: number;
    backdropColor?: string;
    backdropOpacity?: number;
    label?: string;
    enter?: {
        type?: "none" | "fade" | "scale" | "fade-scale";
        durationMs?: number;
        easing?: "linear" | "ease-in" | "ease-out" | "ease-in-out";
    };
    exit?: {
        type?: "none" | "fade" | "scale" | "fade-scale";
        durationMs?: number;
        easing?: "linear" | "ease-in" | "ease-out" | "ease-in-out";
    };
};
export declare function parseZoomOptions(options: SuiteCutZoomOptions): {
    scale?: number;
    geometry?: "element" | "content";
    paddingPx?: number;
    holdMs?: number;
    enter?: {
        type?: "none" | "fade" | "scale" | "fade-scale";
        durationMs?: number;
        easing?: "linear" | "ease-in" | "ease-out" | "ease-in-out";
    };
    exit?: {
        type?: "none" | "fade" | "scale" | "fade-scale";
        durationMs?: number;
        easing?: "linear" | "ease-in" | "ease-out" | "ease-in-out";
    };
};
export declare function parsePointerActionOptions(options: SuiteCutPointerActionOptions): {
    zoom?: boolean | {
        scale?: number;
        geometry?: "element" | "content";
        paddingPx?: number;
        holdMs?: number;
        enter?: {
            type?: "none" | "fade" | "scale" | "fade-scale";
            durationMs?: number;
            easing?: "linear" | "ease-in" | "ease-out" | "ease-in-out";
        };
        exit?: {
            type?: "none" | "fade" | "scale" | "fade-scale";
            durationMs?: number;
            easing?: "linear" | "ease-in" | "ease-out" | "ease-in-out";
        };
    };
    moveDurationMs?: number;
    settleMs?: number;
    waitForAnimations?: boolean;
    animationTimeoutMs?: number;
};
export declare function parseTypeInput(locatorText: string, options: SuiteCutTypeOptions | undefined): {
    text: string;
    options?: {
        delayMs?: number;
        settleMs?: number;
        clearExisting?: boolean;
    };
};
export declare function parseScrollOptions(options: SuiteCutScrollOptions): {
    behavior?: "auto" | "smooth";
    block?: "start" | "center" | "end" | "nearest";
    inline?: "start" | "center" | "end" | "nearest";
    settleMs?: number;
    timeoutMs?: number;
};
export declare function parseCapturedGeometry(rect: SuiteCutRect, viewport: SuiteCutViewport): {
    rect: {
        x: number;
        y: number;
        width: number;
        height: number;
    };
    viewport: {
        width: number;
        height: number;
        scrollX: number;
        scrollY: number;
    };
};
export declare function parseCapturedViewport(viewport: SuiteCutViewport): {
    width: number;
    height: number;
    scrollX: number;
    scrollY: number;
};
/** Validates the CSS viewport, physical source size, cadence, and JPEG capture quality. */
export declare function parseCaptureOptions(options: SuiteCutCaptureOptions): SuiteCutCaptureOptions;
//# sourceMappingURL=validation.d.ts.map