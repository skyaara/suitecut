/** Incremental FLV framing with strict size limits for encoder output. */
export declare class LiveFlvParser {
    private pending;
    private filled;
    private header;
    /** Consumes fragments in order, awaiting each complete tag before continuing. */
    push(chunk: Buffer, consume: (tag: Buffer) => Promise<void>): Promise<void>;
}
export declare function flvTimestamp(tag: Buffer): number;
export declare function stampFlv(tag: Buffer, timestamp: number): Buffer;
export declare const LIVE_FLV_HEADER: Buffer<ArrayBuffer>;
//# sourceMappingURL=live-flv.d.ts.map