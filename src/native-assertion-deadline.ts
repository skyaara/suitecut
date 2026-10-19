// SPDX-License-Identifier: MIT
// Receipt issuance is confined to a genuine failed native command parse. Error
// text and public Error constructors never establish assertion-clock ownership.
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

const Kind = z.enum([
  'toHaveScreenshot',
  'toHaveText',
  'toContainText',
  'toHaveValue',
  'toHaveCount',
  'toHaveId',
  'toHaveAttribute',
  'toHaveClass',
  'toContainClass',
  'toHaveCSS',
  'toBeVisible',
  'toBeHidden',
  'toBeAttached',
  'toBeEnabled',
  'toBeDisabled',
  'toBeEditable',
  'toBeFocused',
  'toBeEmpty',
  'toBeInViewport',
  'toHaveTitle',
  'toHaveURL',
])
export type NativeAssertionDeadlineKind = z.infer<typeof Kind>
const Receipt = z.strictObject({
  type: z.literal('native-assertion-deadline'),
  version: z.literal(1),
  operationId: z.number().int().positive(),
  generation: z
    .string()
    .regex(/^[1-9][0-9]{0,19}$/u)
    .refine((value) => BigInt(value) <= 18446744073709551615n),
  matcher: Kind,
  deadlineMs: z.number().finite().positive(),
  observed: z.literal(false),
})
type ActualReceipt = z.infer<typeof Receipt>
const issued = new WeakMap<Error, { readonly receipt: ActualReceipt; consumed: boolean }>()
export function nativeAssertionDeadlineKind(
  matcher: string,
  expected: UntrustedInput,
): NativeAssertionDeadlineKind | undefined {
  if (Array.isArray(expected)) return undefined
  const actual = Kind.safeParse(matcher)
  return actual.success ? actual.data : undefined
}
/** Called at the actual transport response, against its existing pending ID. */
export function nativeAssertionDeadlineError(
  message: string,
  details: UntrustedInput,
  pending: { readonly id: number; readonly matcher: NativeAssertionDeadlineKind },
): Error {
  const receipt = Receipt.parse(details)
  if (receipt.operationId !== pending.id || receipt.matcher !== pending.matcher)
    throw new Error('Native assertion deadline differs from its actual pending command')
  const error = new Error(message)
  issued.set(error, { receipt: Object.freeze(receipt), consumed: false })
  return error
}
/** Diagnostic identity check only; it does not consume a source receipt. */
export function hasNativeAssertionDeadline(error: UntrustedInput, matcher: string): boolean {
  return error instanceof Error && issued.get(error)?.receipt.matcher === matcher
}
/** The genuine matcher producer consumes one original native receipt once. */
export function consumeNativeAssertionDeadline(error: Error): ActualReceipt | undefined {
  const actual = issued.get(error)
  if (!actual || actual.consumed) return undefined
  actual.consumed = true
  return actual.receipt
}
