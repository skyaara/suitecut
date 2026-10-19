// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

/** A genuine native Overlay node-tool lease; it never inserts author DOM. */
export interface NativeHighlightDisposable {
  dispose(): Promise<void>
  [Symbol.asyncDispose](): Promise<void>
}
export interface NativeHighlightOptions {
  readonly style?: string | Readonly<Record<string, string | number>>
}
export type NativeHighlightRun = (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>
const options = z.strictObject({ style: z.unknown().optional() })
const result = z.strictObject({
  token: z.string().regex(/^h[A-Za-z0-9]{1,64}_[1-9]\d{0,19}$/u),
  empty: z.literal(true).optional(),
})

/** Chromium's node-tool exposes protocol colors, rather than arbitrary CSS. */
export async function showNativeHighlight(
  run: NativeHighlightRun,
  cleanup: (token: string, emergency?: boolean) => Promise<void>,
  input: NativeHighlightOptions = {},
): Promise<NativeHighlightDisposable> {
  const config = options.parse(input)
  if (config.style !== undefined)
    throw new Error('Native Chromium Overlay highlighting does not support arbitrary CSS style')
  const actual = result.parse(await run({ action: 'show' }))
  // Native unclaimed delivery has a bounded TTL and never extends the original
  // operation deadline. Claim grants persistent lifetime only to this Source's
  // actual token; failed delivery cleans independently of a caller projection.
  if (!actual.empty) {
    try {
      await run({ action: 'claim', token: actual.token })
    } catch (error) {
      try {
        await cleanup(actual.token, true)
      } catch (failure) {
        throw new AggregateError([error, failure], 'Native highlight delivery and cleanup failed', {
          cause: failure,
        })
      }
      throw error
    }
  }
  let pending: Promise<void> | undefined
  const dispose = (): Promise<void> => {
    pending ??= cleanup(actual.token).catch((error: UntrustedInput) => {
      // Logical failure never retires a physical release. A later invocation
      // may wait for the same genuine token's remaining bounded cleanup.
      pending = undefined
      throw error
    })
    return pending
  }
  return { dispose, [Symbol.asyncDispose]: dispose }
}
export async function hideNativeHighlight(run: NativeHighlightRun): Promise<void> {
  await run({ action: 'hide' })
}
