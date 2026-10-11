import { type SuiteCutPathKind } from '../schemas.js'

import {
  type FilePath,
  type Milliseconds,
  type MimeType,
  type SuiteCutArtifactId,
  type SuiteCutEventId,
  type SuiteCutPageId,
} from './primitives.js'

export type { SuiteCutPathKind } from '../schemas.js'

export type SuiteCutArtifactRole =
  | 'checkpoint'
  | 'source-video'
  | 'playwright-trace'
  | 'narration-audio'
  | 'captions'
  | 'rendered-video'
  | 'render-report'
  | 'other'

export interface SuiteCutArtifact {
  id: SuiteCutArtifactId
  name: string
  role: SuiteCutArtifactRole
  contentType: MimeType
  path: FilePath
  pathKind: SuiteCutPathKind
  sizeBytes?: number
  pageId?: SuiteCutPageId
  createdAtMs?: Milliseconds
  sourceEventId?: SuiteCutEventId
  provider?: string
  voice?: string
}
