// SPDX-License-Identifier: MIT
import * as z from 'zod'

/** Exact native Inspector owner and uint64 attachment generation, never a parsed GUID. */
export const NativeWorkerControlSchema = z.strictObject({
  owner: z.string().min(1).max(128),
  generation: z
    .string()
    .regex(/^[1-9][0-9]{0,19}$/u)
    .refine(
      (value) => value.length < 20 || value <= '18446744073709551615',
      'Native worker generation exceeds uint64',
    ),
})
export type NativeWorkerControl = z.infer<typeof NativeWorkerControlSchema>
export const NativeWorkerInfoSchema = z.strictObject({
  id: z.string().min(1).max(128),
  url: z.string().max(8192),
  type: z.enum(['worker', 'shared_worker', 'service_worker']),
  control: NativeWorkerControlSchema.optional(),
})
export type NativeWorkerInfo = z.infer<typeof NativeWorkerInfoSchema>
export const NativeWorkerEventSchema = z.strictObject({
  event: z.enum(['worker', 'close', 'updated', 'channelclosed']),
  worker: NativeWorkerInfoSchema,
  sequence: z.number().int().positive(),
  reason: z.string().max(1024).optional(),
})
export type NativeWorkerEvent = z.infer<typeof NativeWorkerEventSchema>
