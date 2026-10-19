// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

const resetOptions = z.strictObject({ resetOnNavigation: z.boolean().optional() })
const jsOptions = z.strictObject({
  resetOnNavigation: z.boolean().optional(),
  reportAnonymousScripts: z.boolean().optional(),
  useBlockCoverage: z.boolean().optional(),
  includeWorkers: z.boolean().optional(),
})
const offset = z.number().int().min(0).max(2_147_483_647)
const jsRange = z.strictObject({
  startOffset: offset,
  endOffset: offset,
  count: z.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER),
})
const retainedIdentity = {
  agentId: z.string().min(1).max(256).optional(),
  captureId: z.string().min(1).max(256).optional(),
  sessionId: z.string().max(128).optional(),
  targetId: z.string().min(1).max(256).optional(),
}
const jsEntry = z.strictObject({
  ...retainedIdentity,
  scriptId: z.string().min(1).max(256),
  executionContextId: z.number().int().positive().max(2_147_483_647).optional(),
  url: z.string(),
  source: z.string(),
  functions: z
    .array(
      z.strictObject({
        functionName: z.string(),
        isBlockCoverage: z.boolean(),
        ranges: z.array(jsRange).max(16_384),
      }),
    )
    .max(16_384),
})
const cssEntry = z.strictObject({
  ...retainedIdentity,
  styleSheetId: z.string().min(1).max(256).optional(),
  url: z.string(),
  text: z.string(),
  ranges: z.array(z.strictObject({ start: offset, end: offset })).max(16_384),
})
export type NativeJSCoverageOptions = z.input<typeof jsOptions>
export type NativeCSSCoverageOptions = z.input<typeof resetOptions>
export type NativeJSCoverageEntry = z.output<typeof jsEntry>
export type NativeCSSCoverageEntry = z.output<typeof cssEntry>
export interface NativeCoverageTransport {
  readonly signal: AbortSignal
  defaultTimeout(): number
  runCoverage(params: Record<string, UntrustedInput>): Promise<UntrustedInput>
}
/** Native current-document author measurements across actual document targets and optional dedicated workers; navigation retention requires the acknowledged native backend final-lifecycle contract. */
export class NativeCoverage {
  constructor(private readonly transport: NativeCoverageTransport) {}
  private run(
    action: string,
    options: Record<string, UntrustedInput> = {},
  ): Promise<UntrustedInput> {
    this.transport.signal.throwIfAborted()
    return this.transport.runCoverage({
      action,
      ...options,
      timeoutMs: this.transport.defaultTimeout(),
    })
  }
  async startJSCoverage(options: NativeJSCoverageOptions = {}): Promise<void> {
    await this.run('startJS', jsOptions.parse(options))
  }
  async stopJSCoverage(): Promise<NativeJSCoverageEntry[]> {
    return z
      .array(jsEntry)
      .max(2048)
      .parse(await this.run('stopJS'))
  }
  async startCSSCoverage(options: NativeCSSCoverageOptions = {}): Promise<void> {
    await this.run('startCSS', resetOptions.parse(options))
  }
  async stopCSSCoverage(): Promise<NativeCSSCoverageEntry[]> {
    return z
      .array(cssEntry)
      .max(2048)
      .parse(await this.run('stopCSS'))
  }
}
