// SPDX-License-Identifier: MIT
// Actual authored callbacks only. Native Runner owns every retry and deadline.
import * as z from 'zod'

import {
  nativeRetryInvocation,
  nativeRetryInfrastructureFailure,
} from './native-callback-operations.js'
import { nativeRunnerCallsites } from './native-runner-location.js'
import { type NativeTestAnnotation } from './native-suite.js'
import { type UntrustedInput } from './untrusted.js'

export type NativeRetryCallback = () => UntrustedInput | void
export interface NativeToPassOptions {
  readonly timeout?: number | undefined
  readonly intervals?: readonly number[] | undefined
}
const duration = z.number().int().min(0).max(Number.MAX_SAFE_INTEGER)
export const NativeToPassOptionsSchema = z.strictObject({
  timeout: duration.optional(),
  intervals: z.array(duration).min(1).max(64).optional(),
})
type Bridge = (
  callback: NativeRetryCallback,
  options: NativeToPassOptions,
  location?: NativeTestAnnotation['location'],
) => Promise<void>
let bridge: Bridge | undefined
export function installNativeCallbackAssertionBridge(actual: Bridge): void {
  if (bridge || typeof actual !== 'function')
    throw new Error('Native callback assertion bridge already installed or invalid')
  bridge = actual
}
export class NativeCallbackAssertions {
  constructor(private readonly callback: NativeRetryCallback) {}
  get not(): never {
    throw new Error('Native callback toPass does not support .not')
  }
  async toPass(options: NativeToPassOptions = {}): Promise<void> {
    if (nativeRetryInvocation()) {
      const error = new Error('Nested native callback toPass is unsupported')
      nativeRetryInfrastructureFailure(error)
      throw error
    }
    const config = NativeToPassOptionsSchema.parse(options)
    if (!bridge) throw new Error('Native callback toPass requires an actual native Runner callback')
    // The host registers the real closure before returning this Promise. No
    // callback is invoked here, nor is an interval/default/deadline selected.
    await bridge(this.callback, config, nativeRunnerCallsites()[0])
  }
}
