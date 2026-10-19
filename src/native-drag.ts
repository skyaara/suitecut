// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

export interface NativeDragOptions {
  timeout?: number
  steps?: number
  force?: boolean
  trial?: boolean
  sourcePosition?: { x: number; y: number }
  targetPosition?: { x: number; y: number }
}
const position = z.strictObject({ x: z.number().finite(), y: z.number().finite() })
const optionsSchema = z.strictObject({
  timeout: z.number().int().min(0).max(120_000).optional(),
  steps: z.number().int().min(1).max(10_000).optional(),
  force: z.boolean().optional(),
  trial: z.boolean().optional(),
  sourcePosition: position.optional(),
  targetPosition: position.optional(),
})
export async function performNativeDrag(
  dispatch: (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>,
  target: Record<string, UntrustedInput>,
  options: NativeDragOptions,
): Promise<void> {
  const config = optionsSchema.parse(options)
  await dispatch({
    action: 'dragTo',
    steps: config.steps ?? 1,
    target,
    ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
    force: config.force ?? false,
    trial: config.trial ?? false,
    ...(config.sourcePosition ? { sourcePosition: config.sourcePosition } : {}),
    ...(config.targetPosition ? { targetPosition: config.targetPosition } : {}),
  })
}
