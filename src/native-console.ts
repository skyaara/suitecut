// SPDX-License-Identifier: MIT
/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase lifecycle module edges. */
import { inspect } from 'node:util'

import * as z from 'zod'

import {
  NativeConsoleInfoSchema,
  NativeFrameInfoSchema,
  type NativeConsoleInfo,
} from './native-protocol.js'
import { type UntrustedInput } from './untrusted.js'

import type { NativeBrowser } from './native-browser.js'
import type { NativePage } from './native-page.js'

export const nativeConsoleInspection = inspect.custom
const filterSchema = z.strictObject({ filter: z.enum(['all', 'since-navigation']).optional() })
export type NativeConsoleSource = Pick<NativeBrowser, 'signal' | 'runAutomation'>
interface ClaimState {
  signal: AbortSignal
  pending: Set<number>
  requestedSequence: number
  deliveredSequence: number
  scheduled: boolean
  draining?: Promise<void> | undefined
  error?: Error | undefined
}
const claims = new WeakMap<NativeConsoleSource, ClaimState>()
function claimState(source: NativeConsoleSource): ClaimState {
  let state = claims.get(source)
  if (state?.signal !== source.signal) {
    state = {
      signal: source.signal,
      pending: new Set(),
      requestedSequence: 0,
      deliveredSequence: 0,
      scheduled: false,
    }
    claims.set(source, state)
  }
  return state
}
function drainClaims(source: NativeConsoleSource, state: ClaimState): Promise<void> {
  if (state.draining) return state.draining
  const run = async () => {
    while (state.pending.size || state.requestedSequence > state.deliveredSequence) {
      while (state.pending.size) {
        const ids = [...state.pending].slice(0, 64)
        for (const id of ids) state.pending.delete(id)
        await source.runAutomation('claimConsoleArguments', { handles: ids }, state.signal)
      }
      if (state.requestedSequence > state.deliveredSequence) {
        const sequence = state.requestedSequence
        await source.runAutomation('acknowledgeConsole', { sequence }, state.signal)
        state.deliveredSequence = sequence
      }
    }
  }
  state.draining = run().finally(() => {
    state.draining = undefined
    if (state.pending.size || state.requestedSequence > state.deliveredSequence)
      scheduleClaims(source, state)
  })
  void state.draining.catch((error: UntrustedInput) => {
    state.error = error instanceof Error ? error : new Error('Native console delivery failed')
    state.pending.clear()
  })
  return state.draining
}
function scheduleClaims(source: NativeConsoleSource, state: ClaimState): void {
  if (state.scheduled || state.draining || state.error || state.signal.aborted) return
  state.scheduled = true
  queueMicrotask(() => {
    state.scheduled = false
    if (!state.error && !state.signal.aborted) void drainClaims(source, state)
  })
}

/** Delivery cursors coalesce; no per-event promise queue survives backpressure. */
export function acknowledgeNativeConsoleEvent(source: NativeConsoleSource, sequence: number): void {
  const state = claimState(source)
  state.requestedSequence = Math.max(state.requestedSequence, sequence)
  scheduleClaims(source, state)
}
async function flushClaims(source: NativeConsoleSource): Promise<void> {
  const state = claimState(source)
  if (state.error && !source.signal.aborted) throw state.error
  await drainClaims(source, state)
}

/** Handles are identities allocated and retained by the native Runtime controller. */
export function nativeConsoleArguments<T>(
  info: NativeConsoleInfo,
  create: (value: UntrustedInput) => T,
  source: NativeConsoleSource,
): T[] {
  if (info.argumentsError) throw new Error(info.argumentsError)
  if (!info.arguments) throw new Error('Native console argument metadata is unavailable')
  const state = claimState(source)
  if (state.error) throw state.error
  for (const argument of info.arguments) {
    if (!state.pending.has(argument.handle) && state.pending.size >= 1024)
      throw new Error('Native console claim capacity exceeded')
    state.pending.add(argument.handle)
  }
  scheduleClaims(source, state)
  return info.arguments.map(create)
}

/** Read the bounded native journal; no SDK duplicate journal or clock is maintained. */
export async function readNativeConsoleMessages(
  page: NativePage,
  options: { filter?: 'all' | 'since-navigation' } = {},
): Promise<NativeConsoleInfo[]> {
  const config = filterSchema.parse(options)
  return z.array(NativeConsoleInfoSchema).parse(
    await page.runAutomation('console', {
      action: 'list',
      filter: config.filter ?? 'since-navigation',
    }),
  )
}

export async function clearNativeConsoleMessages(page: NativePage): Promise<void> {
  await flushClaims(page.source)
  await page.runAutomation('console', { action: 'clear' })
}

export const NativePageErrorInfoSchema = z.object({
  name: z.string().max(1_000_000),
  frame: NativeFrameInfoSchema.optional(),
  message: z.string().max(1_000_000),
  stack: z.string().max(1_000_000),
})
export type NativePageErrorInfo = z.infer<typeof NativePageErrorInfoSchema>
export function createNativePageError(record: NativePageErrorInfo): Error {
  const error = new Error(record.message)
  error.name = record.name
  error.stack = record.stack
  return error
}
export async function readNativePageErrors(
  page: NativePage,
  options: { filter?: 'all' | 'since-navigation' } = {},
): Promise<Error[]> {
  const config = filterSchema.parse(options)
  const records = z.array(NativePageErrorInfoSchema).parse(
    await page.runAutomation('console', {
      action: 'errors',
      filter: config.filter ?? 'since-navigation',
    }),
  )
  return records.map(createNativePageError)
}
