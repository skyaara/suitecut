import { type NativePixelFormat } from './native-browser.js';
/** Small Matroska headers; frame pixels are written separately without concatenation. */
export declare class NativeVideoContainer {
    readonly header: Buffer;
    readonly frameBytes: number;
    constructor(width: number, height: number, pixelFormat: NativePixelFormat, fps: number);
    frameHeader(timestampMs: number): Buffer;
}
//# sourceMappingURL=native-video-container.d.ts.map