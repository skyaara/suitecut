import { type SuiteCutNarrationWorkerRequest, type SuiteCutNarrationWorkerSuccess } from './narration-worker-protocol.js';
export type SuiteCutNarrationRequest = Omit<SuiteCutNarrationWorkerRequest, 'type'>;
export declare class SuiteCutNarrationWorker {
    #private;
    constructor(signal?: AbortSignal);
    synthesize(request: SuiteCutNarrationRequest): Promise<SuiteCutNarrationWorkerSuccess>;
    close(): Promise<void>;
}
//# sourceMappingURL=narration-worker-client.d.ts.map