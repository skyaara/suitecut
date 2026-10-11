import * as z from 'zod';
import { type SuiteCutDiagnostic, type SuiteCutPageId } from './types.js';
declare const OutputContainerSchema: z.ZodEnum<{
    mp4: "mp4";
    webm: "webm";
    mov: "mov";
    mkv: "mkv";
}>;
declare const RenderQualitySchema: z.ZodEnum<{
    standard: "standard";
    high: "high";
    master: "master";
}>;
declare const RenderFailureModeSchema: z.ZodEnum<{
    strict: "strict";
    "best-effort": "best-effort";
}>;
declare const ColorRangeSchema: z.ZodEnum<{
    auto: "auto";
    full: "full";
    limited: "limited";
}>;
declare const OutputSchema: z.ZodObject<{
    container: z.ZodExactOptional<z.ZodEnum<{
        mp4: "mp4";
        webm: "webm";
        mov: "mov";
        mkv: "mkv";
    }>>;
    format: z.ZodExactOptional<z.ZodEnum<{
        mp4: "mp4";
        webm: "webm";
        mov: "mov";
        mkv: "mkv";
    }>>;
    videoCodec: z.ZodExactOptional<z.ZodString>;
    audioCodec: z.ZodExactOptional<z.ZodString>;
    pixelFormat: z.ZodExactOptional<z.ZodString>;
    colorRange: z.ZodExactOptional<z.ZodEnum<{
        auto: "auto";
        full: "full";
        limited: "limited";
    }>>;
    width: z.ZodExactOptional<z.ZodNumber>;
    height: z.ZodExactOptional<z.ZodNumber>;
    framesPerSecond: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodLiteral<30>, z.ZodLiteral<60>]>>;
    quality: z.ZodExactOptional<z.ZodEnum<{
        standard: "standard";
        high: "high";
        master: "master";
    }>>;
}, z.core.$strict>;
declare const RenderConfigSchema: z.ZodObject<{
    output: z.ZodExactOptional<z.ZodObject<{
        container: z.ZodExactOptional<z.ZodEnum<{
            mp4: "mp4";
            webm: "webm";
            mov: "mov";
            mkv: "mkv";
        }>>;
        format: z.ZodExactOptional<z.ZodEnum<{
            mp4: "mp4";
            webm: "webm";
            mov: "mov";
            mkv: "mkv";
        }>>;
        videoCodec: z.ZodExactOptional<z.ZodString>;
        audioCodec: z.ZodExactOptional<z.ZodString>;
        pixelFormat: z.ZodExactOptional<z.ZodString>;
        colorRange: z.ZodExactOptional<z.ZodEnum<{
            auto: "auto";
            full: "full";
            limited: "limited";
        }>>;
        width: z.ZodExactOptional<z.ZodNumber>;
        height: z.ZodExactOptional<z.ZodNumber>;
        framesPerSecond: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodLiteral<30>, z.ZodLiteral<60>]>>;
        quality: z.ZodExactOptional<z.ZodEnum<{
            standard: "standard";
            high: "high";
            master: "master";
        }>>;
    }, z.core.$strict>>;
    narrationEnabled: z.ZodExactOptional<z.ZodBoolean>;
    sourceAudioEnabled: z.ZodExactOptional<z.ZodBoolean>;
    sourceAudioVolume: z.ZodExactOptional<z.ZodNumber>;
    narrationVolume: z.ZodExactOptional<z.ZodNumber>;
    resultHoldMs: z.ZodExactOptional<z.ZodNumber>;
    backgroundColor: z.ZodExactOptional<z.ZodString>;
    ffmpegPath: z.ZodExactOptional<z.ZodString>;
    failureMode: z.ZodExactOptional<z.ZodEnum<{
        strict: "strict";
        "best-effort": "best-effort";
    }>>;
}, z.core.$strict>;
declare const RenderRequestSchema: z.ZodObject<{
    manifestPath: z.ZodString;
    outputPath: z.ZodString;
    signal: z.ZodExactOptional<z.ZodCustom<AbortSignal, AbortSignal>>;
    selection: z.ZodExactOptional<z.ZodObject<{
        testId: z.ZodExactOptional<z.ZodString>;
        retry: z.ZodExactOptional<z.ZodNumber>;
    }, z.core.$strict>>;
    config: z.ZodExactOptional<z.ZodObject<{
        output: z.ZodExactOptional<z.ZodObject<{
            container: z.ZodExactOptional<z.ZodEnum<{
                mp4: "mp4";
                webm: "webm";
                mov: "mov";
                mkv: "mkv";
            }>>;
            format: z.ZodExactOptional<z.ZodEnum<{
                mp4: "mp4";
                webm: "webm";
                mov: "mov";
                mkv: "mkv";
            }>>;
            videoCodec: z.ZodExactOptional<z.ZodString>;
            audioCodec: z.ZodExactOptional<z.ZodString>;
            pixelFormat: z.ZodExactOptional<z.ZodString>;
            colorRange: z.ZodExactOptional<z.ZodEnum<{
                auto: "auto";
                full: "full";
                limited: "limited";
            }>>;
            width: z.ZodExactOptional<z.ZodNumber>;
            height: z.ZodExactOptional<z.ZodNumber>;
            framesPerSecond: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodLiteral<30>, z.ZodLiteral<60>]>>;
            quality: z.ZodExactOptional<z.ZodEnum<{
                standard: "standard";
                high: "high";
                master: "master";
            }>>;
        }, z.core.$strict>>;
        narrationEnabled: z.ZodExactOptional<z.ZodBoolean>;
        sourceAudioEnabled: z.ZodExactOptional<z.ZodBoolean>;
        sourceAudioVolume: z.ZodExactOptional<z.ZodNumber>;
        narrationVolume: z.ZodExactOptional<z.ZodNumber>;
        resultHoldMs: z.ZodExactOptional<z.ZodNumber>;
        backgroundColor: z.ZodExactOptional<z.ZodString>;
        ffmpegPath: z.ZodExactOptional<z.ZodString>;
        failureMode: z.ZodExactOptional<z.ZodEnum<{
            strict: "strict";
            "best-effort": "best-effort";
        }>>;
    }, z.core.$strict>>;
}, z.core.$strict>;
/** A video container supported by the SuiteCut renderer. */
export type SuiteCutOutputContainer = z.infer<typeof OutputContainerSchema>;
/** Backward-compatible name for SuiteCutOutputContainer. */
export type SuiteCutOutputFormat = SuiteCutOutputContainer;
/** How SuiteCut maps output sample values to black and white. */
export type SuiteCutColorRange = z.infer<typeof ColorRangeSchema>;
/** Named encoder settings with codec-specific CRF and speed values. */
export type SuiteCutRenderQuality = z.infer<typeof RenderQualitySchema>;
/** Controls whether FFmpeg diagnostics stop the render. */
export type SuiteCutRenderFailureMode = z.infer<typeof RenderFailureModeSchema>;
/** Validated dimensions, cadence, container, and encoder quality. */
export type SuiteCutRenderOutputConfig = z.infer<typeof OutputSchema>;
/** Video, audio, layout, and failure settings for one render. */
export type SuiteCutRenderConfig = z.infer<typeof RenderConfigSchema>;
/** Selects a recorded attempt and names its rendered output file. */
export type SuiteCutRenderRequest = z.infer<typeof RenderRequestSchema>;
/** Maps a span of test time to its position in the presentation timeline. */
export interface SuiteCutEditSegment {
    kind: 'play' | 'hold';
    pageId: SuiteCutPageId;
    executionStartMs: number;
    executionEndMs: number;
    presentationStartMs: number;
    presentationEndMs: number;
}
/** Records the inputs, timing edits, diagnostics, and FFmpeg result of a render. */
export interface SuiteCutRenderReport {
    attemptId: string;
    testId: string;
    startedAt: string;
    endedAt: string;
    status: 'rendered' | 'failed';
    outputPath?: string;
    executionDurationMs: number;
    presentationDurationMs: number;
    edits: SuiteCutEditSegment[];
    diagnostics: SuiteCutDiagnostic[];
    ffmpeg?: {
        executable: string;
        args: string[];
        exitCode: number | null;
        signal: NodeJS.Signals | null;
        stderrTail: string;
    };
}
/** Renders one recorded SuiteCut attempt with an installed FFmpeg encoder combination. */
export declare function renderSuiteCut(input: SuiteCutRenderRequest): Promise<SuiteCutRenderReport>;
export {};
//# sourceMappingURL=render.d.ts.map