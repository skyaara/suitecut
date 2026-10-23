// SPDX-License-Identifier: MIT
import * as z from 'zod'

const Counter = z
  .string()
  .regex(/^[1-9][0-9]{0,19}$/u)
  .refine(
    (value) => value.length < 20 || value <= '18446744073709551615',
    'Native route callback counter exceeds uint64',
  )
/** Actual Page-owner policy and callback identities, never a Context or Frame inference. */
export const NativePageRouteMutationSchema = z.strictObject({
  event: z.literal('routemutation'),
  sequence: z.number().int().positive().max(Number.MAX_SAFE_INTEGER),
  mutationId: z.string().min(1).max(128),
  phase: z.enum(['admitted', 'applied']),
  registrationIds: z.array(z.string().min(1).max(128)).max(256),
  callbackScope: Counter,
  callbackTokens: z.array(Counter).max(128),
})
export type NativePageRouteMutation = z.infer<typeof NativePageRouteMutationSchema>
/** Compare only the genuine native pair; no part is parsed as a worker/Page ID. */
export function nativePageRouteCallbackIdentity(scope: string, token: string): string {
  return JSON.stringify([scope, token])
}
