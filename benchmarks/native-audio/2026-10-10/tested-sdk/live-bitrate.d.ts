import { type NetworkSample } from './live-network.js';
/** Socket-based reduction with conservative recovery after measured output congestion. */
export declare class LiveBitrate {
    private readonly maximum;
    current: number;
    private changedAt;
    private pressureAt;
    private quietAt;
    private healthyAt;
    private throughput;
    constructor(maximum: number);
    resetConnection(): void;
    update(now: number, sample: NetworkSample, deliveryHealthy: boolean): number | undefined;
}
//# sourceMappingURL=live-bitrate.d.ts.map