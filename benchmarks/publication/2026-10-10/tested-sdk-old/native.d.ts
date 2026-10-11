import * as z from 'zod';
import { type SuiteCutLiveStream } from './live-stream.js';
import { type NativeBrowser } from './native-browser.js';
export { launchNativeBrowser } from './native-browser.js';
export type { NativeAudioPacket, NativeBrowser, NativeBrowserOptions, NativeBrowserState, NativePixelFormat, NativeVideoFrame, } from './native-browser.js';
export interface NativeBroadcast extends Omit<SuiteCutLiveStream, 'update'> {
    /** Switches sources without restarting the publisher; dimensions and frame rate must match. */
    selectSource(source: NativeBrowser): void;
}
export interface NativeBroadcastOptions {
    source: NativeBrowser;
    ffmpegPath: string;
    stream: NativeStreamOptions;
    signal?: AbortSignal;
}
declare const NativeStreamOptionsSchema: z.ZodObject<{
    url: z.ZodString;
    size: z.ZodExactOptional<z.ZodObject<{
        width: z.ZodNumber;
        height: z.ZodNumber;
    }, z.core.$strict>>;
    adaptiveBitrate: z.ZodExactOptional<z.ZodBoolean>;
    reconnect: z.ZodExactOptional<z.ZodUnion<readonly [z.ZodLiteral<false>, z.ZodObject<{
        maxAttempts: z.ZodExactOptional<z.ZodNumber>;
        initialDelayMs: z.ZodExactOptional<z.ZodNumber>;
        maxDelayMs: z.ZodExactOptional<z.ZodNumber>;
    }, z.core.$strict>]>>;
    onDiagnostic: z.ZodExactOptional<z.ZodCustom<(event: import("./live-congestion.js").LiveStreamDiagnostic) => void, (event: import("./live-congestion.js").LiveStreamDiagnostic) => void>>;
    bitrateKbps: z.ZodExactOptional<z.ZodNumber>;
    audio: z.ZodDefault<z.ZodBoolean>;
}, z.core.$strict>;
export type NativeStreamOptions = z.input<typeof NativeStreamOptionsSchema>;
/** Feeds CEF's raw video and audio into SuiteCut's persistent RTMP publisher. */
export declare function createNativeBroadcast(options: NativeBroadcastOptions): Promise<NativeBroadcast>;
export { createNativePage, NativePage, NativeLocator } from './native-page.js';
export { recordNative } from './native-recording.js';
export type { NativeRecordingOptions, NativeRecordingContext, NativeRecordingCaptureOptions, } from './native-recording.js';
//# sourceMappingURL=native.d.ts.map