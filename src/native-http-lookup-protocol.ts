// SPDX-License-Identifier: MIT
import * as z from 'zod'

export const NativeHTTPLookupEventSchema = z.strictObject({
  event: z.enum(['request', 'canceled']),
  owner: z.number().int().positive(),
  apiContextId: z.number().int().positive(),
  generation: z.number().int().min(1).max(Number.MAX_SAFE_INTEGER),
  lookupKey: z.string().min(1).max(128),
  hostname: z.string().min(1).max(1024),
  port: z.number().int().min(1).max(65535),
})
export type NativeHTTPLookupEvent = z.infer<typeof NativeHTTPLookupEventSchema>
