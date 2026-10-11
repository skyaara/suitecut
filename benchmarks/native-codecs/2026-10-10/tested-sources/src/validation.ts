import {
  SuiteCutCapturedGeometrySchema,
  SuiteCutCaptureOptionsSchema,
  type SuiteCutCaptureOptions,
  SuiteCutCheckpointInputSchema,
  type SuiteCutCheckpointOptions,
  type SuiteCutHighlightOptions,
  SuiteCutHighlightOptionsSchema,
  SuiteCutHoldInputSchema,
  SuiteCutNarrationInputSchema,
  type SuiteCutNarrationOptions,
  type SuiteCutPointerActionOptions,
  SuiteCutPointerActionOptionsSchema,
  type SuiteCutRect,
  type SuiteCutScrollOptions,
  SuiteCutScrollOptionsSchema,
  SuiteCutTypeInputSchema,
  type SuiteCutTypeOptions,
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

export function parsePointerActionOptions(options: SuiteCutPointerActionOptions) {
  return SuiteCutPointerActionOptionsSchema.parse(options)
}

export function parseTypeInput(locatorText: string, options: SuiteCutTypeOptions | undefined) {
  const input = options === undefined ? { text: locatorText } : { text: locatorText, options }
  return SuiteCutTypeInputSchema.parse(input)
}

export function parseScrollOptions(options: SuiteCutScrollOptions) {
  return SuiteCutScrollOptionsSchema.parse(options)
}

export function parseCapturedGeometry(rect: SuiteCutRect, viewport: SuiteCutViewport) {
  return SuiteCutCapturedGeometrySchema.parse({ rect, viewport })
}

export function parseCapturedViewport(viewport: SuiteCutViewport) {
  return SuiteCutViewportSchema.parse(viewport)
}

/** Validates the CSS viewport, physical source size, cadence, and JPEG capture quality. */
export function parseCaptureOptions(options: SuiteCutCaptureOptions): SuiteCutCaptureOptions {
  return SuiteCutCaptureOptionsSchema.parse(options)
}
