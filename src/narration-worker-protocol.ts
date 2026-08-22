import * as z from 'zod'

import {
  SuiteCutNarrationProviderSchema,
  SuiteCutNarrationVoiceSchema,
  SuiteCutNonEmptyTextSchema,
} from './schemas.js'
import { type UntrustedInput } from './untrusted.js'

export const SuiteCutNarrationWorkerRequestSchema = z.strictObject({
  type: z.literal('synthesize'),
  jobId: z.uuid(),
  text: SuiteCutNonEmptyTextSchema,
  provider: SuiteCutNarrationProviderSchema,
  voice: SuiteCutNarrationVoiceSchema,
  speed: z.number().min(0.5).max(2),
  outputPath: z.string().trim().min(1),
})

const SuiteCutSerializedErrorSchema = z.strictObject({
  name: z.string().trim().min(1),
  message: z.string(),
  stack: z.string().exactOptional(),
})

const SuiteCutNarrationWorkerSuccessSchema = z.strictObject({
  type: z.literal('synthesized'),
  jobId: z.uuid(),
})

const SuiteCutNarrationWorkerFailureSchema = z.strictObject({
  type: z.literal('failed'),
  jobId: z.uuid(),
  error: SuiteCutSerializedErrorSchema,
})

export const SuiteCutNarrationWorkerResponseSchema = z.discriminatedUnion('type', [
  SuiteCutNarrationWorkerSuccessSchema,
  SuiteCutNarrationWorkerFailureSchema,
])

export type SuiteCutNarrationWorkerRequest = z.infer<typeof SuiteCutNarrationWorkerRequestSchema>
export type SuiteCutNarrationWorkerSuccess = z.infer<typeof SuiteCutNarrationWorkerSuccessSchema>
export type SuiteCutNarrationWorkerFailure = z.infer<typeof SuiteCutNarrationWorkerFailureSchema>
export type SuiteCutNarrationWorkerResponse = z.infer<typeof SuiteCutNarrationWorkerResponseSchema>

/** Decodes a message before the narration worker executes it. */
export function parseNarrationWorkerRequest(input: UntrustedInput): SuiteCutNarrationWorkerRequest {
  return SuiteCutNarrationWorkerRequestSchema.parse(input)
}

/** Decodes a response before the main thread resolves a narration job. */
export function parseNarrationWorkerResponse(
  input: UntrustedInput,
): SuiteCutNarrationWorkerResponse {
  return SuiteCutNarrationWorkerResponseSchema.parse(input)
}

/** Returns whether a raw worker message is a valid synthesis request. */
export function isNarrationWorkerRequest(
  input: UntrustedInput,
): input is SuiteCutNarrationWorkerRequest {
  return SuiteCutNarrationWorkerRequestSchema.safeParse(input).success
}

/** Returns whether a raw worker message is a valid synthesis response. */
export function isNarrationWorkerResponse(
  input: UntrustedInput,
): input is SuiteCutNarrationWorkerResponse {
  return SuiteCutNarrationWorkerResponseSchema.safeParse(input).success
}
