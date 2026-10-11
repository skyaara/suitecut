import { type SuiteCutZoomEvent } from './types.js';
/** Crops live JPEGs without altering application layout or recorded source frames. */
export declare function createLiveCamera(signal?: AbortSignal): {
    zoom: (event: SuiteCutZoomEvent, playback?: () => Promise<void>) => Promise<void>;
    render: (data: Buffer, pageId: string) => Promise<Buffer>;
    stop: () => void;
};
//# sourceMappingURL=live-zoom.d.ts.map