import { type TestStep } from '@playwright/test/reporter';
import { type SuiteCutAttemptClock, type SuiteCutExecutionStep, type SuiteCutStepCategory } from './types.js';
export declare function normalizeExecutionSteps(steps: readonly TestStep[], clock: SuiteCutAttemptClock): SuiteCutExecutionStep[];
/** Keeps selected step categories and reconnects them to the nearest retained ancestor. */
export declare function filterExecutionSteps(steps: readonly SuiteCutExecutionStep[], categories: readonly SuiteCutStepCategory[] | undefined): SuiteCutExecutionStep[];
//# sourceMappingURL=steps.d.ts.map