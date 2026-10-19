// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { NativeConsoleInfoSchema } from './native-protocol.js'

export const NativeContextConsoleInfoSchema = z.strictObject({
  contextId: z.number().int().positive(),
  pageId: z.number().int().positive().nullable(),
  sequence: z.number().int().positive(),
  console: NativeConsoleInfoSchema,
})
export type NativeContextConsoleInfo = z.infer<typeof NativeContextConsoleInfoSchema>
