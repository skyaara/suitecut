import { type NetworkSample } from './live-network.js'

/** Socket-based reduction with conservative recovery after measured output congestion. */
export class LiveBitrate {
  current: number
  private changedAt = -Infinity
  private pressureAt: number | undefined
  private quietAt: number | undefined
  private healthyAt: number | undefined
  private throughput: number[] = []

  constructor(private readonly maximum: number) {
    this.current = maximum
  }

  resetConnection(): void {
    this.pressureAt = undefined
    this.quietAt = undefined
    this.healthyAt = undefined
    this.throughput = []
  }

  update(now: number, sample: NetworkSample, deliveryHealthy: boolean): number | undefined {
    const pressured = sample.blockedMs >= 200
    if (pressured) {
      this.quietAt = undefined
      this.healthyAt = undefined
      this.pressureAt ??= now
      if (sample.sentKbps > 0) this.throughput.push(sample.sentKbps)
      this.throughput = this.throughput.slice(-5)
      if (now - this.pressureAt < 5000 || now - this.changedAt < 30_000) return
      const floor = Math.min(this.maximum, Math.max(100, this.maximum / 4))
      const estimate = this.throughput.length
        ? this.throughput.reduce((a, b) => a + b, 0) / this.throughput.length
        : Infinity
      // Reserve room for AAC and framing. Limit each reduction to 25–50 percent.
      const next = Math.round(
        Math.max(floor, this.current / 2, Math.min(this.current * 0.75, estimate * 0.85 - 160)),
      )
      if (next >= this.current) return
      this.current = next
      this.changedAt = now
      this.resetConnection()
      return next
    }
    this.quietAt ??= now
    if (now - this.quietAt >= 2000) {
      this.pressureAt = undefined
      this.throughput = []
    }
    if (!deliveryHealthy || sample.queuedBytes > 0) {
      this.healthyAt = undefined
      return
    }
    this.healthyAt ??= now
    if (
      this.current < this.maximum &&
      now - this.healthyAt >= 300_000 &&
      now - this.changedAt >= 300_000
    ) {
      this.current = Math.min(
        this.maximum,
        this.current + Math.max(100, Math.round(this.maximum * 0.1)),
      )
      this.changedAt = now
      this.resetConnection()
      return this.current
    }
    return undefined
  }
}
