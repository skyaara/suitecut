import * as z from 'zod';
import { type SuiteCutFixture } from './fixtures.js';
import { type NativeBrowser } from './native-browser.js';
import { type NativeLocator, type NativePage } from './native-page.js';
import { type SuiteCutRecordResult } from './playwright.js';
import { type SuiteCutAudioPluginReference, type SuiteCutOutputOptions } from './schemas.js';
declare const NativeRecordingCaptureSchema: z.ZodObject<{
    size: z.ZodExactOptional<z.ZodObject<{
        width: z.ZodNumber;
        height: z.ZodNumber;
    }, z.core.$strict>>;
    narrationTailMs: z.ZodExactOptional<z.ZodNumber>;
    audio: z.ZodDefault<z.ZodBoolean>;
}, z.core.$strict>;
export type NativeRecordingCaptureOptions = z.input<typeof NativeRecordingCaptureSchema>;
export interface NativeRecordingOptions {
    /** Caller-owned source; it can also supply a native broadcast. */
    source: NativeBrowser;
    ffmpegPath?: string;
    capture?: NativeRecordingCaptureOptions;
    audioPlugins?: readonly SuiteCutAudioPluginReference[];
    output?: SuiteCutOutputOptions;
    signal?: AbortSignal;
}
export interface NativeRecordingContext {
    source: NativeBrowser;
    page: NativePage;
    suitecut: SuiteCutFixture<NativePage, NativeLocator>;
    signal: AbortSignal;
    /** Registers another caller-owned source; select it with suitecut.selectPage(). */
    addSource: (source: NativeBrowser) => Promise<NativePage>;
}
/** Records a native browser flow into the standard SuiteCut manifest and render pipeline. */
export declare function recordNative(name: string, callback: (context: NativeRecordingContext) => void | Promise<void>, options: NativeRecordingOptions): Promise<SuiteCutRecordResult>;
export {};
//# sourceMappingURL=native-recording.d.ts.map