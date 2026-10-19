import { type Buffer } from 'node:buffer'

import * as z from 'zod'

export const NativeWebSocketInfoSchema = z.strictObject({
  socketId: z.string().regex(/^[1-9][0-9]{0,19}$/u),
  sessionId: z.string().max(128),
  requestId: z.string().min(1).max(128),
  url: z.string().max(8192),
  event: z.enum(['created', 'framesent', 'framereceived', 'socketerror', 'close']),
  sequence: z.number().int().positive(),
  opcode: z.union([z.literal(1), z.literal(2)]).optional(),
  error: z.string().max(8192).optional(),
})
export type NativeWebSocketInfo = z.infer<typeof NativeWebSocketInfoSchema>
export interface NativeWebSocketMessage {
  info: NativeWebSocketInfo
  payload: Buffer
}
