import { type EpochMilliseconds, type ISODateTime } from './primitives.js'

export interface SuiteCutAttemptClock {
  originEpochMs: EpochMilliseconds
  originMonotonicMs: number
  startedAt: ISODateTime
}
