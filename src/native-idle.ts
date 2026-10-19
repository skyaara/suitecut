import * as z from 'zod'

import { withoutNativeRetryOperation } from './native-callback-operations.js'
import { type UntrustedInput } from './untrusted.js'

const IdleState = z.strictObject({
  isUserActive: z.boolean(),
  isScreenUnlocked: z.boolean(),
})
export type NativeIdleState = z.input<typeof IdleState>

/** Undefined restores Chromium's actual idle state; no browser values are shimmed. */
export async function emulateNativeIdleState(
  run: (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>,
  state?: NativeIdleState,
): Promise<void> {
  const value =
    state === undefined ? null : withoutNativeRetryOperation(() => IdleState.parse(state))
  await run({ action: 'emulateIdleState', state: value })
}
