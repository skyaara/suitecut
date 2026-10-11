import { type LiveAudioTransport } from './live-audio.js';
import { type LiveEncodedOutput, type SuiteCutLiveStream } from './live-stream.js';
import { type SuiteCutCaptureSize, type SuiteCutStreamOptions } from './schemas.js';
type VideoFactory = (options: SuiteCutStreamOptions, output: LiveEncodedOutput, signal: AbortSignal) => SuiteCutLiveStream;
/** Audio and publisher survive video encoder handoffs. No RTMP handshake occurs on bitrate changes. */
export declare function createPersistentLiveStream(ffmpeg: string, options: SuiteCutStreamOptions, fps: number, _size: SuiteCutCaptureSize, signal: AbortSignal | undefined, audio: LiveAudioTransport | undefined, makeVideo: VideoFactory): SuiteCutLiveStream;
export {};
//# sourceMappingURL=live-publisher.d.ts.map