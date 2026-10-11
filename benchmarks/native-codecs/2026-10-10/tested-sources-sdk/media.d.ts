import * as z from 'zod';
import { type SuiteCutArtifact, type SuiteCutMedia } from './types.js';
import { type UntrustedInput } from './untrusted.js';
declare const ProbeDocumentSchema: z.ZodObject<{
    streams: z.ZodArray<z.ZodRecord<z.ZodString, z.ZodJSONSchema>>;
    format: z.ZodObject<{
        format_name: z.ZodString;
        duration: z.ZodPipe<z.ZodPipe<z.ZodUnion<readonly [z.ZodNumber, z.ZodString]>, z.ZodTransform<number, string | number>>, z.ZodNumber>;
    }, z.core.$strip>;
}, z.core.$strip>;
/** Decodes the JSON document emitted by FFprobe. */
export declare function decodeProbeDocument(input: UntrustedInput): z.infer<typeof ProbeDocumentSchema>;
/** Reads the measured container duration without constructing manifest media records. */
export declare function probeMediaDurationMs(absolutePath: string, ffmpegPath?: string): Promise<number>;
export declare function probeMedia(artifact: SuiteCutArtifact, absolutePath: string, ffmpegPath?: string): Promise<SuiteCutMedia>;
/** Probes an attempt's artifacts in order with at most four FFprobe processes at once. */
export declare function probeMediaBatch(inputs: readonly {
    artifact: SuiteCutArtifact;
    path: string;
}[]): Promise<SuiteCutMedia[]>;
export {};
//# sourceMappingURL=media.d.ts.map