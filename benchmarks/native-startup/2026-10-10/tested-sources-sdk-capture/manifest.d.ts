import * as z from 'zod';
import { type SuiteCutEventAttachment, type SuiteCutManifest } from './types.js';
import { type UntrustedInput } from './untrusted.js';
export declare const MediaSchema: z.ZodObject<{
    id: z.ZodString;
    artifactId: z.ZodString;
    formatName: z.ZodString;
    durationMs: z.ZodNumber;
    streams: z.ZodArray<z.ZodDiscriminatedUnion<[z.ZodObject<{
        kind: z.ZodLiteral<"video">;
        codec: z.ZodString;
        pixelFormat: z.ZodExactOptional<z.ZodString>;
        colorRange: z.ZodExactOptional<z.ZodEnum<{
            full: "full";
            limited: "limited";
        }>>;
        colorSpace: z.ZodExactOptional<z.ZodString>;
        colorTransfer: z.ZodExactOptional<z.ZodString>;
        colorPrimaries: z.ZodExactOptional<z.ZodString>;
        width: z.ZodNumber;
        height: z.ZodNumber;
        durationMs: z.ZodNumber;
        frameRate: z.ZodNumber;
        timeBase: z.ZodExactOptional<z.ZodString>;
        hasVariableFrameRate: z.ZodBoolean;
    }, z.core.$strict>, z.ZodObject<{
        kind: z.ZodLiteral<"audio">;
        codec: z.ZodString;
        channels: z.ZodNumber;
        sampleRate: z.ZodNumber;
        durationMs: z.ZodNumber;
    }, z.core.$strict>], "kind">>;
}, z.core.$strict>;
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