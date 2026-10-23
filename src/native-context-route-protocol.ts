// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { NativeNetworkInfoSchema } from './native-protocol.js'
import { NativeWorkerInfoSchema } from './native-worker-protocol.js'

const Base = {
  contextId: z.number().int().positive(),
  owner: z.string().min(1).max(128),
  network: NativeNetworkInfoSchema.refine(
    (value) =>
      (value.event === 'route' && value.route !== undefined) || value.event === 'routematch',
    'Native context route requires an actual intercepted invocation',
  ),
}
/** Owner is a native channel namespace; no part is parsed to infer ownership. */
export const NativeContextRouteInfoSchema = z.union([
  z.strictObject({ ...Base, pageId: z.number().int().positive() }),
  z.strictObject({ ...Base, worker: NativeWorkerInfoSchema }),
])
export type NativeContextRouteInfo = z.infer<typeof NativeContextRouteInfoSchema>

/** Policy admission is distinct from the actual all-target control ACK. */
export const NativeContextRouteMutationSchema = z.strictObject({
  contextId: z.number().int().positive(),
  mutationId: z.string().min(1).max(128),
  phase: z.enum(['admitted', 'applied']),
  registrationIds: z.array(z.string().min(1).max(128)).max(256),
})
export type NativeContextRouteMutation = z.infer<typeof NativeContextRouteMutationSchema>
