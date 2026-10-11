import {
  type EpochMilliseconds,
  type Milliseconds,
  type SuiteCutArtifactId,
  type SuiteCutMediaId,
  type SuiteCutPageId,
} from './primitives.js'

export interface SuiteCutVideoStream {
  kind: 'video'
  codec: string
  pixelFormat?: string
  colorRange?: 'full' | 'limited'
  colorSpace?: string
  colorTransfer?: string
  colorPrimaries?: string
  width: number
  height: number
  durationMs: Milliseconds
  frameRate: number
  timeBase?: string
  hasVariableFrameRate: boolean
}

export interface SuiteCutAudioStream {
  kind: 'audio'
  codec: string
  channels: number
  sampleRate: number
  durationMs: Milliseconds
}

export type SuiteCutMediaStream = SuiteCutVideoStream | SuiteCutAudioStream

export interface SuiteCutMedia {
  id: SuiteCutMediaId
  artifactId: SuiteCutArtifactId
  formatName: string
  durationMs: Milliseconds
  streams: SuiteCutMediaStream[]
}

export interface SuiteCutVideoTiming {
  pageId: SuiteCutPageId
  mediaId: SuiteCutMediaId
  firstFrameEpochMs: EpochMilliseconds
  sourceStartedAtMs: Milliseconds
}
