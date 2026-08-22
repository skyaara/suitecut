import {
  SuiteCutCapturedGeometrySchema,
  SuiteCutCheckpointInputSchema,
  type SuiteCutCheckpointOptions,
  type SuiteCutHighlightOptions,
  SuiteCutHighlightOptionsSchema,
  SuiteCutHoldInputSchema,
  SuiteCutNarrationInputSchema,
  type SuiteCutNarrationOptions,
  type SuiteCutRect,
  type SuiteCutViewport,
  SuiteCutViewportSchema,
  type SuiteCutZoomOptions,
  SuiteCutZoomOptionsSchema,
} from './schemas.js'

export function parseNarrationInput(text: string, options: SuiteCutNarrationOptions | undefined) {
  const input = options === undefined ? { text } : { text, options }
  return SuiteCutNarrationInputSchema.parse(input)
}

export function parseCheckpointInput(
  label: string,
  options: SuiteCutCheckpointOptions | undefined,
) {
  const input = options === undefined ? { label } : { label, options }
  return SuiteCutCheckpointInputSchema.parse(input)
}

export function parseHoldInput(durationMs: number) {
  return SuiteCutHoldInputSchema.parse({ durationMs }).durationMs
}

export function parseHighlightOptions(options: SuiteCutHighlightOptions) {
  return SuiteCutHighlightOptionsSchema.parse(options)
}

export function parseZoomOptions(options: SuiteCutZoomOptions) {
  return SuiteCutZoomOptionsSchema.parse(options)
}

export function parseCapturedGeometry(rect: SuiteCutRect, viewport: SuiteCutViewport) {
  return SuiteCutCapturedGeometrySchema.parse({ rect, viewport })
}

export function parseCapturedViewport(viewport: SuiteCutViewport) {
  return SuiteCutViewportSchema.parse(viewport)
}
