/** Measurements never contain publisher URLs, process arguments, or encoder stderr. */
export interface LiveStreamDiagnostic {
  event:
    | 'recovering'
    | 'caught-up'
    | 'resynchronized'
    | 'persistent-congestion'
    | 'stalled'
    | 'bitrate-adjusted'
    | 'bitrate-change-failed'
    | 'bitrate-changing'
  bottleneck:
    | 'network-backpressure'
    | 'frame-processing'
    | 'video-input-backpressure'
    | 'audio-input-backpressure'
    | 'unknown'
  lagMs: number
  processingMs: number
  videoWriteMs: number
  audioWriteMs: number
  skippedMs: number
  outputLagMs?: number
  progressAgeMs?: number
  bitrateKbps?: number
  previousBitrateKbps?: number
  networkQueuedBytes?: number
  networkBlockedMs?: number
  sentKbps?: number
  incidents: number
}

export const LIVE_RECOVERY_WINDOW_MS = 10_000
export const LIVE_NO_PROGRESS_MS = 20_000
export const LIVE_FRAME_BUFFER_BYTES = 64 * 1024 * 1024

/** Bounded incident history with hysteresis; one slow frame is not a new incident. */
export class LiveCongestion {
  private incidents: number[] = []
  private recoveringAt: number | undefined
  private healthyAt: number | undefined
  private persistent = false
  private measurements = {
    networkBlockedMs: 0,
    networkQueuedBytes: 0,
    processingMs: 0,
    videoWriteMs: 0,
    audioWriteMs: 0,
    outputLagMs: 0,
    progressAgeMs: 0,
  }

  constructor(private readonly report?: (event: LiveStreamDiagnostic) => void) {}

  measure(values: Partial<typeof this.measurements>): void {
    this.measurements = { ...this.measurements, ...values }
  }

  private emit(event: LiveStreamDiagnostic['event'], lagMs: number, skippedMs = 0): void {
    const { processingMs, videoWriteMs, audioWriteMs, networkBlockedMs } = this.measurements
    const largest = Math.max(processingMs, videoWriteMs, audioWriteMs, networkBlockedMs)
    const bottleneck =
      largest < 100
        ? 'unknown'
        : networkBlockedMs === largest
          ? 'network-backpressure'
          : processingMs === largest
            ? 'frame-processing'
            : videoWriteMs >= audioWriteMs
              ? 'video-input-backpressure'
              : 'audio-input-backpressure'
    // Observability must never interrupt the encoder or surface caller exceptions.
    try {
      void Promise.resolve(
        this.report?.({
          event,
          bottleneck,
          lagMs,
          ...this.measurements,
          skippedMs,
          incidents: this.incidents.length,
        }),
      ).catch(() => undefined)
    } catch {
      /* The stream remains independent of the diagnostic consumer. */
    }
  }

  /** Returns how much capture time to discard, never a request to restart FFmpeg. */
  update(now: number, lagMs: number, forced = false): number {
    this.incidents = this.incidents.filter((at) => now - at < 300_000)
    if (lagMs >= 5000 || forced) {
      this.healthyAt = undefined
      if (this.recoveringAt === undefined) {
        this.recoveringAt = now
        this.incidents.push(now)
        this.emit('recovering', lagMs)
      }
      if (!this.persistent && (this.incidents.length >= 3 || now - this.recoveringAt >= 30_000)) {
        this.persistent = true
        this.emit('persistent-congestion', lagMs)
      }
    } else if (
      lagMs < 1000 &&
      this.measurements.networkBlockedMs < 200 &&
      this.measurements.networkQueuedBytes === 0 &&
      this.measurements.outputLagMs < 500 &&
      this.measurements.progressAgeMs < 1000
    ) {
      this.healthyAt ??= now
      if (this.recoveringAt !== undefined && now - this.healthyAt >= 10_000) {
        this.recoveringAt = undefined
        this.emit('caught-up', lagMs)
      }
      if (now - this.healthyAt >= 300_000) this.persistent = false
    } else {
      this.healthyAt = undefined
    }
    if (forced || lagMs >= (this.persistent ? 1000 : LIVE_RECOVERY_WINDOW_MS)) {
      const skipped = Math.max(0, lagMs)
      this.emit('resynchronized', lagMs, skipped)
      return skipped
    }
    return 0
  }

  stalled(lagMs: number): void {
    this.emit('stalled', lagMs)
  }
}
