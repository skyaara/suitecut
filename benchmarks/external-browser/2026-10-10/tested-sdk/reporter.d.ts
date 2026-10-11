import { type FullConfig, type FullResult, type Reporter, type Suite, type TestCase, type TestResult } from '@playwright/test/reporter';
import { type SuiteCutReporterOptions } from './schemas.js';
export type { SuiteCutReporterOptions } from './schemas.js';
/** Collects SuiteCut Playwright attachments into one validated run manifest. */
export default class SuiteCutReporter implements Reporter {
    #private;
    readonly options: SuiteCutReporterOptions;
    /** Creates a reporter with validated output and path options. */
    constructor(options?: SuiteCutReporterOptions);
    /** Starts manifest collection for a Playwright run. */
    onBegin(config: FullConfig, suite: Suite): void;
    /** Queues one completed Playwright attempt for collection. */
    onTestEnd(test: TestCase, result: TestResult): void;
    /** Validates and writes the manifest after all attempts finish. */
    onEnd(result: FullResult): Promise<void>;
}
//# sourceMappingURL=reporter.d.ts.map