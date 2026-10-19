// SPDX-License-Identifier: MIT
// Issued only by the actual native browser response parser against its pending
// prepared comparison command. Public result shapes do not establish identity.
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

export const NativeScreenshotReceiptSchema = z.strictObject({
  type: z.literal('native-screenshot-assertion'),
  version: z.literal(1),
  operationId: z.number().int().positive(),
  generation: z
    .string()
    .regex(/^[1-9][0-9]{0,19}$/u)
    .refine((value) => BigInt(value) <= 18446744073709551615n),
  inputToken: z.string().min(1).max(128),
  deadlineMs: z.number().finite().nonnegative(),
  outcome: z.enum(['passed', 'observedMismatch']),
  observed: z.literal(true),
})
type Receipt = z.infer<typeof NativeScreenshotReceiptSchema>
const issued = new WeakMap<object, { readonly receipt: Receipt; consumed: boolean }>()

/** Actual transport success, before caller code can receive/mutate the result. */
export function nativeScreenshotResult(
  result: UntrustedInput,
  pending: { readonly id: number; readonly inputToken: string },
): UntrustedInput {
  if (typeof result !== 'object' || result === null || Array.isArray(result))
    throw new Error('Invalid actual native screenshot assertion result')
  const raw = result as Record<string, UntrustedInput>
  const receipt = NativeScreenshotReceiptSchema.parse(raw.assertion)
  if (
    receipt.operationId !== pending.id ||
    receipt.inputToken !== pending.inputToken ||
    typeof raw.passed !== 'boolean' ||
    raw.passed !== (receipt.outcome === 'passed') ||
    typeof raw.actual !== 'string' ||
    !Number.isSafeInteger(raw.differentPixels) ||
    typeof raw.totalPixels !== 'number' ||
    !Number.isSafeInteger(raw.totalPixels) ||
    raw.totalPixels <= 0
  )
    throw new Error('Native screenshot assertion differs from its actual pending comparison')
  Object.freeze(raw.assertion)
  Object.freeze(result)
  issued.set(result, { receipt: Object.freeze(receipt), consumed: false })
  return result
}

/** The typed parser keeps the original source result as its private issuer. */
export function consumeNativeScreenshotMismatch(result: UntrustedInput): boolean {
  if (typeof result !== 'object' || result === null) return false
  const actual = issued.get(result)
  if (!actual || actual.consumed || actual.receipt.outcome !== 'observedMismatch') return false
  actual.consumed = true
  return true
}
