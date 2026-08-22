import { type Page } from '@playwright/test'

import { type SuiteCutAttemptClock } from './clock.js'
import { type SuiteCutEvent } from './events.js'
import { type SuiteCutPage } from './pages.js'
import {
  type EpochMilliseconds,
  type FilePath,
  type Milliseconds,
  type MimeType,
  type SuiteCutArtifactId,
  type SuiteCutAttemptId,
  type SuiteCutEventId,
  type SuiteCutPageId,
} from './primitives.js'

export interface SuiteCutCapturedCheckpointArtifact {
  id: SuiteCutArtifactId
  attachmentName: string
  role: 'checkpoint'
  contentType: MimeType
  capturedAtMs: Milliseconds
  pageId: SuiteCutPageId
}

export interface SuiteCutCapturedNarrationArtifact {
  id: SuiteCutArtifactId
  attachmentName: string
  role: 'narration-audio'
  contentType: MimeType
  createdAtMs: Milliseconds
  sourceEventId: SuiteCutEventId
  provider: string
  voice: string
}

export type SuiteCutCapturedArtifact =
  SuiteCutCapturedCheckpointArtifact | SuiteCutCapturedNarrationArtifact

export interface SuiteCutCapturedVideo {
  artifactId: SuiteCutArtifactId
  pageId: SuiteCutPageId
  attachmentName: string
  firstFrameEpochMs: EpochMilliseconds
  sourceStartedAtMs: Milliseconds
}

export interface SuiteCutActiveScreencast {
  pageId: SuiteCutPageId
  page: Page
  outputPath: FilePath
  attachmentName: string
  firstFrameEpochMs?: EpochMilliseconds
}

export interface SuiteCutEventAttachment {
  attemptId: SuiteCutAttemptId
  clock: SuiteCutAttemptClock
  endedAtMs: Milliseconds
  pages: SuiteCutPage[]
  events: SuiteCutEvent[]
  artifacts: SuiteCutCapturedArtifact[]
  videos: SuiteCutCapturedVideo[]
}

export interface SuiteCutRecordingSession {
  readonly attemptId: SuiteCutAttemptId
  readonly clock: SuiteCutAttemptClock
  readonly pages: Map<SuiteCutPageId, SuiteCutPage>
  readonly activePageId: SuiteCutPageId
  readonly events: SuiteCutEvent[]
  readonly artifacts: SuiteCutCapturedArtifact[]
  readonly screencasts: Map<SuiteCutPageId, SuiteCutActiveScreencast>
  readonly videos: SuiteCutCapturedVideo[]

  now(): Milliseconds
  pageFor(pageId: SuiteCutPageId): Page
  pageIdFor(page: Page): SuiteCutPageId
  selectPage(page: Page): SuiteCutPageId
  nextEventId(): SuiteCutEventId
  record(event: SuiteCutEvent): void
  endRecording(): Promise<void>
  seal(): Promise<SuiteCutEventAttachment>
}
