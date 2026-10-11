import * as z from 'zod'

import { formattedJson } from './atomic-file.js'
import { type SuiteCutWordTimingArtifact } from './types.js'
import { type UntrustedInput } from './untrusted.js'

export const SuiteCutWordTimingSchema = z
  .strictObject({
    text: z.string().min(1),
    startOffset: z.number().int().nonnegative(),
    endOffset: z.number().int().positive(),
    startMs: z.number().nonnegative(),
    endMs: z.number().positive(),
  })
  .superRefine((word, context) => {
    if (word.endOffset <= word.startOffset) {
      context.addIssue({ code: 'custom', path: ['endOffset'], message: 'must follow startOffset' })
    }
    if (word.endMs <= word.startMs) {
      context.addIssue({ code: 'custom', path: ['endMs'], message: 'must follow startMs' })
    }
  })

export const SuiteCutWordTimingArtifactSchema = z
  .strictObject({
    schemaVersion: z.literal(1),
    type: z.literal('word-timings'),
    sourceEventId: z.string().trim().min(1),
    text: z.string().min(1),
    durationMs: z.number().positive(),
    words: z.array(SuiteCutWordTimingSchema),
  })
  .superRefine((artifact, context) => {
    for (const [index, word] of artifact.words.entries()) {
      const previous = artifact.words[index - 1]
      if (word.endOffset > artifact.text.length) {
        context.addIssue({
          code: 'custom',
          path: ['words', index, 'endOffset'],
          message: 'exceeds text length',
        })
      }
      if (artifact.text.slice(word.startOffset, word.endOffset) !== word.text) {
        context.addIssue({
          code: 'custom',
          path: ['words', index, 'text'],
          message: 'does not match text offsets',
        })
      }
      if (word.endMs > artifact.durationMs) {
        context.addIssue({
          code: 'custom',
          path: ['words', index, 'endMs'],
          message: 'exceeds audio duration',
        })
      }
      if (previous !== undefined) {
        if (word.startOffset < previous.endOffset) {
          context.addIssue({
            code: 'custom',
            path: ['words', index, 'startOffset'],
            message: 'overlaps the previous word',
          })
        }
        if (word.startMs < previous.endMs) {
          context.addIssue({
            code: 'custom',
            path: ['words', index, 'startMs'],
            message: 'overlaps the previous word timing',
          })
        }
      }
    }
  })

export function decodeWordTimingArtifact(input: UntrustedInput): SuiteCutWordTimingArtifact {
  return SuiteCutWordTimingArtifactSchema.parse(input)
}

export function encodeWordTimingArtifact(artifact: SuiteCutWordTimingArtifact): string {
  return formattedJson(SuiteCutWordTimingArtifactSchema.parse(artifact))
}
