import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

function headerRecord(
  value: UntrustedInput,
  allowNumber: boolean,
): value is Record<string, string | number> {
  if (!value || typeof value !== 'object' || Array.isArray(value)) return false
  const prototype = z.unknown().parse(Object.getPrototypeOf(value)) as UntrustedInput
  if (prototype !== Object.prototype && prototype !== null) return false
  return Object.values(value).every(
    (entry) =>
      typeof entry === 'string' ||
      (allowNumber && typeof entry === 'number' && Number.isFinite(entry)),
  )
}
/** Clone own HTTP fields without invoking an inherited __proto__ setter. */
export const NativeObservedHeadersSchema = z
  .custom<Record<string, string | number>>(
    (value) => typeof value === 'object' && headerRecord(value, true),
    'Invalid native HTTP headers',
  )
  .transform((value) => Object.fromEntries(Object.entries(value)))
export const NativeStringHeadersSchema = z
  .custom<Record<string, string>>(
    (value) => typeof value === 'object' && headerRecord(value, false),
    'Invalid native HTTP headers',
  )
  .transform((value) => Object.fromEntries(Object.entries(value)))

/** Fold all observed case variants while preserving actual field values. */
export function normalizeNativeObservedHeaders(
  headers: Record<string, string | number>,
): Record<string, string> {
  const folded = new Map<string, string>()
  for (const [name, value] of Object.entries(headers)) {
    const key = name.toLowerCase()
    const separator = key === 'set-cookie' ? '\n' : ', '
    const rendered = key === 'set-cookie' ? String(value) : String(value).replaceAll('\n', ', ')
    const previous = folded.get(key)
    folded.set(key, previous === undefined ? rendered : previous + separator + rendered)
  }
  return Object.fromEntries(folded)
}

/** Keep every observed header occurrence; commas within a value are data. */
export function nativeObservedHeaderValues(
  headers: Record<string, string | number>,
  name: string,
): string[] {
  const key = name.toLowerCase()
  return Object.entries(headers).flatMap(([observed, value]) =>
    observed.toLowerCase() === key ? String(value).split('\n') : [],
  )
}
