import { toError, type UntrustedInput } from './untrusted.js'

function abortReason(signal: AbortSignal): Error {
  if (signal.reason instanceof Error) return signal.reason
  return new DOMException('The operation was aborted', 'AbortError')
}

/** Rejects with the signal reason while still observing the original operation. */
export function raceWithAbort<T>(operation: Promise<T>, signal?: AbortSignal): Promise<T> {
  if (signal === undefined) return operation
  if (signal.aborted) {
    void operation.catch(() => undefined)
    return Promise.reject(abortReason(signal))
  }

  return new Promise<T>((resolve, reject) => {
    const onAbort = (): void => {
      signal.removeEventListener('abort', onAbort)
      reject(abortReason(signal))
    }
    signal.addEventListener('abort', onAbort, { once: true })
    void operation.then(
      (value) => {
        signal.removeEventListener('abort', onAbort)
        resolve(value)
      },
      (error: UntrustedInput) => {
        signal.removeEventListener('abort', onAbort)
        reject(toError(error))
      },
    )
  })
}
