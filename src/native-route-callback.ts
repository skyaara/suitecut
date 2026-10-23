// SPDX-License-Identifier: MIT
import * as z from 'zod'

const Lease = z.strictObject({
  invocation: z.string().min(1).max(128),
  token: z.string().regex(/^[1-9][0-9]{0,19}$/u),
  scope: z.string().regex(/^[1-9][0-9]{0,19}$/u),
})
export type NativeRouteCallbackLease = z.infer<typeof Lease>
export interface NativeRouteCallbackTransport {
  signal: AbortSignal
  processSignal: AbortSignal
  start(lease: NativeRouteCallbackLease): Promise<void>
  finish(lease: NativeRouteCallbackLease): Promise<void>
}
let pending = 0
const counts = new WeakMap<AbortSignal, { pending: number }>()
const capacity = 128
/** Counts actual pending host work, including its native acknowledgment. */
export function nativeRouteCallbackPhysicalCount(processSignal: AbortSignal): number {
  return counts.get(processSignal)?.pending ?? 0
}
/** A scope close cannot settle an authored asynchronous handler. */
export async function invokeNativeRouteCallback<T>(
  transport: NativeRouteCallbackTransport,
  lease: NativeRouteCallbackLease,
  callback: () => T | Promise<T>,
): Promise<T> {
  const parsed = Lease.parse(lease)
  if (pending >= capacity) {
    if (!transport.processSignal.aborted) await transport.finish(parsed)
    throw new Error('Native route physical host callback capacity exceeded')
  }
  let count = counts.get(transport.processSignal)
  if (!count) {
    count = { pending: 0 }
    counts.set(transport.processSignal, count)
  }
  const physical = count
  ++pending
  ++physical.pending
  try {
    // The actual native start acknowledgment precedes invoking author code.
    await transport.start(parsed)
    transport.signal.throwIfAborted()
    return await callback()
  } finally {
    try {
      if (!transport.processSignal.aborted) await transport.finish(parsed)
    } finally {
      --pending
      --physical.pending
    }
  }
}
