import { type NativeBrowser, type NativeBrowserOptions } from './native-browser.js';
import { type NativeRecordingContext, type NativeRecordingOptions } from './native-recording.js';
import { type NativeBroadcast, type NativeBroadcastOptions } from './native.js';
import { type SuiteCutRecordOptions, type SuiteCutRecordingCallback, type SuiteCutRecordResult } from './playwright.js';
import { type SuiteCutRenderRequest } from './render.js';
/** Renders through the C++ libavfilter/codec backend. */
export declare function renderSuiteCut(request: SuiteCutRenderRequest): Promise<import("./render.js").SuiteCutRenderReport>;
export { createNativePage } from './native-page.js';
export type { NativePage, NativeLocator } from './native-page.js';
export type { NativeBrowser, NativeVideoFrame, NativeAudioPacket } from './native-browser.js';
export type { NativeRecordingCaptureOptions, NativeRecordingContext } from './native-recording.js';
export type { NativeBroadcast, NativeBroadcastOptions, NativeStreamOptions } from './native.js';
export type { SuiteCutStreamOptions } from './schemas.js';
export type { SuiteCutRenderRequest, SuiteCutRenderConfig } from './render.js';
export declare const DEFAULT_BACKEND: "native";
export type SuiteCutBrowserOptions = Omit<NativeBrowserOptions, 'executablePath'> & {
    executablePath?: string;
};
export interface SuiteCutNativeRecordOptions extends Omit<NativeRecordingOptions, 'source'> {
    backend?: 'native';
    /** Native launch settings; executablePath also accepts SUITECUT_NATIVE_EXECUTABLE. */
    native?: Omit<SuiteCutBrowserOptions, 'signal'>;
}
export interface SuiteCutPlaywrightRecordOptions extends SuiteCutRecordOptions {
    backend: 'playwright';
}
export type SuiteCutRecordingResult = SuiteCutRecordResult & {
    backend: 'native' | 'playwright';
};
export type SuiteCutNativeRecordingCallback = (context: NativeRecordingContext) => void | Promise<void>;
/** Launches SuiteCut's default I420 native browser; never silently falls back to Playwright. */
export declare function launchBrowser(options?: SuiteCutBrowserOptions): Promise<NativeBrowser>;
/** Records with native capture by default; each call owns and closes its primary browser. */
export declare function record(name: string, callback: SuiteCutNativeRecordingCallback, options?: SuiteCutNativeRecordOptions): Promise<SuiteCutRecordingResult>;
export declare function record(name: string, callback: SuiteCutRecordingCallback, options: SuiteCutPlaywrightRecordOptions): Promise<SuiteCutRecordingResult>;
/** Creates a native-default recorder with reusable launch and recording settings. */
export declare function defineSuiteCut(defaults?: SuiteCutNativeRecordOptions): (name: string, callback: SuiteCutNativeRecordingCallback, overrides?: SuiteCutNativeRecordOptions) => Promise<SuiteCutRecordingResult>;
/** Broadcasts a caller-owned native source using SuiteCut's configured encoder. */
export declare function createBroadcast(options: Omit<NativeBroadcastOptions, 'ffmpegPath'> & {
    ffmpegPath?: string;
}): Promise<NativeBroadcast>;
export { launchEngine } from './engine.js';
export type { SuiteCutEngine, NativeOutput, NativeOutputOptions, NativeOutputStatus, NativeVideoCodec, NativeAudioCodec, NativeAudioStartup, NativeOutputContainer, NativeEncoderInfo, } from './engine.js';
//# sourceMappingURL=native-default.d.ts.map