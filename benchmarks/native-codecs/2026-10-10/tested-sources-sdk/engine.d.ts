import { type Browser, type Page } from 'playwright';
import * as z from 'zod';
import { type NativeBrowser, type NativeBrowserOptions } from './native-browser.js';
import { type SuiteCutRenderRequest, type SuiteCutRenderReport } from './render.js';
import { type SuiteCutArtifact, type SuiteCutMedia } from './types.js';
declare const StatusSchema: z.ZodObject<{
    ready: z.ZodBoolean;
    stopped: z.ZodBoolean;
    paused: z.ZodBoolean;
    videoFrames: z.ZodNumber;
    audioSamples: z.ZodNumber;
    repeatedFrames: z.ZodNumber;
    droppedAudioSamples: z.ZodNumber;
    queuedVideoBytes: z.ZodNumber;
    queuedAudioBytes: z.ZodNumber;
    startedMs: z.ZodNumber;
    durationMs: z.ZodNumber;
    maximumEncodeMs: z.ZodNumber;
    error: z.ZodString;
}, z.core.$strict>;
export type NativeOutputStatus = z.infer<typeof StatusSchema>;
export type NativeVideoCodec = 'libx264' | 'libx265' | 'libvpx' | 'libvpx-vp9' | 'libsvtav1' | 'libaom-av1' | 'ffv1' | 'h264_nvenc' | 'hevc_nvenc' | 'av1_nvenc';
export type NativeAudioCodec = 'aac' | 'libopus' | 'flac' | 'pcm_s16le';
export type NativeOutputContainer = 'mp4' | 'webm' | 'mkv' | 'mov';
declare const EncoderInfoSchema: z.ZodObject<{
    name: z.ZodString;
    kind: z.ZodEnum<{
        audio: "audio";
        video: "video";
    }>;
    codec: z.ZodString;
    linked: z.ZodBoolean;
    hardware: z.ZodBoolean;
    lossless: z.ZodBoolean;
    containers: z.ZodArray<z.ZodEnum<{
        mp4: "mp4";
        webm: "webm";
        mov: "mov";
        mkv: "mkv";
        flv: "flv";
    }>>;
}, z.core.$strict>;
export type NativeEncoderInfo = z.infer<typeof EncoderInfoSchema>;
export interface NativeOutputOptions {
    destination: string;
    width?: number;
    height?: number;
    fps?: 30 | 60;
    audio?: boolean;
    videoCodec?: NativeVideoCodec;
    audioCodec?: NativeAudioCodec;
    container?: NativeOutputContainer;
    /** Software lossy recording quality; range depends on encoder. */
    crf?: number;
    bitrateKbps?: number;
    audioBitrateKbps?: number;
}
export interface NativeOutput {
    readonly failure: Promise<never>;
    selectTarget(targetId: string): Promise<void>;
    health(): {
        captureLagMs: number;
        outputLagMs: number;
        progressAgeMs: number;
    };
    status(): Promise<NativeOutputStatus>;
    pause(paused: boolean): Promise<void>;
    stop(): Promise<NativeOutputStatus>;
}
export interface SuiteCutEngine {
    readonly browser: Browser;
    readonly source: NativeBrowser;
    encoders(): Promise<NativeEncoderInfo[]>;
    record(page: Page, options: NativeOutputOptions): Promise<NativeOutput>;
    stream(page: Page, options: NativeOutputOptions): Promise<NativeOutput>;
    select(output: NativeOutput, page: Page): Promise<void>;
    render(request: Omit<SuiteCutRenderRequest, 'nativeSource'>): Promise<SuiteCutRenderReport>;
    close(): Promise<void>;
}
/** Public Playwright transport over private IPC. No debugging TCP port or raw media. */
export declare function connectCEF(source: NativeBrowser): Promise<Browser>;
export declare function createNativeOutput(source: NativeBrowser, options: NativeOutputOptions, streaming?: boolean, targetId?: string): Promise<NativeOutput>;
/** C++ owns CEF, encoding, audio timelines and muxing; TypeScript sends commands. */
export declare function launchEngine(options: NativeBrowserOptions): Promise<SuiteCutEngine>;
/** Reads measured metadata directly through libavformat in the native engine. */
export declare function probeNativeMedia(source: NativeBrowser, artifact: SuiteCutArtifact, path: string): Promise<SuiteCutMedia>;
export {};
//# sourceMappingURL=engine.d.ts.map