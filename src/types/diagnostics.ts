import { type Milliseconds, type SuiteCutArtifactId, type SuiteCutEventId } from './primitives.js'

export type SuiteCutDiagnosticLevel = 'info' | 'warning' | 'error'

export type SuiteCutDiagnosticCode =
  | 'INVALID_EVENT'
  | 'MISSING_ATTACHMENT'
  | 'MISSING_SOURCE_VIDEO'
  | 'MEDIA_PROBE_FAILED'
  | 'SCREENCAST_START_FAILED'
  | 'FIRST_FRAME_TIMESTAMP_MISSING'
  | 'VIDEO_TIMING_INVALID'
  | 'POINTER_OUTSIDE_VIEWPORT'
  | 'LOCATOR_GEOMETRY_MISSING'
  | 'OPTIONAL_TRACK_OMITTED'
  | 'RENDER_FAILED'
  | 'FFMPEG_FAILED'

export interface SuiteCutDiagnostic {
  level: SuiteCutDiagnosticLevel
  code: SuiteCutDiagnosticCode
  message: string
  atMs?: Milliseconds
  eventId?: SuiteCutEventId
  artifactId?: SuiteCutArtifactId
}
