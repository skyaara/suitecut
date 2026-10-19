import {
  invokeNativeRouteCallback,
  type NativeRouteCallbackLease,
  type NativeRouteCallbackTransport,
} from './native-route-callback.js'
import { type NativeRouteMatcherEvaluation } from './native-route-url-matcher.js'
import { toError, type UntrustedInput } from './untrusted.js'

/** One genuine native candidate; this bridge never chooses another handler. */
export async function invokeNativeRouteURLMatcher(
  transport: NativeRouteCallbackTransport,
  lease: NativeRouteCallbackLease,
  callback: (url: string) => NativeRouteMatcherEvaluation,
  url: string,
  decide: (matched: boolean | undefined) => Promise<void>,
  continuationFailed: (error: Error) => void,
): Promise<void> {
  const matched = await invokeNativeRouteCallback(transport, lease, async () => {
    const result = callback(url)
    if (!result.error) return result.matched
    let failure: Error = result.error
    try {
      // Undefined is a real invalid-result notification, never a false author
      // result. Native aborts its owned pause without consuming a route use.
      await decide(undefined)
    } catch (error) {
      failure = toError(error as UntrustedInput)
    } finally {
      // An invalid async return can already have started actual author work.
      // Scope abort/removal/command failure cannot retire that physical work.
      if (result.continuation)
        await result.continuation.catch((error: UntrustedInput) =>
          continuationFailed(toError(error)),
        )
    }
    throw failure
  })
  // A genuine boolean is usable only after the native finish ACK above.
  transport.signal.throwIfAborted()
  await decide(matched)
}
