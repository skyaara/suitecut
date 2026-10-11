import { type NativeAudioPacket } from './native-browser.js';
/** Reblocks arbitrary CEF callback sizes into the publisher's 20 ms PCM packets. */
export declare class NativeAudioAssembler {
    private readonly emit;
    private pending;
    private at;
    constructor(emit: (timestampMs: number, pcm: Buffer) => void);
    clear(): void;
    push(packet: NativeAudioPacket): void;
}
//# sourceMappingURL=native-audio.d.ts.map