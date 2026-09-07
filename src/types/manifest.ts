import { type SuiteCutArtifact } from './artifacts.js'
import { type SuiteCutAttemptClock } from './clock.js'
import { type SuiteCutDiagnostic } from './diagnostics.js'
import { type SuiteCutError, type SuiteCutSourceLocation } from './errors.js'
import { type SuiteCutEvent } from './events.js'
import { type SuiteCutMedia, type SuiteCutVideoTiming } from './media.js'
import { type SuiteCutPage } from './pages.js'
import {
  type FilePath,
  type ISODateTime,
  type Milliseconds,
  type SuiteCutAttemptId,
  type SuiteCutTestId,
} from './primitives.js'
import { type SuiteCutExecutionStep } from './steps.js'

export type SuiteCutTestStatus = 'passed' | 'failed' | 'timedOut' | 'skipped' | 'interrupted'

export type SuiteCutRunStatus = 'passed' | 'failed' | 'timedout' | 'interrupted'

export interface SuiteCutAttempt {
  id: SuiteCutAttemptId
  retry: number
  status: SuiteCutTestStatus
  clock: SuiteCutAttemptClock
  durationMs: Milliseconds
  pages: SuiteCutPage[]
  events: SuiteCutEvent[]
  steps: SuiteCutExecutionStep[]
  artifacts: SuiteCutArtifact[]
  media: SuiteCutMedia[]
  videoTiming: SuiteCutVideoTiming[]
  errors: SuiteCutError[]
  diagnostics: SuiteCutDiagnostic[]
}

export interface SuiteCutTest {
  id: SuiteCutTestId
  order: number
  title: string
  titlePath: string[]
  projectName: string
  location: SuiteCutSourceLocation
  attempts: SuiteCutAttempt[]
}

export interface SuiteCutManifest {
  schemaVersion: 1
  startedAt: ISODateTime
  endedAt: ISODateTime
  status: SuiteCutRunStatus
  rootDirectory: FilePath
  tests: SuiteCutTest[]
}
