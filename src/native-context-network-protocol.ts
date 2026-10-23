// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { NativeNetworkInfoSchema } from './native-protocol.js'
import { NativeWorkerInfoSchema } from './native-worker-protocol.js'

const NativeContextNetworkBase = {
  contextId: z.number().int().positive(),
  sequence: z.number().int().positive().max(Number.MAX_SAFE_INTEGER),
  network: NativeNetworkInfoSchema.refine(
    (value) => ['request', 'response', 'requestfinished', 'requestfailed'].includes(value.event),
    'Native context network records must contain an observable network event',
  ),
}
export const NativeContextNetworkInfoSchema = z.union([
  z.strictObject({ ...NativeContextNetworkBase, pageId: z.number().int().positive() }),
  z.strictObject({
    ...NativeContextNetworkBase,
    worker: NativeWorkerInfoSchema,
    network: NativeNetworkInfoSchema.refine(
      (value) =>
        ['request', 'response', 'requestfinished', 'requestfailed'].includes(value.event) ||
        (value.event === 'requestupdated' &&
          value.request.postDataBodyId !== undefined &&
          value.request.postDataOwner !== undefined),
      'Native worker metadata updates require a body publication owner',
    ),
  }),
])
export type NativeContextNetworkInfo = z.infer<typeof NativeContextNetworkInfoSchema>
