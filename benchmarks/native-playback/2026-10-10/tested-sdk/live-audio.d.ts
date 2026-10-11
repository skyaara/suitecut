export declare const LIVE_AUDIO_DELAY_MS = 150;
/** Timestamped stereo PCM with bounded retention and silence for missing samples. */
export declare class LiveAudioBuffer {
    private chunks;
    clear(): void;
    push(at: number, pcm: Buffer): void;
    read(at: number, samples: number): Buffer;
}
export interface LiveAudioInput {
    url: string;
    write(at: number, samples: number, signal: AbortSignal): Promise<void>;
    close(): void;
}
export interface LiveAudioTransport {
    url: string;
    buffer: LiveAudioBuffer;
    activate(generation: number): Promise<void>;
    createInput(): LiveAudioInput;
    close(): Promise<void>;
}
/** Binary WebSocket capture and a continuous loopback HTTP input for FFmpeg. */
export declare function createLiveAudioTransport(onPCM: (generation: number, timestamp: number, pcm: Buffer) => void, onEnded: (generation: number) => void): Promise<LiveAudioTransport>;
//# sourceMappingURL=live-audio.d.ts.map