// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { nativeTraceStack } from './native-trace-metadata.js'
import { type UntrustedInput } from './untrusted.js'

const identity = z.strictObject({
  session: z.number().int().positive().max(Number.MAX_SAFE_INTEGER),
  chunk: z.number().int().positive().max(Number.MAX_SAFE_INTEGER),
})
const text = z.string().max(512)
const startOptions = z.strictObject({
  name: text.optional(),
  title: text.optional(),
  screenshots: z.boolean().optional(),
  snapshots: z
    .union([
      z.boolean(),
      z.strictObject({
        dom: z.boolean().optional(),
        screen: z.boolean().optional(),
        aria: z.boolean().optional(),
      }),
    ])
    .optional(),
  sources: z.boolean().optional(),
})
const chunkOptions = z.strictObject({ name: text.optional(), title: text.optional() })
const saveOptions = z.strictObject({
  path: z
    .string()
    .min(1)
    .max(4096)
    .refine((value) => !value.includes('\0'))
    .optional(),
})
const groupOptions = z.strictObject({
  location: z
    .strictObject({
      file: z.string().min(1).max(4096),
      line: z.number().int().positive().optional(),
      column: z.number().int().positive().optional(),
    })
    .optional(),
})
export interface NativeTraceTransport {
  signal: AbortSignal
  defaultTimeout(): number
  runTrace(params: Record<string, UntrustedInput>): Promise<UntrustedInput>
}
export interface NativeTraceDisposable {
  dispose(): Promise<void>
  [Symbol.asyncDispose](): Promise<void>
}
/** Native code owns sessions, chunks, capture work, deadlines and artifact writes. */
export class NativeTracing {
  constructor(private readonly transport: NativeTraceTransport) {}
  private run(
    action: string,
    params: Record<string, UntrustedInput> = {},
  ): Promise<UntrustedInput> {
    this.transport.signal.throwIfAborted()
    return this.transport.runTrace({
      action,
      timeoutMs: this.transport.defaultTimeout(),
      ...params,
    })
  }
  private disposable(value: UntrustedInput, chunk: boolean): NativeTraceDisposable {
    const scope = identity.parse(value)
    const dispose = async () => {
      await this.run(chunk ? 'discardChunk' : 'discard', scope)
    }
    return { dispose, [Symbol.asyncDispose]: dispose }
  }
  async start(options: z.input<typeof startOptions> = {}): Promise<NativeTraceDisposable> {
    return this.disposable(await this.run('start', startOptions.parse(options)), false)
  }
  async startChunk(options: z.input<typeof chunkOptions> = {}): Promise<NativeTraceDisposable> {
    return this.disposable(await this.run('startChunk', chunkOptions.parse(options)), true)
  }
  async stop(options: z.input<typeof saveOptions> = {}): Promise<void> {
    await this.run('stop', saveOptions.parse(options))
  }
  async stopChunk(options: z.input<typeof saveOptions> = {}): Promise<void> {
    await this.run('stopChunk', saveOptions.parse(options))
  }
  async group(name: string, options: z.input<typeof groupOptions> = {}): Promise<void> {
    await this.run('group', {
      name: text.parse(name),
      traceStack: nativeTraceStack(),
      ...groupOptions.parse(options),
    })
  }
  async groupEnd(): Promise<void> {
    await this.run('groupEnd')
  }
}
