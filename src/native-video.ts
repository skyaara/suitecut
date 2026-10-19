// SPDX-License-Identifier: MIT
import { resolve } from 'node:path'

import * as z from 'zod'

import {
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import { type NativeVideoDescriptor } from './native-video-protocol.js'
import { type UntrustedInput } from './untrusted.js'

// Unicode-mode matching rejects isolated surrogate units while preserving pairs.
const pathSchema = z
  .string()
  .min(1)
  .max(4096)
  .refine(
    (value) => !value.includes('\0') && !/[\uD800-\uDFFF]/u.test(value),
    'Native video paths require scalar Unicode without NUL',
  )
const width = z.number().int().min(1).max(3840)
const height = z.number().int().min(1).max(2160)
/** Native VP9/WebM accepts integer canvases, including odd dimensions. Native selects the default fit. */
export const NativeRecordVideoSchema = z.strictObject({
  dir: pathSchema,
  size: z.strictObject({ width, height }).optional(),
})
export type NativeRecordVideoOptions = z.input<typeof NativeRecordVideoSchema>
interface Transport {
  readonly signal: AbortSignal
  runContext(
    action: string,
    params?: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
}
export function nativeVideoAbsolutePath(path: string): string {
  return withoutNativeRetryOperation(() => pathSchema.parse(resolve(pathSchema.parse(path))))
}
/** A native-owned WebM recording. File work and finalization remain in the browser process. */
export class NativeVideo {
  private readonly id: string
  constructor(
    private readonly transport: Transport,
    private readonly contextId: number,
    descriptor: NativeVideoDescriptor,
  ) {
    this.id = descriptor.id
  }
  private dispatch(
    action: 'path' | 'saveAs' | 'delete',
    fields: Record<string, UntrustedInput> = {},
  ): Promise<UntrustedInput> {
    this.transport.signal.throwIfAborted()
    // Wait for the actual native close/finalization without an SDK timeout.
    // Native request ownership and bounded publication IO remain authoritative.
    // This is the surviving process context channel, not the closed page channel.
    return this.transport.runContext('video', {
      action,
      contextId: this.contextId,
      videoId: this.id,
      timeoutMs: 0,
      ...fields,
    })
  }
  path(): Promise<string> {
    return runNativeRetryOperation(async () => {
      const result = await this.dispatch('path')
      return z.strictObject({ path: pathSchema }).parse(result).path
    })
  }
  saveAs(path: string): Promise<void> {
    return runNativeRetryOperation(async () => {
      const destination = nativeVideoAbsolutePath(path)
      await this.dispatch('saveAs', { path: destination })
    })
  }
  delete(): Promise<void> {
    return runNativeRetryOperation(async () => {
      await this.dispatch('delete')
    })
  }
}
