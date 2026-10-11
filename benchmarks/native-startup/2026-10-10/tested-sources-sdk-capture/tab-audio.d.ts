import { type BrowserContext, type BrowserContextOptions, type LaunchOptions, type Page } from 'playwright';
import { type LiveAudioTransport } from './live-audio.js';
interface TabAudio {
    transport: LiveAudioTransport;
    failure: Promise<never>;
    select(page: Page): Promise<void>;
    pause(paused: boolean): void;
    close(): Promise<void>;
}
export declare function getTabAudio(context: BrowserContext): TabAudio | undefined;
/** Owns an isolated Chromium profile and its tab-capture extension. */
export declare function launchTabAudioContext(launch: LaunchOptions, options: BrowserContextOptions): Promise<BrowserContext>;
export {};
//# sourceMappingURL=tab-audio.d.ts.map