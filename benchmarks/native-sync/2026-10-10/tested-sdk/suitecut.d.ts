import { type Locator, type Page } from 'playwright';
import { type SuiteCutArtifactSink } from './artifact-sink.js';
import { type PresentationPage, type PresentationLocator } from './browser-control.js';
import { type SuiteCutCaptureOptions, type SuiteCutFixture } from './fixtures.js';
import { type SuiteCutNarrationPipeline } from './narration.js';
import { type SuiteCutRecordingSession } from './types.js';
export declare function createRecordingSession({ captureOptions, output, page, signal, }: {
    captureOptions: SuiteCutCaptureOptions;
    output: SuiteCutArtifactSink;
    page: Page;
    signal?: AbortSignal;
}): Promise<SuiteCutRecordingSession>;
export declare function createSuiteCutFixture<P extends PresentationPage = Page, L extends PresentationLocator<P> = Locator & PresentationLocator<P>>(session: SuiteCutRecordingSession<P>, narration: SuiteCutNarrationPipeline, captureOptions: SuiteCutCaptureOptions): SuiteCutFixture<P, L>;
//# sourceMappingURL=suitecut.d.ts.map