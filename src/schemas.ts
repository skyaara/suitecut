import * as z from 'zod'

const NonEmptyStringSchema = z
  .string()
  .refine((value) => value.trim().length > 0, { message: 'must not be empty' })
const NonNegativeNumberSchema = z.number().nonnegative()
const PositiveNumberSchema = z.number().positive()
const OpacitySchema = z.number().min(0).max(1)

export const SuiteCutNonEmptyTextSchema = NonEmptyStringSchema
export const SuiteCutPositiveDurationSchema = PositiveNumberSchema
export const SuiteCutNarrationVoiceSchema = NonEmptyStringSchema
export const SuiteCutNarrationProviderSchema = z.enum(['kokoro', 'macos-say'])

export const SuiteCutNarrationOptionsSchema = z.strictObject({
  provider: SuiteCutNarrationProviderSchema.exactOptional(),
  voice: SuiteCutNarrationVoiceSchema.exactOptional(),
  speed: z.number().min(0.5).max(2).exactOptional(),
  caption: z.string().exactOptional(),
})

export const SuiteCutCheckpointOptionsSchema = z.strictObject({
  fullPage: z.boolean().exactOptional(),
})

export const SuiteCutCaptureOptionsSchema = z.strictObject({
  size: z
    .strictObject({
      width: z.number().int().positive().max(7680),
      height: z.number().int().positive().max(4320),
    })
    .exactOptional(),
  framesPerSecond: z.union([z.literal(30), z.literal(60)]).exactOptional(),
  quality: z.number().int().min(1).max(100).exactOptional(),
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
export const SuiteCutBorderStyleSchema = z.enum(['solid', 'dashed'])

export const SuiteCutHighlightOptionsSchema = z.strictObject({
  durationMs: PositiveNumberSchema.exactOptional(),
  mode: SuiteCutHighlightModeSchema.exactOptional(),
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
  scale: z.number().min(1).exactOptional(),
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

export const SuiteCutViewportSchema = z.strictObject({
  width: PositiveNumberSchema,
  height: PositiveNumberSchema,
  deviceScaleFactor: PositiveNumberSchema,
  scrollX: z.number(),
  scrollY: z.number(),
})

export const SuiteCutCapturedGeometrySchema = z.strictObject({
  rect: SuiteCutRectSchema,
  viewport: SuiteCutViewportSchema,
})

export const SuiteCutPathKindSchema = z.enum(['absolute', 'manifest-relative'])

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
export type SuiteCutCheckpointOptions = z.infer<typeof SuiteCutCheckpointOptionsSchema>
export type SuiteCutCaptureOptions = z.infer<typeof SuiteCutCaptureOptionsSchema>
export type SuiteCutEasing = z.infer<typeof SuiteCutEasingSchema>
export type SuiteCutVisualAnimation = z.infer<typeof SuiteCutVisualAnimationSchema>
export type SuiteCutAnimationOptions = z.infer<typeof SuiteCutAnimationOptionsSchema>
export type SuiteCutHighlightMode = z.infer<typeof SuiteCutHighlightModeSchema>
export type SuiteCutBorderStyle = z.infer<typeof SuiteCutBorderStyleSchema>
export type SuiteCutHighlightOptions = z.infer<typeof SuiteCutHighlightOptionsSchema>
export type SuiteCutZoomOptions = z.infer<typeof SuiteCutZoomOptionsSchema>
export type SuiteCutPoint = z.infer<typeof SuiteCutPointSchema>
export type SuiteCutSize = z.infer<typeof SuiteCutSizeSchema>
export type SuiteCutRect = z.infer<typeof SuiteCutRectSchema>
export type SuiteCutViewport = z.infer<typeof SuiteCutViewportSchema>
export type SuiteCutPathKind = z.infer<typeof SuiteCutPathKindSchema>
export type SuiteCutStepCategory = z.infer<typeof SuiteCutStepCategorySchema>
export type SuiteCutReporterOptions = z.infer<typeof SuiteCutReporterOptionsSchema>
