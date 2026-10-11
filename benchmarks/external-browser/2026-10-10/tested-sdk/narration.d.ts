import { type SuiteCutArtifactSink } from './artifact-sink.js';
import { type SuiteCutAudioPluginReference } from './schemas.js';
import { type SuiteCutNarrationEvent, type SuiteCutRecordingSession, type SuiteCutWordTimingArtifact } from './types.js';
export interface SuiteCutNarrationPipeline {
    enqueue(event: SuiteCutNarrationEvent): Promise<SuiteCutNarrationClip>;
    finish(): Promise<void>;
}
export interface SuiteCutNarrationClip {
    durationMs: number;
    timing?: SuiteCutWordTimingArtifact;
}
export declare function createNarrationPipeline(output: SuiteCutArtifactSink, session: Pick<SuiteCutRecordingSession, 'attemptId' | 'retainArtifacts' | 'artifacts' | 'now'>, audioPlugins?: readonly SuiteCutAudioPluginReference[], signal?: AbortSignal): SuiteCutNarrationPipeline;
//# sourceMappingURL=narration.d.ts.map