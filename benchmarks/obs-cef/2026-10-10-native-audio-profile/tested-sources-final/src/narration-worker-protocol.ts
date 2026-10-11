import * as z from 'zod'

import { SuiteCutWordTimingSchema } from './captions.js'
import {
  SuiteCutAudioPluginReferenceSchema,
  SuiteCutNarrationProviderSchema,
  SuiteCutNarrationVoiceSchema,
  SuiteCutNonEmptyTextSchema,
} from './schemas.js'
import { type UntrustedInput } from './untrusted.js'

const SuiteCutNarrationSynthesisRequestSchema = z.strictObject({
  type: z.literal('synthesize'),
  jobId: z.uuid(),
  text: SuiteCutNonEmptyTextSchema,
  provider: SuiteCutNarrationProviderSchema,
  voice: SuiteCutNarrationVoiceSchema,
  speed: z.number().min(0.5).max(2),
  outputPath: z.string().trim().min(1),
  plugin: SuiteCutAudioPluginReferenceSchema.exactOptional(),
})

export const SuiteCutNarrationWorkerRequestSchema =
  SuiteCutNarrationSynthesisRequestSchema.superRefine((request, context) => {
    const builtIn = request.provider === 'kokoro' || request.provider === 'macos-say'
    if (builtIn && request.plugin !== undefined) {
      context.addIssue({
        code: 'custom',
        path: ['plugin'],
        message: `Built-in provider ${request.provider} cannot use an external plugin`,
      })
    }
    if (!builtIn && request.plugin === undefined) {
      context.addIssue({
        code: 'custom',
        path: ['plugin'],
        message: `External provider ${request.provider} requires a plugin reference`,
      })
    }
    if (request.plugin !== undefined && request.plugin.provider !== request.provider) {
      context.addIssue({
        code: 'custom',
        path: ['plugin', 'provider'],
        message: 'Plugin provider must match the narration provider',
      })
    }
  })

export const SuiteCutNarrationWorkerCloseRequestSchema = z.strictObject({
  type: z.literal('close'),
})

export const SuiteCutNarrationWorkerMessageSchema = z.union([
  SuiteCutNarrationWorkerRequestSchema,
  SuiteCutNarrationWorkerCloseRequestSchema,
])

const SuiteCutSerializedErrorSchema = z.strictObject({
  name: z.string().trim().min(1),
  message: z.string(),
  stack: z.string().exactOptional(),
})

const SuiteCutNarrationWorkerSuccessSchema = z.strictObject({
  type: z.literal('synthesized'),
  jobId: z.uuid(),
  timing: z
    .strictObject({
      text: z.string().min(1),
      durationMs: z.number().positive(),
      words: z.array(SuiteCutWordTimingSchema),
    })
    .exactOptional(),
})

const SuiteCutNarrationWorkerFailureSchema = z.strictObject({
  type: z.literal('failed'),
  jobId: z.uuid(),
  error: SuiteCutSerializedErrorSchema,
})

const SuiteCutNarrationWorkerClosedSchema = z.strictObject({
  type: z.literal('closed'),
})

const SuiteCutNarrationWorkerCloseFailureSchema = z.strictObject({
  type: z.literal('close-failed'),
  error: SuiteCutSerializedErrorSchema,
})

export const SuiteCutNarrationWorkerResponseSchema = z.discriminatedUnion('type', [
  SuiteCutNarrationWorkerSuccessSchema,
  SuiteCutNarrationWorkerFailureSchema,
  SuiteCutNarrationWorkerClosedSchema,
  SuiteCutNarrationWorkerCloseFailureSchema,
])

export type SuiteCutNarrationWorkerRequest = z.infer<typeof SuiteCutNarrationWorkerRequestSchema>
export type SuiteCutNarrationWorkerMessage = z.infer<typeof SuiteCutNarrationWorkerMessageSchema>
export type SuiteCutNarrationWorkerSuccess = z.infer<typeof SuiteCutNarrationWorkerSuccessSchema>
export type SuiteCutNarrationWorkerFailure = z.infer<typeof SuiteCutNarrationWorkerFailureSchema>
export type SuiteCutNarrationWorkerResponse = z.infer<typeof SuiteCutNarrationWorkerResponseSchema>

/** Decodes a message before the narration worker executes it. */
export function parseNarrationWorkerRequest(input: UntrustedInput): SuiteCutNarrationWorkerRequest {
  return SuiteCutNarrationWorkerRequestSchema.parse(input)
}

/** Decodes synthesis and lifecycle messages received by the narration worker. */
export function parseNarrationWorkerMessage(input: UntrustedInput): SuiteCutNarrationWorkerMessage {
  return SuiteCutNarrationWorkerMessageSchema.parse(input)
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
