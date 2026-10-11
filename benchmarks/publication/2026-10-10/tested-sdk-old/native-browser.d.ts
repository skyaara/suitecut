import { type UntrustedInput } from './untrusted.js';
export type NativePixelFormat = 'bgra' | 'i420';
export type NativeBrowserState = 'running' | 'closing' | 'closed' | 'failed';
export interface NativeBrowserOptions {
    /** Absolute path to the separately built suitecut-browser executable. */
    executablePath: string;
    width?: number;
    height?: number;
    deviceScaleFactor?: 1 | 2;
    framesPerSecond?: 30 | 60;
    /** BGRA8 for exact pixels; planar BT.709 limited-range I420 for lower-bandwidth streaming. */
    pixelFormat?: NativePixelFormat;
    startupTimeoutMs?: number;
    signal?: AbortSignal;
}
export interface NativeVideoFrame {
    /** Owned packed BGRA8 or contiguous I420 Y/U/V planes (BT.709 limited range), top-left origin. */
    data: Buffer;
    pixelFormat: NativePixelFormat;
    width: number;
    height: number;
    /** Capture time in this Node process's performance.now() clock. */
    timestampMs: number;
    /** Cumulative frames replaced by the native writer under backpressure. */
    droppedFrames: number;
}
export interface NativeAudioPacket {
    /** Owned interleaved signed 16-bit little-endian stereo PCM at 48 kHz. */
    data: Buffer;
    frames: number;
    /** Estimated first-sample time in this Node process's performance.now() clock. */
    timestampMs: number;
}
export interface NativeBrowser {
    readonly state: NativeBrowserState;
    readonly pixelFormat: NativePixelFormat;
    readonly width: number;
    readonly height: number;
    readonly framesPerSecond: 30 | 60;
    readonly cefVersion: string;
    /** Rejects on runtime, renderer, audio, protocol, or subscriber failure. */
    readonly failure: Promise<never>;
    /** Settles only after the native process and its stdio have closed. */
    readonly closed: Promise<void>;
    /** Navigates an HTTP(S) page and waits for its main-frame load. */
    navigate(url: string): Promise<void>;
    /** Executes a DevTools method through private process IPC, without opening a debugging port. */
    sendDevToolsCommand(method: string, params?: Record<string, UntrustedInput>): Promise<UntrustedInput>;
    /** Subscribers must return promptly; buffers may be retained but must not be modified. */
    onFrame(listener: (frame: NativeVideoFrame) => void): () => void;
    onAudio(listener: (packet: NativeAudioPacket) => void): () => void;
    /** Idempotent; closes CEF gracefully, then kills the helper if shutdown stalls. */
    close(): Promise<void>;
}
/** Launches an isolated CEF source with raw frame/audio capture and private command IPC. */
export declare function launchNativeBrowser(options: NativeBrowserOptions): Promise<NativeBrowser>;
//# sourceMappingURL=native-browser.d.ts.map