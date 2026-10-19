import { resolve } from 'node:path'

import * as z from 'zod'

import {
  NativeScreenshotAssertionCaptureSchema,
  prepareNativeScreenshot,
  type NativeArtifactRunner,
} from './native-artifacts.js'
import { type NativeBrowser } from './native-browser.js'
import {
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import { runNativeScreenshotInput } from './native-screenshot-input.js'
import {
  consumeNativeScreenshotMismatch,
  NativeScreenshotReceiptSchema,
} from './native-screenshot-receipt.js'
import { nativeSoftOperationOptions } from './native-soft-assertions.js'
import { encodeNativeStringFields } from './native-string-wire.js'
import { type UntrustedInput } from './untrusted.js'

const maxPNGBytes = 2 * 1024 * 1024
const PathSchema = z
  .string()
  .min(1)
  .max(32768)
  .refine((path) => !path.includes('\0') && Buffer.from(path).toString('utf8') === path)
const OptionsSchema = NativeScreenshotAssertionCaptureSchema.extend({
  threshold: z.number().finite().min(0).max(1).default(0.2),
  maxDiffPixels: z
    .number()
    .int()
    .nonnegative()
    .max(2 * 1024 * 1024)
    .optional(),
  maxDiffPixelRatio: z.number().finite().min(0).max(1).optional(),
  artifactsDir: PathSchema.optional(),
  timeout: z.number().int().min(0).max(120000).default(5000),
})
export type NativeScreenshotAssertionOptions = z.input<typeof OptionsSchema>
export type NativeLocatorScreenshotAssertionOptions = Omit<
  NativeScreenshotAssertionOptions,
  'clip' | 'fullPage'
>
export function parseNativeScreenshotAssertionOptions(value: UntrustedInput, timeout?: number) {
  const config = withoutNativeRetryOperation(() => OptionsSchema.parse(value))
  if (typeof value === 'object' && value !== null && !('timeout' in value) && timeout !== undefined)
    config.timeout = withoutNativeRetryOperation(() => OptionsSchema.shape.timeout.parse(timeout))
  return config
}
const ResultSchema = z.strictObject({
  assertion: NativeScreenshotReceiptSchema.optional(),
  actualPath: PathSchema.optional(),
  diffPath: PathSchema.optional(),
  passed: z.boolean(),
  stable: z.boolean(),
  timedOut: z.boolean(),
  attempts: z.number().int().nonnegative(),
  threshold: z.number().finite().min(0).max(1),
  dimensionsMatch: z.boolean().optional(),
  differentPixels: z
    .number()
    .int()
    .nonnegative()
    .max(4 * 1024 * 1024)
    .optional(),
  totalPixels: z
    .number()
    .int()
    .positive()
    .max(4 * 1024 * 1024)
    .optional(),
  expectedWidth: z.number().int().positive().max(16384).optional(),
  expectedHeight: z.number().int().positive().max(16384).optional(),
  actualWidth: z.number().int().positive().max(16384).optional(),
  actualHeight: z.number().int().positive().max(16384).optional(),
  actual: z
    .string()
    .max(Math.ceil(maxPNGBytes / 3) * 4)
    .optional(),
  diff: z
    .string()
    .max(Math.ceil(maxPNGBytes / 3) * 4)
    .optional(),
})
function png(bytes: Buffer): Buffer {
  if (
    bytes.length < 33 ||
    bytes.length > maxPNGBytes ||
    !bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))
  )
    throw new Error('Native screenshot assertions require a PNG baseline of at most2MiB')
  return bytes
}
function outputPNG(encoded: string): Buffer {
  if (encoded.length % 4 || !/^[A-Za-z\d+/]*={0,2}$/.test(encoded))
    throw new Error('Invalid native screenshot comparison PNG encoding')
  const bytes = png(Buffer.from(encoded, 'base64'))
  if (bytes.toString('base64') !== encoded)
    throw new Error('Noncanonical native screenshot comparison PNG encoding')
  return bytes
}
/**
 * Native code owns actual presentation gates, capture attempts, image decoding,
 * stability, pixel arithmetic and the one original deadline. The SDK handles
 * typed validation only. Native finalization owns actual artifact file I/O.
 */
export async function compareNativeScreenshot(
  run: NativeArtifactRunner,
  expected: string | Buffer,
  options: NativeScreenshotAssertionOptions,
  page: object,
  isNot: boolean,
  element?: Record<string, UntrustedInput>,
  source?: Pick<NativeBrowser, 'pageId' | 'runContext'>,
  lifetimeSignal?: AbortSignal,
) {
  return runNativeRetryOperation(async () => {
    const config = withoutNativeRetryOperation(() => OptionsSchema.parse(options))
    const soft = nativeSoftOperationOptions(config.timeout, config.signal)
    if (soft) {
      config.timeout = soft.timeout
      config.signal = soft.signal
    }
    const signal =
      lifetimeSignal && config.signal
        ? AbortSignal.any([lifetimeSignal, config.signal])
        : (lifetimeSignal ?? config.signal)
    signal?.throwIfAborted()
    if (element && ('clip' in options || 'fullPage' in options))
      throw new Error('Locator screenshot assertions do not accept clip or fullPage')
    if (Buffer.isBuffer(expected)) png(expected)
    else withoutNativeRetryOperation(() => PathSchema.parse(expected))
    if (!source) throw new Error('Native screenshot input requires its actual page transport')
    const { threshold, maxDiffPixels, maxDiffPixelRatio, artifactsDir, ...capture } = config
    const { params } = prepareNativeScreenshot(capture, page)
    delete params.timeoutMs
    if (element) params.element = element
    const sourceResult = await runNativeScreenshotInput(
      run,
      source,
      expected,
      {
        screenshot: encodeNativeStringFields('artifact', params),
        threshold,
        maxDiffPixels,
        maxDiffPixelRatio,
        isNot,
        ...(artifactsDir === undefined ? {} : { artifactsDir: resolve(artifactsDir) }),
      },
      config.timeout,
      signal,
    )
    const result = withoutNativeRetryOperation(() => ResultSchema.parse(sourceResult))
    const actual = result.actual === undefined ? undefined : outputPNG(result.actual)
    const diff = result.diff === undefined ? undefined : outputPNG(result.diff)
    const { actualPath, diffPath } = result
    if (
      (actualPath && !actual) ||
      (diffPath && (!actualPath || !diff)) ||
      (result.passed && (actualPath || diffPath))
    )
      throw new Error('Invalid native screenshot artifact publication paths')
    const observedMismatch = !result.passed && consumeNativeScreenshotMismatch(sourceResult)
    const message =
      `expect(${element ? 'locator' : 'page'}).${isNot ? 'not.' : ''}toHaveScreenshot(explicit baseline) failed\n\n` +
      (result.timedOut ? `Timeout ${config.timeout}ms exceeded.\n` : '') +
      `Samples: ${result.attempts}; consecutive decoded pixels stable: ${result.stable}.\n` +
      (result.dimensionsMatch === false
        ? `Expected ${result.expectedWidth}×${result.expectedHeight}, received ${result.actualWidth}×${result.actualHeight}.\n`
        : '') +
      (result.differentPixels === undefined
        ? 'No completed native pixel comparison.\n'
        : `Different pixels: ${result.differentPixels}/${result.totalPixels}; normalized Oklab threshold: ${threshold}.\n`) +
      (actualPath ? `Actual: ${actualPath}\n` : '') +
      (diffPath ? `Difference: ${diffPath}\n` : '')
    return { result, actual, diff, actualPath, diffPath, message, observedMismatch }
  })
}
