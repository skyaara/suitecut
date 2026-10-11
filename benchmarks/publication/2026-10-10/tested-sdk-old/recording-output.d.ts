import { type SuiteCutArtifactSink } from './artifact-sink.js';
import { type SuiteCutAttempt, type SuiteCutEventAttachment, type SuiteCutPathKind } from './types.js';
export declare function rawArtifactSink(outputDirectory: string): SuiteCutArtifactSink;
export declare function createAttempt(captured: SuiteCutEventAttachment, outputDirectory: string, outputFile: string, pathKind: SuiteCutPathKind, errors: readonly Error[]): Promise<SuiteCutAttempt>;
//# sourceMappingURL=recording-output.d.ts.map