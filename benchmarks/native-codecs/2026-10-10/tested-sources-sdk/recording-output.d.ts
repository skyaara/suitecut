import { type SuiteCutArtifactSink } from './artifact-sink.js';
import { type SuiteCutArtifact, type SuiteCutAttempt, type SuiteCutEventAttachment, type SuiteCutPathKind, type SuiteCutMedia } from './types.js';
export declare function rawArtifactSink(outputDirectory: string): SuiteCutArtifactSink;
export declare function createAttempt(captured: SuiteCutEventAttachment, outputDirectory: string, outputFile: string, pathKind: SuiteCutPathKind, errors: readonly Error[], probe?: (inputs: readonly {
    artifact: SuiteCutArtifact;
    path: string;
}[]) => Promise<SuiteCutMedia[]>): Promise<SuiteCutAttempt>;
//# sourceMappingURL=recording-output.d.ts.map