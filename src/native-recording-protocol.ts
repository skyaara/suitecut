// SPDX-License-Identifier: MIT
import * as z from 'zod'

const count = z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER)
export const NativeRecordingInfoSchema = z.strictObject({
  id: z.string().min(1),
  phase: z.enum(['starting', 'recording', 'stopping', 'complete', 'failed']),
  outputPath: z.string().min(1),
  failure: z.string(),
  diagnostics: z.string(),
  firstTimestampMs: z.number().finite().min(-1),
  durationMs: z.number().finite().nonnegative(),
  encodedDurationMs: z.number().finite().nonnegative(),
  framesPerSecond: z.number().finite().positive(),
  encodingBackend: z.string(),
  encoder: z.string(),
  container: z.string(),
  preparationFrames: count,
  preparationPackets: count,
  preparationMs: z.number().finite().nonnegative(),
  coalescedFrames: count,
  gpuNv12Frames: count,
  gpuInputFrames: count,
  captureTimestampsExact: z.boolean(),
  captureLookaheadMs: z.number().finite().nonnegative(),
  maxTimestampCorrectionMs: z.number().finite().nonnegative(),
  inputFrames: count,
  outputFrames: count,
  heldFrames: count,
  supersededFrames: count,
  audioInputFrames: count,
  audioOutputFrames: count,
  audioOverlapFrames: count,
  audioGapFrames: count,
  outputBytes: count,
  timestampMs: z.number().finite().nonnegative().optional(),
})
export type NativeRecordingInfo = z.infer<typeof NativeRecordingInfoSchema>
