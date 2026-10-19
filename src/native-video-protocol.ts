// SPDX-License-Identifier: MIT
import * as z from 'zod'

/** Opaque ID admitted by the native recorder before the actual page announcement. */
export const NativeVideoDescriptorSchema = z.strictObject({ id: z.string().min(1).max(128) })
export type NativeVideoDescriptor = z.infer<typeof NativeVideoDescriptorSchema>
