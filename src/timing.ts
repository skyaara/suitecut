import { type EpochMilliseconds, type Milliseconds } from './types.js'

function requireFinite(value: number, name: string): void {
  if (!Number.isFinite(value)) {
    throw new RangeError(`${name} must be finite`)
  }
}

export function toAttemptTime(
  epochMs: EpochMilliseconds,
  originEpochMs: EpochMilliseconds,
): Milliseconds {
  requireFinite(epochMs, 'Epoch timestamp')
  requireFinite(originEpochMs, 'Attempt origin')
  return epochMs - originEpochMs
}

export function deriveSourceStartedAt(
  firstFrameEpochMs: EpochMilliseconds,
  originEpochMs: EpochMilliseconds,
): Milliseconds {
  return toAttemptTime(firstFrameEpochMs, originEpochMs)
}

export function toSourceVideoTime(
  attemptTimeMs: Milliseconds,
  sourceStartedAtMs: Milliseconds,
): Milliseconds {
  requireFinite(attemptTimeMs, 'Attempt time')
  requireFinite(sourceStartedAtMs, 'Source-video start')
  return attemptTimeMs - sourceStartedAtMs
}
