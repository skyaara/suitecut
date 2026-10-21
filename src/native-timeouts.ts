import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

interface NativeTimeoutPolicy {
  timeout?: number
  navigationTimeout?: number
}
const policies = new WeakMap<object, NativeTimeoutPolicy>()

/** Preferences are carried on each command; the native engine owns its deadline. */
export function setNativeDefaultTimeout(owner: object, timeout: number, navigation = false): void {
  const value = z.number().int().min(0).max(120_000).parse(timeout)
  const policy = policies.get(owner) ?? {}
  if (navigation) policy.navigationTimeout = value
  else policy.timeout = value
  policies.set(owner, policy)
}

export function getNativeDefaultTimeout(
  source: object,
  context?: object,
  navigation = false,
): number {
  const page = policies.get(source)
  const parent = context ? policies.get(context) : undefined
  return (
    (navigation ? page?.navigationTimeout : undefined) ??
    page?.timeout ??
    (navigation ? parent?.navigationTimeout : undefined) ??
    parent?.timeout ??
    30_000
  )
}

export class NativeTimeoutError extends Error {
  constructor(message: string, options?: ErrorOptions) {
    super(message, options)
    this.name = 'TimeoutError'
  }
}

export function nativeCommandError(message: string): Error {
  const coverageDeadline =
    /^Native coverage (?:operation|queued command|target bootstrap) exceeded its original deadline$/u.test(
      message,
    )
  const debuggerDeadline =
    /^Native Debugger (?:acquisition|startup|pause|breakpoint arm) exceeded its original (?:document )?deadline(?: before arm ACK)?$/u.test(
      message,
    )
  const workerDeadline =
    /^Native context worker original (?:control )?deadline (?:exceeded|expired)$/u.test(message)
  return workerDeadline ||
    coverageDeadline ||
    debuggerDeadline ||
    /\b(?:timed out|Timeout (?:\d+ms|exceeded)|deadline expired)\b/iu.test(message)
    ? new NativeTimeoutError(message)
    : new Error(message)
}

const NativeHTTPTransportErrorSchema = z.strictObject({
  type: z.literal('native-http-transport'),
  curlCode: z.number().int().min(1).max(1023),
  osErrno: z.number().int().min(0).max(2147483647),
  code: z
    .enum([
      'ECONNRESET',
      'ECONNREFUSED',
      'EPIPE',
      'ETIMEDOUT',
      'ENETUNREACH',
      'EHOSTUNREACH',
      'EADDRNOTAVAIL',
      'ECONNABORTED',
    ])
    .optional(),
})

/** Genuine native libcurl failure with its observed OS error, when available. */
export class NativeHTTPTransportError extends Error {
  readonly curlCode: number
  readonly osErrno: number
  readonly code: string | undefined
  constructor(message: string, observation: z.infer<typeof NativeHTTPTransportErrorSchema>) {
    const actual = NativeHTTPTransportErrorSchema.parse(observation)
    super(actual.code && !message.includes(actual.code) ? `${message} (${actual.code})` : message)
    this.name = 'NativeHTTPTransportError'
    this.curlCode = actual.curlCode
    this.osErrno = actual.osErrno
    this.code = actual.code
  }
}

/** Called only for a failed native HTTP command's separately framed payload. */
export function nativeHTTPCommandError(message: string, details: UntrustedInput): Error {
  const actual = NativeHTTPTransportErrorSchema.safeParse(details)
  return actual.success
    ? new NativeHTTPTransportError(message, actual.data)
    : nativeCommandError(message)
}
