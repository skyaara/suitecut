// SPDX-License-Identifier: MIT
import * as z from 'zod'

const count = z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER)
export const NativeRecordingInfoSchema = z.strictObject({
  id: z.string().min(1).max(128),
  phase: z.enum(['starting', 'recording', 'stopping', 'complete', 'failed']),
  outputPath: z.string().min(1).max(4096),
  failure: z.string().max(16384),
  diagnostics: z.string().max(8192),
  firstTimestampMs: z.number().finite().min(-1),
  durationMs: z.number().finite().nonnegative(),
  encodedDurationMs: z.number().finite().nonnegative(),
  framesPerSecond: z.number().finite().positive().max(120),
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
