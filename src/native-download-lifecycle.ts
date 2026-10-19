// SPDX-License-Identifier: MIT
// A context close cancels pending file APIs through the native transport. The
// browser's download failure metadata remains independent of that operation.
const scopes = new WeakMap<object, { controller: AbortController; closed: boolean }>()

function scope(context: object): { controller: AbortController; closed: boolean } {
  let value = scopes.get(context)
  if (!value) {
    value = { controller: new AbortController(), closed: false }
    scopes.set(context, value)
  }
  return value
}

export function nativeDownloadContextSignal(context: object): AbortSignal {
  const value = scope(context)
  if (value.closed) throw new Error('Target page, context or browser has been closed')
  return value.controller.signal
}

export function prepareNativeDownloadContextClose(context: object): void {
  scope(context).controller.abort(new Error('canceled'))
}

export function finishNativeDownloadContextClose(context: object): void {
  const value = scope(context)
  value.closed = true
  value.controller.abort(new Error('Target page, context or browser has been closed'))
}
