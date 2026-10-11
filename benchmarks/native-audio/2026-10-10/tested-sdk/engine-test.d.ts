import { expect } from '@playwright/test';
import { type SuiteCutEngine } from './engine.js';
import { type NativeBrowserOptions } from './native-browser.js';
/** Runs ordinary Playwright tests against SuiteCut's embedded Chromium. */
export declare const test: import("@playwright/test").TestType<import("@playwright/test").PlaywrightTestArgs & import("@playwright/test").PlaywrightTestOptions & object, import("@playwright/test").PlaywrightWorkerArgs & import("@playwright/test").PlaywrightWorkerOptions & {
    suitecutEngine: SuiteCutEngine;
    suitecutNativeOptions: NativeBrowserOptions;
}>;
export { expect };
//# sourceMappingURL=engine-test.d.ts.map