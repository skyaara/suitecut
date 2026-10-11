import { type Browser, type BrowserContext, type BrowserContextOptions, type LaunchOptions, type Page } from 'playwright';
import { type SuiteCutFixture } from './fixtures.js';
import { type SuiteCutAudioPluginReference, type SuiteCutBrowserName, type SuiteCutCaptureOptions, type SuiteCutOutputOptions } from './schemas.js';
import { type SuiteCutManifest } from './types.js';
export type { LiveStreamDiagnostic } from './live-congestion.js';
export type { SuiteCutStreamOptions, SuiteCutAudioPluginReference, SuiteCutBrowserName, SuiteCutOutputOptions, } from './schemas.js';
export type { SuiteCutCaptureOptions, SuiteCutCheckpointOptions, SuiteCutFixture, SuiteCutNarrationOptions, SuiteCutPointerActionOptions, SuiteCutScrollOptions, SuiteCutTypeOptions, } from './fixtures.js';
export type { Milliseconds, SuiteCutHighlightOptions, SuiteCutZoomOptions } from './types.js';
export type SuiteCutCleanup = () => Promise<void> | void;
export interface SuiteCutSetupContext {
    browser: Browser;
    context: BrowserContext;
    page: Page;
    suitecut: SuiteCutFixture;
    signal?: AbortSignal;
    onCleanup: (cleanup: SuiteCutCleanup) => void;
}
export type SuiteCutRecordingContext<Extensions extends object = object> = Omit<SuiteCutSetupContext, 'onCleanup'> & Extensions;
export interface SuiteCutRecordOptions {
    browserName?: SuiteCutBrowserName;
    launch?: LaunchOptions;
    context?: BrowserContextOptions;
    capture?: SuiteCutCaptureOptions;
    audioPlugins?: readonly SuiteCutAudioPluginReference[];
    output?: SuiteCutOutputOptions;
    signal?: AbortSignal;
}
export interface SuiteCutDefinitionOptions<Extensions extends object = object> extends SuiteCutRecordOptions {
    setup?: (context: SuiteCutSetupContext) => Extensions | Promise<Extensions>;
}
export interface SuiteCutRecordResult {
    attemptId: string;
    manifest: SuiteCutManifest;
    manifestPath: string;
    outputDirectory: string;
    testId: string;
}
export type SuiteCutRecordingCallback<Extensions extends object = object> = (context: SuiteCutRecordingContext<Extensions>) => Promise<void> | void;
export type SuiteCutRecorder<Extensions extends object = object> = (name: string, callback: SuiteCutRecordingCallback<Extensions>, options?: SuiteCutRecordOptions) => Promise<SuiteCutRecordResult>;
/** Creates a raw Playwright recorder with typed setup extensions. */
export declare function defineSuiteCut<Extensions extends object = object>(defaults?: SuiteCutDefinitionOptions<Extensions>): SuiteCutRecorder<Extensions>;
/** Records one browser flow with SuiteCut's default lifecycle. */
export declare const record: SuiteCutRecorder<object>;
export { connectBrowser } from './browser-connection.js';
export type { BrowserConnectionOptions } from './browser-connection.js';
//# sourceMappingURL=playwright.d.ts.map