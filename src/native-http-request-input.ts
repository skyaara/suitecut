// SPDX-License-Identifier: MIT
/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase lifecycle module edges. */
import type { NativeRequest } from './native-network.js'
import type { UntrustedInput } from './untrusted.js'

// Break the HTTP/Network runtime module cycle while preserving the genuine
// existing instanceof check, including subclasses. No per-request registry.
let actualRequestClass: typeof NativeRequest | undefined
export function registerNativeHTTPRequestClass(value: typeof NativeRequest): void {
  if (actualRequestClass && actualRequestClass !== value)
    throw new Error('Native HTTP request class identity changed')
  actualRequestClass = value
}
export function isNativeHTTPRequest(value: UntrustedInput): value is NativeRequest {
  return actualRequestClass !== undefined && value instanceof actualRequestClass
}
