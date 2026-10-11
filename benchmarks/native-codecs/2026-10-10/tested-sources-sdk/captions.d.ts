import * as z from 'zod';
import { type SuiteCutWordTimingArtifact } from './types.js';
import { type UntrustedInput } from './untrusted.js';
export declare const SuiteCutWordTimingSchema: z.ZodObject<{
    text: z.ZodString;
    startOffset: z.ZodNumber;
    endOffset: z.ZodNumber;
    startMs: z.ZodNumber;
    endMs: z.ZodNumber;
}, z.core.$strict>;
export declare const SuiteCutWordTimingArtifactSchema: z.ZodObject<{
    schemaVersion: z.ZodLiteral<1>;
    type: z.ZodLiteral<"word-timings">;
    sourceEventId: z.ZodString;
    text: z.ZodString;
    durationMs: z.ZodNumber;
    words: z.ZodArray<z.ZodObject<{
        text: z.ZodString;
        startOffset: z.ZodNumber;
        endOffset: z.ZodNumber;
        startMs: z.ZodNumber;
        endMs: z.ZodNumber;
    }, z.core.$strict>>;
}, z.core.$strict>;
export declare function decodeWordTimingArtifact(input: UntrustedInput): SuiteCutWordTimingArtifact;
export declare function encodeWordTimingArtifact(artifact: SuiteCutWordTimingArtifact): string;
//# sourceMappingURL=captions.d.ts.map