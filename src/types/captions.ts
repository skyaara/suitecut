import { type Milliseconds, type SuiteCutEventId } from './primitives.js'

export interface SuiteCutWordTiming {
  text: string
  startOffset: number
  endOffset: number
  startMs: Milliseconds
  endMs: Milliseconds
}

export interface SuiteCutWordTimingArtifact {
  schemaVersion: 1
  type: 'word-timings'
  sourceEventId: SuiteCutEventId
  text: string
  durationMs: Milliseconds
  words: SuiteCutWordTiming[]
}
