import { toError, type UntrustedInput } from './untrusted.js'

const observers = new WeakMap<object, (settlement: Promise<void>) => void>()
const controlFailures = new WeakSet<Error>()

/** Associate one resolved handler wrapper with its actual invocation receipt. */
export function observeNativeRouteHandler(
  handler: object,
  observer: (settlement: Promise<void>) => void,
): void {
  observers.set(handler, observer)
}

/** The existing native start/handler/finish promise is the sole settlement source. */
export function nativeRouteHandlerInvoked(
  handler: object | undefined,
  settlement: Promise<void>,
): void {
  if (handler) observers.get(handler)?.(settlement)
}

/** Preserve the actual error object while classifying a genuine command rejection. */
export function nativeRouteCommandFailed(reason: UntrustedInput): Error {
  const error = toError(reason)
  controlFailures.add(error)
  return error
}

/** Exception policy must never suppress native command errors by matching text. */
export function isNativeRouteCommandFailure(reason: UntrustedInput): boolean {
  return reason instanceof Error && controlFailures.has(reason)
}
