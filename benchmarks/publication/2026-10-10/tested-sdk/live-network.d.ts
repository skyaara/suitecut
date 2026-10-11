export interface NetworkSample {
    blockedMs: number;
    queuedBytes: number;
    sentKbps: number;
}
/** Transparent bounded transport; measures socket pressure independently of encoding. */
export declare function createLiveNetwork(destination: string): Promise<{
    url: string;
    pressure(): {
        networkBlockedMs: number;
        networkQueuedBytes: number;
    };
    sample(): NetworkSample;
    close(): Promise<void>;
}>;
//# sourceMappingURL=live-network.d.ts.map