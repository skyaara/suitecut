// SPDX-License-Identifier: MIT
import * as z from 'zod'

export const NativeWebSocketRouteLeaseSchema = z.strictObject({
  invocation: z.string().min(1),
  token: z.string().regex(/^[1-9][0-9]{0,19}$/u),
  scope: z.string().regex(/^[1-9][0-9]{0,19}$/u),
})
/** Global native publication; context events can precede a Page facade. */
export const NativeWebSocketRouteEnvelopeSchema = z.strictObject({
  scope: z.enum(['page', 'context']),
  contextId: z.number().int().positive(),
  pageId: z.number().int().nonnegative(),
  targetId: z.string().min(1),
  registrationId: z.string().min(1),
  routingToken: z.string().regex(/^[1-9][0-9]{0,19}$/u),
  requestId: z.string().min(1),
  stage: z.enum(['match', 'created', 'message', 'closed']),
  url: z.string(),
  messageId: z.string(),
  outgoing: z.boolean(),
  opcode: z.number().int(),
  callback: NativeWebSocketRouteLeaseSchema,
})
export const NativeWebSocketRouteInfoSchema = NativeWebSocketRouteEnvelopeSchema.extend({
  payload: z.instanceof(Buffer),
}).strip()
export type NativeWebSocketRouteInfo = z.infer<typeof NativeWebSocketRouteInfoSchema>
