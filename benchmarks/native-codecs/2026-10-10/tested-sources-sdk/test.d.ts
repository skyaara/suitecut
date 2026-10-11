import { type SuiteCutCaptureOptions, type SuiteCutFixture } from './fixtures.js';
import { type SuiteCutAudioPluginReference } from './schemas.js';
interface SuiteCutFixtures {
    suitecut: SuiteCutFixture;
    suitecutCapture: SuiteCutCaptureOptions;
    suitecutAudioPlugins: readonly SuiteCutAudioPluginReference[];
}
export declare const test: import("@playwright/test").TestType<import("@playwright/test").PlaywrightTestArgs & import("@playwright/test").PlaywrightTestOptions & SuiteCutFixtures, import("@playwright/test").PlaywrightWorkerArgs & import("@playwright/test").PlaywrightWorkerOptions>;
export declare const expect: import("@playwright/test").Expect<{}>;
export {};
//# sourceMappingURL=test.d.ts.map