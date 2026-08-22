/**
 * A value received before SuiteCut has checked its runtime shape.
 *
 * Keep this type at process, JSON, and worker boundaries. Pass the value through
 * a schema or type guard before using it as a domain value.
 */
export type UntrustedInput = object | string | number | boolean | bigint | symbol | null | undefined

/** Converts a rejected or thrown value into an Error without object stringification. */
export function toError(input: UntrustedInput): Error {
  if (input instanceof Error) return input
  if (typeof input === 'string') return new Error(input)
  if (
    typeof input === 'number' ||
    typeof input === 'boolean' ||
    typeof input === 'bigint' ||
    typeof input === 'symbol'
  ) {
    return new Error(String(input))
  }
  if (input === null) return new Error('Null was thrown')
  if (input === undefined) return new Error('Undefined was thrown')
  return new Error('A non-Error object was thrown')
}
