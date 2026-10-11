import { type SuiteCutArtifactSink } from './artifact-sink.js';
import { type NativeBrowser } from './native-browser.js';
import { type NativePage } from './native-page.js';
import { type SuiteCutCaptureOptions } from './schemas.js';
import { type SuiteCutRecordingSession } from './types.js';
export declare function createNativeRecordingSession(options: {
    source: NativeBrowser;
    output: SuiteCutArtifactSink;
    ffmpegPath: string;
    capture: SuiteCutCaptureOptions & {
        audio: boolean;
    };
    signal?: AbortSignal;
}): Promise<{
    session: SuiteCutRecordingSession<NativePage>;
    page: NativePage;
    addSource: (source: NativeBrowser) => Promise<NativePage>;
}>;
//# sourceMappingURL=native-session.d.ts.map