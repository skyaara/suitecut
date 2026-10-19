// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

export interface NativeVirtualTimeSource {
  runAutomation(
    command: 'clock',
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
}

const InfoSchema = z.object({
  installed: z.boolean(),
  lastAcknowledgedPaused: z.boolean(),
  pendingPhysicalCommand: z.boolean(),
  activeOwner: z.boolean(),
  closing: z.boolean(),
  queued: z.number().int().min(0).max(64),
  /** Ownership scope differs from Chromium's process-wide native wall clock. */
  scope: z
    .object({
      controller: z.literal('currentDocument'),
      nativeWallClock: z.literal('rendererProcess'),
      isolatedContext: z.literal(false),
    })
    .optional(),
  capabilities: z.object({
    nativeDate: z.boolean(),
    nativeTemporalNow: z.boolean(),
    nativePerformance: z.boolean(),
    nativeTimerBudgets: z.boolean(),
    nativeRunForExceptionReceipts: z.boolean(),
    sameProcessFrames: z.boolean(),
    performanceTimeOriginRebase: z.boolean(),
    workerPropagation: z.boolean(),
    animationFrameControl: z.boolean(),
    idleCallbackControl: z.boolean(),
    contextWide: z.boolean(),
    outOfProcessFrames: z.boolean(),
    navigation: z.boolean(),
    realtimeResume: z.boolean(),
    fixedDateWithRunningTimers: z.boolean(),
    systemTimeRebase: z.boolean(),
    fastForwardAtMostOnce: z.boolean(),
  }),
})
export type NativeVirtualTimeInfo = z.infer<typeof InfoSchema>
export interface NativeVirtualTimeOptions {
  timeout?: number
  signal?: AbortSignal
}
const OptionsSchema = z.strictObject({
  timeout: z.number().int().min(0).max(2147483647).optional(),
  signal: z.instanceof(AbortSignal).optional(),
})

/**
 * Chromium's native current-target virtual-time budgets. This is a narrower
 * facility than a context-wide browser clock: read capabilities before use.
 * It preserves the actual Date, Temporal, timers and performance implementations.
 * The controller pins its original default document identity. Chromium's native
 * wall clock is renderer-process-wide; this is not context-isolated time.
 * Animation/idle callbacks remain uncontrolled; rebasing, fixed Date, realtime
 * resume, context/OOPIF propagation and navigation require another native backend.
 */
export class NativeVirtualTime {
  constructor(private readonly source: NativeVirtualTimeSource) {}
  private async command(
    action: string,
    params: Record<string, UntrustedInput>,
    options: NativeVirtualTimeOptions,
  ): Promise<NativeVirtualTimeInfo> {
    const config = OptionsSchema.parse(options)
    return InfoSchema.parse(
      await this.source.runAutomation(
        'clock',
        {
          ...params,
          action,
          ...(config.timeout === undefined ? {} : { timeoutMs: config.timeout }),
        },
        config.signal,
      ),
    )
  }
  status(options: NativeVirtualTimeOptions = {}): Promise<NativeVirtualTimeInfo> {
    return this.command('status', {}, options)
  }
  installPaused(
    options: NativeVirtualTimeOptions & { time?: number | string | Date } = {},
  ): Promise<NativeVirtualTimeInfo> {
    const { time, ...rest } = options
    if (time === undefined) return this.command('installPaused', {}, rest)
    const epoch =
      typeof time === 'number' ? time : time instanceof Date ? time.getTime() : Date.parse(time)
    z.number().finite().min(-8640000000000000).max(8640000000000000).parse(epoch)
    return this.command('installPaused', { timeMs: epoch }, rest)
  }
  /** Pause in FIFO order; an already acknowledged pause needs no renderer IPC. */
  pause(options: NativeVirtualTimeOptions = {}): Promise<NativeVirtualTimeInfo> {
    return this.command('pause', {}, options)
  }
  /**
   * Run native timer callbacks for a numeric millisecond budget or a duration
   * string in SS, MM:SS or HH:MM:SS form, with exactly two digits per field.
   * C++ parses and schedules it.
   * After the physical pause ACK, reject the first synchronous uncaught
   * main-document exception observed during this budget. Inspector receipts do
   * not prove timer-exclusive attribution. Promise rejections are excluded.
   * Positive integer budgets and their observed monotonic targets must fit native
   * exact microsecond arithmetic. Zero and RAF/idle remain unsupported.
   */
  runFor(
    duration: number | string,
    options: NativeVirtualTimeOptions = {},
  ): Promise<NativeVirtualTimeInfo> {
    z.union([z.number(), z.string()]).parse(duration)
    return this.command('runFor', { milliseconds: duration }, options)
  }
  /** Run an integer native virtual-time budget and wait for its actual completion. */
  advance(
    milliseconds: number,
    options: NativeVirtualTimeOptions = {},
  ): Promise<NativeVirtualTimeInfo> {
    z.number().int().min(1).max(86400000).parse(milliseconds)
    return this.command('advance', { milliseconds }, options)
  }
}
