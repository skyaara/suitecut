import * as z from 'zod';
import { type LiveStreamDiagnostic } from './live-congestion.js';
export declare const SuiteCutNonEmptyTextSchema: z.ZodString;
export declare const SuiteCutPositiveDurationSchema: z.ZodNumber;
export declare const SuiteCutNarrationVoiceSchema: z.ZodString;
export declare const SuiteCutNarrationProviderSchema: z.ZodString;
export declare const SuiteCutJsonValueSchema: z.ZodJSONSchema;
export declare const SuiteCutAudioPluginReferenceSchema: z.ZodObject<{
    provider: z.ZodString;
    module: z.ZodString;
    options: z.ZodExactOptional<z.ZodJSONSchema>;
}, z.core.$strict>;
export declare const SuiteCutNarrationOptionsSchema: z.ZodObject<{
    provider: z.ZodExactOptional<z.ZodString>;
    voice: z.ZodExactOptional<z.ZodString>;
    speed: z.ZodExactOptional<z.ZodNumber>;
    caption: z.ZodExactOptional<z.ZodString>;
}, z.core.$strict>;
export declare const SuiteCutCheckpointOptionsSchema: z.ZodObject<{
    durationMs: z.ZodExactOptional<z.ZodNumber>;
}, z.core.$strict>;
export declare const SuiteCutCaptureViewportSchema: z.ZodObject<{
    width: z.ZodNumber;
    height: z.ZodNumber;
}, z.core.$strict>;
export declare const SuiteCutCaptureSizeSchema: z.ZodObject<{
    width: z.ZodNumber;
    height: z.ZodNumber;
}, z.core.$strict>;
export declare const SuiteCutReconnectOptionsSchema: z.ZodObject<{
    maxAttempts: z.ZodExactOptional<z.ZodNumber>;
    initialDelayMs: z.ZodExactOptional<z.ZodNumber>;
    maxDelayMs: z.ZodExactOptional<z.ZodNumber>;
}, z.core.$strict>;
export declare const SuiteCutStreamOptionsSchema: z.ZodObject<{
    url: z.ZodString;
    size: z.ZodExactOptional<z.ZodObject<{
        width: z.ZodNumber;
        height: z.ZodNumber;
    }, z.core.$strict>>;
    audio: z.ZodExactOptional<z.ZodPipe<z.ZodUnion<readonly [z.ZodBoolean, z.ZodEnum<{
        silent: "silent";
        tab: "tab";
    }>]>, z.ZodTransform<boolean, boolean | "silent" | "tab">>>;
    adaptiveBitrate: z.ZodExactOptional<z.ZodBoolean>;
    reconnect: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodLiteral<false>, z.ZodObject<{
        maxAttempts: z.ZodExactOptional<z.ZodNumber>;
        initialDelayMs: z.ZodExactOptional<z.ZodNumber>;
        maxDelayMs: z.ZodExactOptional<z.ZodNumber>;
    }, z.core.$strict>]>>;
    onDiagnostic: z.ZodExactOptional<z.ZodCustom<(event: LiveStreamDiagnostic) => void, (event: LiveStreamDiagnostic) => void>>;
    bitrateKbps: z.ZodExactOptional<z.ZodNumber>;
}, z.core.$strict>;
export declare const SuiteCutCaptureOptionsSchema: z.ZodObject<{
    viewport: z.ZodExactOptional<z.ZodObject<{
        width: z.ZodNumber;
        height: z.ZodNumber;
    }, z.core.$strict>>;
    size: z.ZodExactOptional<z.ZodObject<{
        width: z.ZodNumber;
        height: z.ZodNumber;
    }, z.core.$strict>>;
    deviceScaleFactor: z.ZodExactOptional<z.ZodNumber>;
    framesPerSecond: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodLiteral<30>, z.ZodLiteral<60>]>>;
    quality: z.ZodExactOptional<z.ZodNumber>;
    narrationTailMs: z.ZodExactOptional<z.ZodNumber>;
    stream: z.ZodExactOptional<z.ZodObject<{
        url: z.ZodString;
        size: z.ZodExactOptional<z.ZodObject<{
            width: z.ZodNumber;
            height: z.ZodNumber;
        }, z.core.$strict>>;
        audio: z.ZodExactOptional<z.ZodPipe<z.ZodUnion<readonly [z.ZodBoolean, z.ZodEnum<{
            silent: "silent";
            tab: "tab";
        }>]>, z.ZodTransform<boolean, boolean | "silent" | "tab">>>;
        adaptiveBitrate: z.ZodExactOptional<z.ZodBoolean>;
        reconnect: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodLiteral<false>, z.ZodObject<{
            maxAttempts: z.ZodExactOptional<z.ZodNumber>;
            initialDelayMs: z.ZodExactOptional<z.ZodNumber>;
            maxDelayMs: z.ZodExactOptional<z.ZodNumber>;
        }, z.core.$strict>]>>;
        onDiagnostic: z.ZodExactOptional<z.ZodCustom<(event: LiveStreamDiagnostic) => void, (event: LiveStreamDiagnostic) => void>>;
        bitrateKbps: z.ZodExactOptional<z.ZodNumber>;
    }, z.core.$strict>>;
}, z.core.$strict>;
export declare const SuiteCutTypeOptionsSchema: z.ZodObject<{
    delayMs: z.ZodExactOptional<z.ZodNumber>;
    settleMs: z.ZodExactOptional<z.ZodNumber>;
    clearExisting: z.ZodExactOptional<z.ZodBoolean>;
}, z.core.$strict>;
export declare const SuiteCutTypeInputSchema: z.ZodObject<{
    text: z.ZodString;
    options: z.ZodExactOptional<z.ZodObject<{
        delayMs: z.ZodExactOptional<z.ZodNumber>;
        settleMs: z.ZodExactOptional<z.ZodNumber>;
        clearExisting: z.ZodExactOptional<z.ZodBoolean>;
    }, z.core.$strict>>;
}, z.core.$strict>;
export declare const SuiteCutScrollBehaviorSchema: z.ZodEnum<{
    auto: "auto";
    smooth: "smooth";
}>;
export declare const SuiteCutScrollAlignmentSchema: z.ZodEnum<{
    start: "start";
    center: "center";
    end: "end";
    nearest: "nearest";
}>;
export declare const SuiteCutScrollOptionsSchema: z.ZodObject<{
    behavior: z.ZodExactOptional<z.ZodEnum<{
        auto: "auto";
        smooth: "smooth";
    }>>;
    block: z.ZodExactOptional<z.ZodEnum<{
        start: "start";
        center: "center";
        end: "end";
        nearest: "nearest";
    }>>;
    inline: z.ZodExactOptional<z.ZodEnum<{
        start: "start";
        center: "center";
        end: "end";
        nearest: "nearest";
    }>>;
    settleMs: z.ZodExactOptional<z.ZodNumber>;
    timeoutMs: z.ZodExactOptional<z.ZodNumber>;
}, z.core.$strict>;
export declare const SuiteCutNarrationInputSchema: z.ZodObject<{
    text: z.ZodString;
    options: z.ZodExactOptional<z.ZodObject<{
        provider: z.ZodExactOptional<z.ZodString>;
        voice: z.ZodExactOptional<z.ZodString>;
        speed: z.ZodExactOptional<z.ZodNumber>;
        caption: z.ZodExactOptional<z.ZodString>;
    }, z.core.$strict>>;
}, z.core.$strict>;
export declare const SuiteCutCheckpointInputSchema: z.ZodObject<{
    label: z.ZodString;
    options: z.ZodExactOptional<z.ZodObject<{
        durationMs: z.ZodExactOptional<z.ZodNumber>;
    }, z.core.$strict>>;
}, z.core.$strict>;
export declare const SuiteCutHoldInputSchema: z.ZodObject<{
    durationMs: z.ZodNumber;
}, z.core.$strict>;
export declare const SuiteCutEasingSchema: z.ZodEnum<{
    linear: "linear";
    "ease-in": "ease-in";
    "ease-out": "ease-out";
    "ease-in-out": "ease-in-out";
}>;
export declare const SuiteCutVisualAnimationSchema: z.ZodEnum<{
    none: "none";
    fade: "fade";
    scale: "scale";
    "fade-scale": "fade-scale";
}>;
export declare const SuiteCutAnimationOptionsSchema: z.ZodObject<{
    type: z.ZodExactOptional<z.ZodEnum<{
        none: "none";
        fade: "fade";
        scale: "scale";
        "fade-scale": "fade-scale";
    }>>;
    durationMs: z.ZodExactOptional<z.ZodNumber>;
    easing: z.ZodExactOptional<z.ZodEnum<{
        linear: "linear";
        "ease-in": "ease-in";
        "ease-out": "ease-out";
        "ease-in-out": "ease-in-out";
    }>>;
}, z.core.$strict>;
export declare const SuiteCutHighlightModeSchema: z.ZodEnum<{
    outline: "outline";
    spotlight: "spotlight";
    fill: "fill";
}>;
export declare const SuiteCutHighlightGeometrySchema: z.ZodEnum<{
    element: "element";
    content: "content";
}>;
export declare const SuiteCutBorderStyleSchema: z.ZodEnum<{
    solid: "solid";
    dashed: "dashed";
}>;
export declare const SuiteCutZoomOptionsSchema: z.ZodObject<{
    scale: z.ZodExactOptional<z.ZodNumber>;
    geometry: z.ZodExactOptional<z.ZodEnum<{
        element: "element";
        content: "content";
    }>>;
    paddingPx: z.ZodExactOptional<z.ZodNumber>;
    holdMs: z.ZodExactOptional<z.ZodNumber>;
    enter: z.ZodExactOptional<z.ZodObject<{
        type: z.ZodExactOptional<z.ZodEnum<{
            none: "none";
            fade: "fade";
            scale: "scale";
            "fade-scale": "fade-scale";
        }>>;
        durationMs: z.ZodExactOptional<z.ZodNumber>;
        easing: z.ZodExactOptional<z.ZodEnum<{
            linear: "linear";
            "ease-in": "ease-in";
            "ease-out": "ease-out";
            "ease-in-out": "ease-in-out";
        }>>;
    }, z.core.$strict>>;
    exit: z.ZodExactOptional<z.ZodObject<{
        type: z.ZodExactOptional<z.ZodEnum<{
            none: "none";
            fade: "fade";
            scale: "scale";
            "fade-scale": "fade-scale";
        }>>;
        durationMs: z.ZodExactOptional<z.ZodNumber>;
        easing: z.ZodExactOptional<z.ZodEnum<{
            linear: "linear";
            "ease-in": "ease-in";
            "ease-out": "ease-out";
            "ease-in-out": "ease-in-out";
        }>>;
    }, z.core.$strict>>;
}, z.core.$strict>;
export declare const SuiteCutPointerActionOptionsSchema: z.ZodObject<{
    zoom: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodBoolean, z.ZodObject<{
        scale: z.ZodExactOptional<z.ZodNumber>;
        geometry: z.ZodExactOptional<z.ZodEnum<{
            element: "element";
            content: "content";
        }>>;
        paddingPx: z.ZodExactOptional<z.ZodNumber>;
        holdMs: z.ZodExactOptional<z.ZodNumber>;
        enter: z.ZodExactOptional<z.ZodObject<{
            type: z.ZodExactOptional<z.ZodEnum<{
                none: "none";
                fade: "fade";
                scale: "scale";
                "fade-scale": "fade-scale";
            }>>;
            durationMs: z.ZodExactOptional<z.ZodNumber>;
            easing: z.ZodExactOptional<z.ZodEnum<{
                linear: "linear";
                "ease-in": "ease-in";
                "ease-out": "ease-out";
                "ease-in-out": "ease-in-out";
            }>>;
        }, z.core.$strict>>;
        exit: z.ZodExactOptional<z.ZodObject<{
            type: z.ZodExactOptional<z.ZodEnum<{
                none: "none";
                fade: "fade";
                scale: "scale";
                "fade-scale": "fade-scale";
            }>>;
            durationMs: z.ZodExactOptional<z.ZodNumber>;
            easing: z.ZodExactOptional<z.ZodEnum<{
                linear: "linear";
                "ease-in": "ease-in";
                "ease-out": "ease-out";
                "ease-in-out": "ease-in-out";
            }>>;
        }, z.core.$strict>>;
    }, z.core.$strict>]>>;
    moveDurationMs: z.ZodExactOptional<z.ZodNumber>;
    settleMs: z.ZodExactOptional<z.ZodNumber>;
    waitForAnimations: z.ZodExactOptional<z.ZodBoolean>;
    animationTimeoutMs: z.ZodExactOptional<z.ZodNumber>;
}, z.core.$strict>;
export declare const SuiteCutHighlightOptionsSchema: z.ZodObject<{
    zoom: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodBoolean, z.ZodObject<{
        scale: z.ZodExactOptional<z.ZodNumber>;
        geometry: z.ZodExactOptional<z.ZodEnum<{
            element: "element";
            content: "content";
        }>>;
        paddingPx: z.ZodExactOptional<z.ZodNumber>;
        holdMs: z.ZodExactOptional<z.ZodNumber>;
        enter: z.ZodExactOptional<z.ZodObject<{
            type: z.ZodExactOptional<z.ZodEnum<{
                none: "none";
                fade: "fade";
                scale: "scale";
                "fade-scale": "fade-scale";
            }>>;
            durationMs: z.ZodExactOptional<z.ZodNumber>;
            easing: z.ZodExactOptional<z.ZodEnum<{
                linear: "linear";
                "ease-in": "ease-in";
                "ease-out": "ease-out";
                "ease-in-out": "ease-in-out";
            }>>;
        }, z.core.$strict>>;
        exit: z.ZodExactOptional<z.ZodObject<{
            type: z.ZodExactOptional<z.ZodEnum<{
                none: "none";
                fade: "fade";
                scale: "scale";
                "fade-scale": "fade-scale";
            }>>;
            durationMs: z.ZodExactOptional<z.ZodNumber>;
            easing: z.ZodExactOptional<z.ZodEnum<{
                linear: "linear";
                "ease-in": "ease-in";
                "ease-out": "ease-out";
                "ease-in-out": "ease-in-out";
            }>>;
        }, z.core.$strict>>;
    }, z.core.$strict>]>>;
    durationMs: z.ZodExactOptional<z.ZodNumber>;
    mode: z.ZodExactOptional<z.ZodEnum<{
        outline: "outline";
        spotlight: "spotlight";
        fill: "fill";
    }>>;
    geometry: z.ZodExactOptional<z.ZodEnum<{
        element: "element";
        content: "content";
    }>>;
    paddingPx: z.ZodExactOptional<z.ZodNumber>;
    borderWidthPx: z.ZodExactOptional<z.ZodNumber>;
    borderStyle: z.ZodExactOptional<z.ZodEnum<{
        solid: "solid";
        dashed: "dashed";
    }>>;
    borderColor: z.ZodExactOptional<z.ZodString>;
    borderRadiusPx: z.ZodExactOptional<z.ZodNumber>;
    fillColor: z.ZodExactOptional<z.ZodString>;
    fillOpacity: z.ZodExactOptional<z.ZodNumber>;
    backdropColor: z.ZodExactOptional<z.ZodString>;
    backdropOpacity: z.ZodExactOptional<z.ZodNumber>;
    label: z.ZodExactOptional<z.ZodString>;
    enter: z.ZodExactOptional<z.ZodObject<{
        type: z.ZodExactOptional<z.ZodEnum<{
            none: "none";
            fade: "fade";
            scale: "scale";
            "fade-scale": "fade-scale";
        }>>;
        durationMs: z.ZodExactOptional<z.ZodNumber>;
        easing: z.ZodExactOptional<z.ZodEnum<{
            linear: "linear";
            "ease-in": "ease-in";
            "ease-out": "ease-out";
            "ease-in-out": "ease-in-out";
        }>>;
    }, z.core.$strict>>;
    exit: z.ZodExactOptional<z.ZodObject<{
        type: z.ZodExactOptional<z.ZodEnum<{
            none: "none";
            fade: "fade";
            scale: "scale";
            "fade-scale": "fade-scale";
        }>>;
        durationMs: z.ZodExactOptional<z.ZodNumber>;
        easing: z.ZodExactOptional<z.ZodEnum<{
            linear: "linear";
            "ease-in": "ease-in";
            "ease-out": "ease-out";
            "ease-in-out": "ease-in-out";
        }>>;
    }, z.core.$strict>>;
}, z.core.$strict>;
export declare const SuiteCutPointSchema: z.ZodObject<{
    x: z.ZodNumber;
    y: z.ZodNumber;
}, z.core.$strict>;
export declare const SuiteCutSizeSchema: z.ZodObject<{
    width: z.ZodNumber;
    height: z.ZodNumber;
}, z.core.$strict>;
export declare const SuiteCutRectSchema: z.ZodObject<{
    x: z.ZodNumber;
    y: z.ZodNumber;
    width: z.ZodNumber;
    height: z.ZodNumber;
}, z.core.$strict>;
export declare const SuiteCutViewportSchema: z.ZodPipe<z.ZodObject<{
    width: z.ZodNumber;
    height: z.ZodNumber;
    deviceScaleFactor: z.ZodExactOptional<z.ZodNumber>;
    scrollX: z.ZodNumber;
    scrollY: z.ZodNumber;
}, z.core.$strict>, z.ZodTransform<{
    width: number;
    height: number;
    scrollX: number;
    scrollY: number;
}, {
    width: number;
    height: number;
    scrollX: number;
    scrollY: number;
    deviceScaleFactor?: number;
}>>;
export declare const SuiteCutCapturedGeometrySchema: z.ZodObject<{
    rect: z.ZodObject<{
        x: z.ZodNumber;
        y: z.ZodNumber;
        width: z.ZodNumber;
        height: z.ZodNumber;
    }, z.core.$strict>;
    viewport: z.ZodPipe<z.ZodObject<{
        width: z.ZodNumber;
        height: z.ZodNumber;
        deviceScaleFactor: z.ZodExactOptional<z.ZodNumber>;
        scrollX: z.ZodNumber;
        scrollY: z.ZodNumber;
    }, z.core.$strict>, z.ZodTransform<{
        width: number;
        height: number;
        scrollX: number;
        scrollY: number;
    }, {
        width: number;
        height: number;
        scrollX: number;
        scrollY: number;
        deviceScaleFactor?: number;
    }>>;
}, z.core.$strict>;
export declare const SuiteCutPathKindSchema: z.ZodEnum<{
    absolute: "absolute";
    "manifest-relative": "manifest-relative";
}>;
export declare const SuiteCutBrowserNameSchema: z.ZodEnum<{
    chromium: "chromium";
    firefox: "firefox";
    webkit: "webkit";
}>;
export declare const SuiteCutOutputOptionsSchema: z.ZodObject<{
    directory: z.ZodExactOptional<z.ZodString>;
    manifestPath: z.ZodExactOptional<z.ZodString>;
    pathKind: z.ZodExactOptional<z.ZodEnum<{
        absolute: "absolute";
        "manifest-relative": "manifest-relative";
    }>>;
}, z.core.$strict>;
export declare const SuiteCutStepCategorySchema: z.ZodEnum<{
    unknown: "unknown";
    hook: "hook";
    fixture: "fixture";
    "pw:api": "pw:api";
    expect: "expect";
    "test.step": "test.step";
    "test.attach": "test.attach";
}>;
export declare const SuiteCutReporterOptionsSchema: z.ZodObject<{
    outputFile: z.ZodExactOptional<z.ZodString>;
    pathKind: z.ZodExactOptional<z.ZodEnum<{
        absolute: "absolute";
        "manifest-relative": "manifest-relative";
    }>>;
    includeStepCategories: z.ZodExactOptional<z.ZodArray<z.ZodEnum<{
        unknown: "unknown";
        hook: "hook";
        fixture: "fixture";
        "pw:api": "pw:api";
        expect: "expect";
        "test.step": "test.step";
        "test.attach": "test.attach";
    }>>>;
}, z.core.$strict>;
export type SuiteCutNarrationVoice = z.infer<typeof SuiteCutNarrationVoiceSchema>;
export type SuiteCutNarrationProvider = z.infer<typeof SuiteCutNarrationProviderSchema>;
export type SuiteCutNarrationOptions = z.infer<typeof SuiteCutNarrationOptionsSchema>;
export type SuiteCutJsonValue = z.infer<typeof SuiteCutJsonValueSchema>;
export type SuiteCutAudioPluginReference = z.infer<typeof SuiteCutAudioPluginReferenceSchema>;
export type SuiteCutCheckpointOptions = z.infer<typeof SuiteCutCheckpointOptionsSchema>;
export type SuiteCutCaptureViewport = z.infer<typeof SuiteCutCaptureViewportSchema>;
export type SuiteCutCaptureSize = z.infer<typeof SuiteCutCaptureSizeSchema>;
export type SuiteCutCaptureOptions = z.infer<typeof SuiteCutCaptureOptionsSchema>;
export type SuiteCutReconnectOptions = z.infer<typeof SuiteCutReconnectOptionsSchema>;
export type SuiteCutStreamOptions = z.infer<typeof SuiteCutStreamOptionsSchema>;
export type SuiteCutPointerActionOptions = z.infer<typeof SuiteCutPointerActionOptionsSchema>;
export type SuiteCutTypeOptions = z.infer<typeof SuiteCutTypeOptionsSchema>;
export type SuiteCutScrollBehavior = z.infer<typeof SuiteCutScrollBehaviorSchema>;
export type SuiteCutScrollAlignment = z.infer<typeof SuiteCutScrollAlignmentSchema>;
export type SuiteCutScrollOptions = z.infer<typeof SuiteCutScrollOptionsSchema>;
export type SuiteCutEasing = z.infer<typeof SuiteCutEasingSchema>;
export type SuiteCutVisualAnimation = z.infer<typeof SuiteCutVisualAnimationSchema>;
export type SuiteCutAnimationOptions = z.infer<typeof SuiteCutAnimationOptionsSchema>;
export type SuiteCutHighlightMode = z.infer<typeof SuiteCutHighlightModeSchema>;
export type SuiteCutHighlightGeometry = z.infer<typeof SuiteCutHighlightGeometrySchema>;
export type SuiteCutBorderStyle = z.infer<typeof SuiteCutBorderStyleSchema>;
export type SuiteCutHighlightOptions = z.infer<typeof SuiteCutHighlightOptionsSchema>;
export type SuiteCutZoomOptions = z.infer<typeof SuiteCutZoomOptionsSchema>;
export type SuiteCutPoint = z.infer<typeof SuiteCutPointSchema>;
export type SuiteCutSize = z.infer<typeof SuiteCutSizeSchema>;
export type SuiteCutRect = z.infer<typeof SuiteCutRectSchema>;
export type SuiteCutViewport = z.infer<typeof SuiteCutViewportSchema>;
export type SuiteCutPathKind = z.infer<typeof SuiteCutPathKindSchema>;
export type SuiteCutBrowserName = z.infer<typeof SuiteCutBrowserNameSchema>;
export type SuiteCutOutputOptions = z.infer<typeof SuiteCutOutputOptionsSchema>;
export type SuiteCutStepCategory = z.infer<typeof SuiteCutStepCategorySchema>;
export type SuiteCutReporterOptions = z.infer<typeof SuiteCutReporterOptionsSchema>;
//# sourceMappingURL=schemas.d.ts.map