// SPDX-License-Identifier: MIT
import { type Buffer } from 'node:buffer'

import * as z from 'zod'

export const NativeWebSocketInfoSchema = z.strictObject({
  socketId: z.string().regex(/^[1-9][0-9]{0,19}$/u),
  sessionId: z.string(),
  requestId: z.string().min(1),
  url: z.string(),
  event: z.enum(['created', 'framesent', 'framereceived', 'socketerror', 'close']),
  sequence: z.number().int().positive(),
  opcode: z.union([z.literal(1), z.literal(2)]).optional(),
  error: z.string().optional(),
})
export type NativeWebSocketInfo = z.infer<typeof NativeWebSocketInfoSchema>
export interface NativeWebSocketMessage {
  info: NativeWebSocketInfo
  payload: Buffer
}
