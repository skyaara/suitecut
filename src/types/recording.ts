// SPDX-License-Identifier: MIT
import { type PresentationPage } from '../browser-control.js'

import { type SuiteCutAttemptClock } from './clock.js'
import {
  type SuiteCutCheckpointEvent,
  type SuiteCutEvent,
  type SuiteCutZoomEvent,
} from './events.js'
import { type SuiteCutNativeRecordingTiming } from './media.js'
import { type SuiteCutPage } from './pages.js'
import {
  type EpochMilliseconds,
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
  contentType: 'video/webm'
  capturedAtMs: Milliseconds
  durationMs: Milliseconds
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

export interface SuiteCutCapturedCaptionArtifact {
  id: SuiteCutArtifactId
  attachmentName: string
  role: 'captions'
  contentType: 'application/json'
  createdAtMs: Milliseconds
  sourceEventId: SuiteCutEventId
}

export type SuiteCutCapturedArtifact =
  | SuiteCutCapturedCheckpointArtifact
  | SuiteCutCapturedNarrationArtifact
  | SuiteCutCapturedCaptionArtifact

export interface SuiteCutCapturedVideo {
  artifactId: SuiteCutArtifactId
  pageId: SuiteCutPageId
  attachmentName: string
  firstFrameEpochMs: EpochMilliseconds
  sourceStartedAtMs: Milliseconds
  nativeRecording?: SuiteCutNativeRecordingTiming
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

export interface SuiteCutRecordingSession<P = PresentationPage> {
  readonly playLiveZoom?: (
    event: SuiteCutZoomEvent,
    playback?: () => Promise<void>,
  ) => Promise<void>
  readonly retainArtifacts?: boolean
  readonly streamFailure?: Promise<never>
  readonly attemptId: SuiteCutAttemptId
  readonly clock: SuiteCutAttemptClock
  readonly pages: Map<SuiteCutPageId, SuiteCutPage>
  readonly activePageId: SuiteCutPageId
  readonly events: SuiteCutEvent[]
  readonly artifacts: SuiteCutCapturedArtifact[]
  readonly videos: SuiteCutCapturedVideo[]

  now(): Milliseconds
  pageFor(pageId: SuiteCutPageId): P
  pageIdFor(page: P): SuiteCutPageId
  selectPage(page: P): SuiteCutPageId
  nextEventId(): SuiteCutEventId
  record(event: SuiteCutEvent): void
  captureCheckpoint(event: SuiteCutCheckpointEvent): Promise<void>
  withCapturePaused<T>(operation: () => Promise<T>): Promise<T>
  endRecording(): Promise<void>
  seal(): Promise<SuiteCutEventAttachment>
}
