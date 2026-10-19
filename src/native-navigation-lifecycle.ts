// The caller's promise always keeps its actual navigation result. Explicit
// teardown observes cancellation of otherwise ignored navigation promises so
// closing a context cannot create a separate unhandled-rejection failure.
import { type UntrustedInput } from './untrusted.js'

interface NativeNavigationRecord {
  observeClose(): void
}
const pending = new WeakMap<object, Set<NativeNavigationRecord>>()

export function runNativeNavigation<T>(owner: object, action: () => Promise<T>): Promise<T> {
  const active = pending.get(owner) ?? new Set<NativeNavigationRecord>()
  if (active.size >= 64) return Promise.reject(new Error('Too many pending native navigations'))
  pending.set(owner, active)
  let resolve!: (value: T) => void
  let reject!: (reason: UntrustedInput) => void
  const result = new Promise<T>((accept, fail) => {
    resolve = accept
    reject = fail
  })
  const record = {
    observeClose: (): void => {
      void result.catch(() => undefined)
    },
  }
  active.add(record)
  const cleanup = (): void => {
    active.delete(record)
    if (!active.size) pending.delete(owner)
  }
  try {
    void action().then(
      (value) => {
        cleanup()
        resolve(value)
      },
      (error: UntrustedInput) => {
        cleanup()
        reject(error)
      },
    )
  } catch (error) {
    cleanup()
    reject(error instanceof Error ? error : new Error('Native navigation failed', { cause: error }))
  }
  return result
}

export function prepareNativeNavigationClose(owner: object): void {
  for (const navigation of pending.get(owner) ?? []) navigation.observeClose()
}

export function pendingNativeNavigationCount(owner: object): number {
  return pending.get(owner)?.size ?? 0
}
