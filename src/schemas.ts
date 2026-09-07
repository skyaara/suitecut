import * as z from 'zod'

import { MAX_SUITE_CUT_ZOOM_SCALE } from './constants.js'

const NonEmptyStringSchema = z
  .string()
  .refine((value) => value.trim().length > 0, { message: 'must not be empty' })
const NonNegativeNumberSchema = z.number().nonnegative()
const PositiveNumberSchema = z.number().positive()
const OpacitySchema = z.number().min(0).max(1)

export const SuiteCutNonEmptyTextSchema = NonEmptyStringSchema
export const SuiteCutPositiveDurationSchema = PositiveNumberSchema
export const SuiteCutNarrationVoiceSchema = NonEmptyStringSchema
export const SuiteCutNarrationProviderSchema = z
  .string()
  .trim()
  .min(1)
  .regex(/^[a-z0-9]+(?:[._-][a-z0-9]+)*$/u, {
    message: 'must use lowercase letters, numbers, dots, underscores, or hyphens',
  })
export const SuiteCutJsonValueSchema = z.json()

export const SuiteCutAudioPluginReferenceSchema = z.strictObject({
  provider: SuiteCutNarrationProviderSchema,
  module: z.string().trim().min(1),
  options: SuiteCutJsonValueSchema.exactOptional(),
})

export const SuiteCutNarrationOptionsSchema = z.strictObject({
  provider: SuiteCutNarrationProviderSchema.exactOptional(),
  voice: SuiteCutNarrationVoiceSchema.exactOptional(),
  speed: z.number().min(0.5).max(2).exactOptional(),
  caption: z.string().exactOptional(),
})

export const SuiteCutCheckpointOptionsSchema = z.strictObject({
  durationMs: PositiveNumberSchema.max(10_000).exactOptional(),
})

export const SuiteCutCaptureViewportSchema = z.strictObject({
  width: z.number().int().positive().max(7680),
  height: z.number().int().positive().max(4320),
})

export const SuiteCutCaptureSizeSchema = z.strictObject({
  width: z.number().int().positive().max(7680).multipleOf(2),
  height: z.number().int().positive().max(4320).multipleOf(2),
})

export const SuiteCutReconnectOptionsSchema = z.strictObject({
  maxAttempts: z.number().int().nonnegative().max(1000).exactOptional(),
  initialDelayMs: z.number().int().min(100).max(60_000).exactOptional(),
  maxDelayMs: z.number().int().min(100).max(300_000).exactOptional(),
})

export const SuiteCutStreamOptionsSchema = z.strictObject({
  url: z.string().refine(
    (value) => {
      try {
        const url = new URL(value)
        return (
          ['rtmp:', 'rtmps:'].includes(url.protocol) &&
          url.hostname.length > 0 &&
          url.pathname.length > 1 &&
          !/\s/u.test(value) &&
          Array.from(value).every((character) => character.charCodeAt(0) >= 32)
        )
      } catch {
        return false
      }
    },
    { message: 'must be an RTMP or RTMPS publish URL with an application and stream path' },
  ),
  size: SuiteCutCaptureSizeSchema.exactOptional(),
  reconnect: z.union([z.literal(false), SuiteCutReconnectOptionsSchema]).exactOptional(),
  bitrateKbps: z.number().int().min(100).max(50_000).exactOptional(),
})

export const SuiteCutCaptureOptionsSchema = z.strictObject({
  viewport: SuiteCutCaptureViewportSchema.exactOptional(),
  size: SuiteCutCaptureSizeSchema.exactOptional(),
  framesPerSecond: z.union([z.literal(30), z.literal(60)]).exactOptional(),
  quality: z.number().int().min(1).max(100).exactOptional(),
  narrationTailMs: NonNegativeNumberSchema.exactOptional(),
  stream: SuiteCutStreamOptionsSchema.exactOptional(),
})

export const SuiteCutPointerActionOptionsSchema = z.strictObject({
  moveDurationMs: NonNegativeNumberSchema.exactOptional(),
  settleMs: NonNegativeNumberSchema.exactOptional(),
  waitForAnimations: z.boolean().exactOptional(),
  animationTimeoutMs: NonNegativeNumberSchema.exactOptional(),
})

export const SuiteCutTypeOptionsSchema = z.strictObject({
  delayMs: NonNegativeNumberSchema.exactOptional(),
  settleMs: NonNegativeNumberSchema.exactOptional(),
  clearExisting: z.boolean().exactOptional(),
})

export const SuiteCutTypeInputSchema = z.strictObject({
  text: z.string().min(1),
  options: SuiteCutTypeOptionsSchema.exactOptional(),
})

export const SuiteCutScrollBehaviorSchema = z.enum(['auto', 'smooth'])
export const SuiteCutScrollAlignmentSchema = z.enum(['start', 'center', 'end', 'nearest'])
export const SuiteCutScrollOptionsSchema = z.strictObject({
  behavior: SuiteCutScrollBehaviorSchema.exactOptional(),
  block: SuiteCutScrollAlignmentSchema.exactOptional(),
  inline: SuiteCutScrollAlignmentSchema.exactOptional(),
  settleMs: NonNegativeNumberSchema.exactOptional(),
  timeoutMs: PositiveNumberSchema.exactOptional(),
})

export const SuiteCutNarrationInputSchema = z.strictObject({
  text: SuiteCutNonEmptyTextSchema,
  options: SuiteCutNarrationOptionsSchema.exactOptional(),
})

export const SuiteCutCheckpointInputSchema = z.strictObject({
  label: SuiteCutNonEmptyTextSchema,
  options: SuiteCutCheckpointOptionsSchema.exactOptional(),
})

export const SuiteCutHoldInputSchema = z.strictObject({
  durationMs: SuiteCutPositiveDurationSchema,
})

export const SuiteCutEasingSchema = z.enum(['linear', 'ease-in', 'ease-out', 'ease-in-out'])

export const SuiteCutVisualAnimationSchema = z.enum(['none', 'fade', 'scale', 'fade-scale'])

export const SuiteCutAnimationOptionsSchema = z.strictObject({
  type: SuiteCutVisualAnimationSchema.exactOptional(),
  durationMs: NonNegativeNumberSchema.exactOptional(),
  easing: SuiteCutEasingSchema.exactOptional(),
})

export const SuiteCutHighlightModeSchema = z.enum(['outline', 'spotlight', 'fill'])
export const SuiteCutHighlightGeometrySchema = z.enum(['element', 'content'])
export const SuiteCutBorderStyleSchema = z.enum(['solid', 'dashed'])

export const SuiteCutHighlightOptionsSchema = z.strictObject({
  durationMs: PositiveNumberSchema.exactOptional(),
  mode: SuiteCutHighlightModeSchema.exactOptional(),
  geometry: SuiteCutHighlightGeometrySchema.exactOptional(),
  paddingPx: NonNegativeNumberSchema.exactOptional(),
  borderWidthPx: NonNegativeNumberSchema.exactOptional(),
  borderStyle: SuiteCutBorderStyleSchema.exactOptional(),
  borderColor: NonEmptyStringSchema.exactOptional(),
  borderRadiusPx: NonNegativeNumberSchema.exactOptional(),
  fillColor: NonEmptyStringSchema.exactOptional(),
  fillOpacity: OpacitySchema.exactOptional(),
  backdropColor: NonEmptyStringSchema.exactOptional(),
  backdropOpacity: OpacitySchema.exactOptional(),
  label: NonEmptyStringSchema.exactOptional(),
  enter: SuiteCutAnimationOptionsSchema.exactOptional(),
  exit: SuiteCutAnimationOptionsSchema.exactOptional(),
})

export const SuiteCutZoomOptionsSchema = z.strictObject({
  scale: z.number().min(1).max(MAX_SUITE_CUT_ZOOM_SCALE).exactOptional(),
  geometry: SuiteCutHighlightGeometrySchema.exactOptional(),
  paddingPx: NonNegativeNumberSchema.exactOptional(),
  holdMs: NonNegativeNumberSchema.exactOptional(),
  enter: SuiteCutAnimationOptionsSchema.exactOptional(),
  exit: SuiteCutAnimationOptionsSchema.exactOptional(),
})

export const SuiteCutPointSchema = z.strictObject({
  x: z.number(),
  y: z.number(),
})

export const SuiteCutSizeSchema = z.strictObject({
  width: NonNegativeNumberSchema,
  height: NonNegativeNumberSchema,
})

export const SuiteCutRectSchema = z.strictObject({
  x: z.number(),
  y: z.number(),
  width: NonNegativeNumberSchema,
  height: NonNegativeNumberSchema,
})

export const SuiteCutViewportSchema = z
  .strictObject({
    width: PositiveNumberSchema,
    height: PositiveNumberSchema,
    deviceScaleFactor: PositiveNumberSchema.exactOptional(),
    scrollX: z.number(),
    scrollY: z.number(),
  })
  .transform((viewport) => ({
    width: viewport.width,
    height: viewport.height,
    scrollX: viewport.scrollX,
    scrollY: viewport.scrollY,
  }))

export const SuiteCutCapturedGeometrySchema = z.strictObject({
  rect: SuiteCutRectSchema,
  viewport: SuiteCutViewportSchema,
})

export const SuiteCutPathKindSchema = z.enum(['absolute', 'manifest-relative'])

export const SuiteCutBrowserNameSchema = z.enum(['chromium', 'firefox', 'webkit'])

export const SuiteCutOutputOptionsSchema = z.strictObject({
  directory: NonEmptyStringSchema.exactOptional(),
  manifestPath: NonEmptyStringSchema.exactOptional(),
  pathKind: SuiteCutPathKindSchema.exactOptional(),
})

export const SuiteCutStepCategorySchema = z.enum([
  'hook',
  'fixture',
  'pw:api',
  'expect',
  'test.step',
  'test.attach',
  'unknown',
])

export const SuiteCutReporterOptionsSchema = z.strictObject({
  outputFile: NonEmptyStringSchema.exactOptional(),
  pathKind: SuiteCutPathKindSchema.exactOptional(),
  includeStepCategories: z.array(SuiteCutStepCategorySchema).exactOptional(),
})

export type SuiteCutNarrationVoice = z.infer<typeof SuiteCutNarrationVoiceSchema>
export type SuiteCutNarrationProvider = z.infer<typeof SuiteCutNarrationProviderSchema>
export type SuiteCutNarrationOptions = z.infer<typeof SuiteCutNarrationOptionsSchema>
export type SuiteCutJsonValue = z.infer<typeof SuiteCutJsonValueSchema>
export type SuiteCutAudioPluginReference = z.infer<typeof SuiteCutAudioPluginReferenceSchema>
export type SuiteCutCheckpointOptions = z.infer<typeof SuiteCutCheckpointOptionsSchema>
export type SuiteCutCaptureViewport = z.infer<typeof SuiteCutCaptureViewportSchema>
export type SuiteCutCaptureSize = z.infer<typeof SuiteCutCaptureSizeSchema>
export type SuiteCutCaptureOptions = z.infer<typeof SuiteCutCaptureOptionsSchema>
export type SuiteCutReconnectOptions = z.infer<typeof SuiteCutReconnectOptionsSchema>
export type SuiteCutStreamOptions = z.infer<typeof SuiteCutStreamOptionsSchema>
export type SuiteCutPointerActionOptions = z.infer<typeof SuiteCutPointerActionOptionsSchema>
export type SuiteCutTypeOptions = z.infer<typeof SuiteCutTypeOptionsSchema>
export type SuiteCutScrollBehavior = z.infer<typeof SuiteCutScrollBehaviorSchema>
export type SuiteCutScrollAlignment = z.infer<typeof SuiteCutScrollAlignmentSchema>
export type SuiteCutScrollOptions = z.infer<typeof SuiteCutScrollOptionsSchema>
export type SuiteCutEasing = z.infer<typeof SuiteCutEasingSchema>
export type SuiteCutVisualAnimation = z.infer<typeof SuiteCutVisualAnimationSchema>
export type SuiteCutAnimationOptions = z.infer<typeof SuiteCutAnimationOptionsSchema>
export type SuiteCutHighlightMode = z.infer<typeof SuiteCutHighlightModeSchema>
export type SuiteCutHighlightGeometry = z.infer<typeof SuiteCutHighlightGeometrySchema>
export type SuiteCutBorderStyle = z.infer<typeof SuiteCutBorderStyleSchema>
export type SuiteCutHighlightOptions = z.infer<typeof SuiteCutHighlightOptionsSchema>
export type SuiteCutZoomOptions = z.infer<typeof SuiteCutZoomOptionsSchema>
export type SuiteCutPoint = z.infer<typeof SuiteCutPointSchema>
export type SuiteCutSize = z.infer<typeof SuiteCutSizeSchema>
export type SuiteCutRect = z.infer<typeof SuiteCutRectSchema>
export type SuiteCutViewport = z.infer<typeof SuiteCutViewportSchema>
export type SuiteCutPathKind = z.infer<typeof SuiteCutPathKindSchema>
export type SuiteCutBrowserName = z.infer<typeof SuiteCutBrowserNameSchema>
export type SuiteCutOutputOptions = z.infer<typeof SuiteCutOutputOptionsSchema>
export type SuiteCutStepCategory = z.infer<typeof SuiteCutStepCategorySchema>
export type SuiteCutReporterOptions = z.infer<typeof SuiteCutReporterOptionsSchema>
