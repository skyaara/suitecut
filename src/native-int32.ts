import * as z from 'zod'

/**
 * Largest value a native int32 protocol field can carry. This is the wire
 * encoding width (CEF reads these fields with `GetInt`), not a suitecut limit:
 * a larger number would arrive as a double and read back as 0.
 */
export const NATIVE_INT32_MAX = 2_147_483_647

/** An integer option sent to the native helper in an int32 field. */
export function nativeInt32(name: string): z.ZodNumber {
  return z
    .number()
    .int()
    .max(
      NATIVE_INT32_MAX,
      `'${name}' must be at most ${NATIVE_INT32_MAX}, the width of the native int32 field`,
    )
}
