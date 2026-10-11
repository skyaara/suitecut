import * as z from 'zod';
import { type UntrustedInput } from './untrusted.js';
export declare const SuiteCutNarrationWorkerRequestSchema: z.ZodObject<{
    type: z.ZodLiteral<"synthesize">;
    jobId: z.ZodUUID;
    text: z.ZodString;
    provider: z.ZodString;
    voice: z.ZodString;
    speed: z.ZodNumber;
    outputPath: z.ZodString;
    plugin: z.ZodExactOptional<z.ZodObject<{
        provider: z.ZodString;
        module: z.ZodString;
        options: z.ZodExactOptional<z.ZodJSONSchema>;
    }, z.core.$strict>>;
}, z.core.$strict>;
export declare const SuiteCutNarrationWorkerCloseRequestSchema: z.ZodObject<{
    type: z.ZodLiteral<"close">;
}, z.core.$strict>;
export declare const SuiteCutNarrationWorkerMessageSchema: z.ZodUnion<readonly [z.ZodObject<{
    type: z.ZodLiteral<"synthesize">;
    jobId: z.ZodUUID;
    text: z.ZodString;
    provider: z.ZodString;
    voice: z.ZodString;
    speed: z.ZodNumber;
    outputPath: z.ZodString;
    plugin: z.ZodExactOptional<z.ZodObject<{
        provider: z.ZodString;
        module: z.ZodString;
        options: z.ZodExactOptional<z.ZodJSONSchema>;
    }, z.core.$strict>>;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"close">;
}, z.core.$strict>]>;
declare const SuiteCutNarrationWorkerSuccessSchema: z.ZodObject<{
    type: z.ZodLiteral<"synthesized">;
    jobId: z.ZodUUID;
    timing: z.ZodExactOptional<z.ZodObject<{
        text: z.ZodString;
        durationMs: z.ZodNumber;
        words: z.ZodArray<z.ZodObject<{
            text: z.ZodString;
            startOffset: z.ZodNumber;
            endOffset: z.ZodNumber;
            startMs: z.ZodNumber;
            endMs: z.ZodNumber;
        }, z.core.$strict>>;
    }, z.core.$strict>>;
}, z.core.$strict>;
declare const SuiteCutNarrationWorkerFailureSchema: z.ZodObject<{
    type: z.ZodLiteral<"failed">;
    jobId: z.ZodUUID;
    error: z.ZodObject<{
        name: z.ZodString;
        message: z.ZodString;
        stack: z.ZodExactOptional<z.ZodString>;
    }, z.core.$strict>;
}, z.core.$strict>;
export declare const SuiteCutNarrationWorkerResponseSchema: z.ZodDiscriminatedUnion<[z.ZodObject<{
    type: z.ZodLiteral<"synthesized">;
    jobId: z.ZodUUID;
    timing: z.ZodExactOptional<z.ZodObject<{
        text: z.ZodString;
        durationMs: z.ZodNumber;
        words: z.ZodArray<z.ZodObject<{
            text: z.ZodString;
            startOffset: z.ZodNumber;
            endOffset: z.ZodNumber;
            startMs: z.ZodNumber;
            endMs: z.ZodNumber;
        }, z.core.$strict>>;
    }, z.core.$strict>>;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"failed">;
    jobId: z.ZodUUID;
    error: z.ZodObject<{
        name: z.ZodString;
        message: z.ZodString;
        stack: z.ZodExactOptional<z.ZodString>;
    }, z.core.$strict>;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"closed">;
}, z.core.$strict>, z.ZodObject<{
    type: z.ZodLiteral<"close-failed">;
    error: z.ZodObject<{
        name: z.ZodString;
        message: z.ZodString;
        stack: z.ZodExactOptional<z.ZodString>;
    }, z.core.$strict>;
}, z.core.$strict>], "type">;
export type SuiteCutNarrationWorkerRequest = z.infer<typeof SuiteCutNarrationWorkerRequestSchema>;
export type SuiteCutNarrationWorkerMessage = z.infer<typeof SuiteCutNarrationWorkerMessageSchema>;
export type SuiteCutNarrationWorkerSuccess = z.infer<typeof SuiteCutNarrationWorkerSuccessSchema>;
export type SuiteCutNarrationWorkerFailure = z.infer<typeof SuiteCutNarrationWorkerFailureSchema>;
export type SuiteCutNarrationWorkerResponse = z.infer<typeof SuiteCutNarrationWorkerResponseSchema>;
/** Decodes a message before the narration worker executes it. */
export declare function parseNarrationWorkerRequest(input: UntrustedInput): SuiteCutNarrationWorkerRequest;
/** Decodes synthesis and lifecycle messages received by the narration worker. */
export declare function parseNarrationWorkerMessage(input: UntrustedInput): SuiteCutNarrationWorkerMessage;
/** Decodes a response before the main thread resolves a narration job. */
export declare function parseNarrationWorkerResponse(input: UntrustedInput): SuiteCutNarrationWorkerResponse;
/** Returns whether a raw worker message is a valid synthesis request. */
export declare function isNarrationWorkerRequest(input: UntrustedInput): input is SuiteCutNarrationWorkerRequest;
/** Returns whether a raw worker message is a valid synthesis response. */
export declare function isNarrationWorkerResponse(input: UntrustedInput): input is SuiteCutNarrationWorkerResponse;
export {};
//# sourceMappingURL=narration-worker-protocol.d.ts.map