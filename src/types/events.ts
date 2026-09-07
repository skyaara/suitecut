import {
  type SuiteCutHighlightOptions,
  type SuiteCutNarrationProvider,
  type SuiteCutNarrationVoice,
  type SuiteCutZoomOptions,
} from '../schemas.js'

import { type SuiteCutPoint, type SuiteCutRect, type SuiteCutViewport } from './geometry.js'
import {
  type Milliseconds,
  type SuiteCutArtifactId,
  type SuiteCutEventId,
  type SuiteCutPageId,
} from './primitives.js'

export type {
  SuiteCutAnimationOptions,
  SuiteCutBorderStyle,
  SuiteCutEasing,
  SuiteCutHighlightMode,
  SuiteCutHighlightOptions,
  SuiteCutNarrationProvider,
  SuiteCutNarrationVoice,
  SuiteCutVisualAnimation,
  SuiteCutZoomOptions,
} from '../schemas.js'

export interface SuiteCutEventBase {
  id: SuiteCutEventId
  atMs: Milliseconds
}

export interface SuiteCutPageEventBase extends SuiteCutEventBase {
  pageId: SuiteCutPageId
}

export type SuiteCutPageSelectionReason = 'opened' | 'closed' | 'author' | 'interaction'

export interface SuiteCutPageSelectionEvent extends SuiteCutPageEventBase {
  type: 'page-selected'
  reason: SuiteCutPageSelectionReason
}

export interface SuiteCutNarrationEvent extends SuiteCutPageEventBase {
  type: 'narration'
  text: string
  provider?: SuiteCutNarrationProvider
  voice?: SuiteCutNarrationVoice
  speed?: number
  caption?: string
}

export interface SuiteCutCheckpointEvent extends SuiteCutPageEventBase {
  type: 'checkpoint'
  label: string
  artifactId: SuiteCutArtifactId
  durationMs: Milliseconds
  viewport: SuiteCutViewport
}

export type SuiteCutPointerType = 'mouse' | 'pen' | 'touch'

export type SuiteCutPointerButton = 'none' | 'left' | 'middle' | 'right' | 'back' | 'forward'

export interface SuiteCutPointerMoveEvent extends SuiteCutPageEventBase {
  type: 'pointer-move'
  point: SuiteCutPoint
  pointerType: SuiteCutPointerType
  viewport: SuiteCutViewport
  targetRect?: SuiteCutRect
}

export interface SuiteCutPointerButtonEvent extends SuiteCutPageEventBase {
  type: 'pointer-down' | 'pointer-up' | 'click'
  point: SuiteCutPoint
  pointerType: SuiteCutPointerType
  button: SuiteCutPointerButton
  viewport: SuiteCutViewport
  targetRect?: SuiteCutRect
}

export interface SuiteCutHighlightEvent extends SuiteCutPageEventBase {
  type: 'highlight'
  rect: SuiteCutRect
  viewport: SuiteCutViewport
  options: SuiteCutHighlightOptions
}

export interface SuiteCutZoomEvent extends SuiteCutPageEventBase {
  type: 'zoom'
  rect: SuiteCutRect
  viewport: SuiteCutViewport
  options: SuiteCutZoomOptions
}

export type SuiteCutHoldReason = 'author' | 'narration' | 'checkpoint'

export interface SuiteCutHoldEvent extends SuiteCutPageEventBase {
  type: 'hold'
  durationMs: Milliseconds
  reason: SuiteCutHoldReason
}

export type SuiteCutEvent =
  | SuiteCutPageSelectionEvent
  | SuiteCutNarrationEvent
  | SuiteCutCheckpointEvent
  | SuiteCutPointerMoveEvent
  | SuiteCutPointerButtonEvent
  | SuiteCutHighlightEvent
  | SuiteCutZoomEvent
  | SuiteCutHoldEvent
