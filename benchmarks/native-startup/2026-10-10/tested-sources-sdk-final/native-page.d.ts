import { type PresentationLocator, type PresentationPage } from './browser-control.js';
import { type NativeBrowser } from './native-browser.js';
import { type SuiteCutRect } from './types/geometry.js';
import { type UntrustedInput } from './untrusted.js';
/** A deliberately small browser-control API; this is not a Playwright Page. */
export declare class NativePage implements PresentationPage {
    readonly source: NativeBrowser;
    private readonly signal;
    constructor(source: NativeBrowser, signal?: AbortSignal);
    goto(url: string): Promise<void>;
    sendDevToolsCommand(method: string, params?: Record<string, UntrustedInput>): Promise<UntrustedInput>;
    locator(selector: string): NativeLocator;
    evaluate<R>(fn: () => R | Promise<R>): Promise<R>;
    evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg: A): Promise<R>;
    waitForTimeout(ms: number): Promise<void>;
    waitForLoadState(state: 'domcontentloaded'): Promise<void>;
    isClosed(): boolean;
}
/** Strict CSS locator with native input; scopes are the main document only. */
export declare class NativeLocator implements PresentationLocator<NativePage> {
    private readonly owner;
    readonly selector: string;
    constructor(owner: NativePage, selector: string);
    page(): NativePage;
    toString(): string;
    evaluate<R, A>(fn: (element: SVGElement | HTMLElement, arg: A) => R | Promise<R>, arg: A): Promise<R>;
    boundingBox(): Promise<SuiteCutRect | null>;
    scrollIntoViewIfNeeded(): Promise<void>;
    private point;
    hover(): Promise<void>;
    click(): Promise<void>;
    fill(value: string): Promise<void>;
    pressSequentially(value: string, options: {
        delay: number;
    }): Promise<void>;
}
/** Adds authored browser controls to an existing native source without owning it. */
export declare function createNativePage(source: NativeBrowser, signal?: AbortSignal): NativePage;
//# sourceMappingURL=native-page.d.ts.map