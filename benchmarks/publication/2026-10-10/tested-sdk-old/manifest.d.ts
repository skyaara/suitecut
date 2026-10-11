import * as z from 'zod';
import { type SuiteCutEventAttachment, type SuiteCutManifest } from './types.js';
import { type UntrustedInput } from './untrusted.js';
export declare class SuiteCutSchemaError extends Error {
    readonly path: string;
    readonly code: string;
    constructor(issue: z.core.$ZodIssue);
}
/** Decodes and cross-validates a persisted SuiteCut manifest. */
export declare function decodeManifest(input: UntrustedInput): SuiteCutManifest;
/** Decodes the fixture attachment consumed by the SuiteCut reporter. */
export declare function decodeEventAttachment(input: UntrustedInput): SuiteCutEventAttachment;
//# sourceMappingURL=manifest.d.ts.map