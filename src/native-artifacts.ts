// SPDX-License-Identifier: MIT
import { extname } from 'node:path'

import * as z from 'zod'

import { writeFileAtomic } from './atomic-file.js'
import { subscribeNativeActionHook } from './native-action-hooks.js'
import { type NativeBrowser } from './native-browser.js'
import {
  runNativeRetryOperation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import { runNativeArtifactConfiguration } from './native-screenshot-input.js'
import { encodeNativeStringFields } from './native-string-wire.js'
import { type UntrustedInput } from './untrusted.js'

const PathSchema = z
  .string()
  .min(1)
  .max(32_768)
  .refine((path) => !path.includes('\0'))
const TimeoutSchema = z.number().int().min(0).max(120_000)
const ClipSchema = z.strictObject({
  x: z.number().finite(),
  y: z.number().finite(),
  width: z.number().positive('Expected options.clip.width to be greater than 0').max(32_768),
  height: z.number().positive('Expected options.clip.height to be greater than 0').max(32_768),
})
export interface NativeScreenshotMask {
  nativeScreenshotDescriptor(): {
    page: object
    target: Record<string, UntrustedInput>
  }
}
const MaskSchema = z.custom<NativeScreenshotMask>(
  (value) =>
    typeof value === 'object' &&
    value !== null &&
    'nativeScreenshotDescriptor' in value &&
    typeof value.nativeScreenshotDescriptor === 'function',
  'Expected a native locator for screenshot mask',
)
export type NativeBeforeScreenshotHook = () => void | Promise<void>
const BeforeScreenshotSchema = z.custom<NativeBeforeScreenshotHook>(
  (value) => typeof value === 'function',
  'Expected a beforeScreenshot callback',
)
const ScreenshotSchema = z.strictObject({
  type: z.enum(['png', 'jpeg', 'webp']).optional(),
  path: PathSchema.optional(),
  quality: z
    .number()
    .int()
    .min(0, 'Expected options.quality to be between 0 and 100')
    .max(100, 'Expected options.quality to be between 0 and 100')
    .optional(),
  fullPage: z.boolean().default(false),
  clip: ClipSchema.optional(),
  omitBackground: z.boolean().default(false),
  scale: z.enum(['css', 'device']).default('device'),
  caret: z.enum(['hide', 'initial']).default('hide'),
  animations: z.enum(['allow', 'disabled']).default('allow'),
  style: z
    .string()
    .max(1024 * 1024)
    .optional(),
  mask: z.array(MaskSchema).max(1024).optional(),
  maskColor: z.string().min(1).max(4096).default('#FF00FF'),
  beforeScreenshot: BeforeScreenshotSchema.optional(),
  __testHookBeforeScreenshot: BeforeScreenshotSchema.optional(),
  timeout: TimeoutSchema.optional(),
  signal: z.instanceof(AbortSignal).optional(),
})

export type NativeScreenshotOptions = z.input<typeof ScreenshotSchema>
export type NativeLocatorScreenshotOptions = Omit<NativeScreenshotOptions, 'fullPage' | 'clip'>
const LocatorScreenshotSchema = ScreenshotSchema.omit({ fullPage: true, clip: true })

const LengthSchema = z.union([
  z.number().finite().nonnegative(),
  z.string().regex(/^\s*(?:\d+(?:\.\d*)?|\.\d+)\s*(?:px|in|cm|mm)?\s*$/i),
])
const PDFSchema = z.strictObject({
  path: PathSchema.optional(),
  scale: z.number().min(0.1).max(2).default(1),
  displayHeaderFooter: z.boolean().default(false),
  headerTemplate: z
    .string()
    .max(1024 * 1024)
    .default(''),
  footerTemplate: z
    .string()
    .max(1024 * 1024)
    .default(''),
  printBackground: z.boolean().default(false),
  landscape: z.boolean().default(false),
  pageRanges: z.string().max(4096).default(''),
  format: z
    .string()
    .transform((format, context) => {
      const match = Object.keys(PAPER_INCHES).find(
        (paper) => paper.toLowerCase() === format.toLowerCase(),
      )
      if (!match) {
        context.addIssue({ code: 'custom', message: 'Unsupported PDF paper format' })
        return z.NEVER
      }
      return match as keyof typeof PAPER_INCHES
    })
    .optional(),
  width: LengthSchema.optional(),
  height: LengthSchema.optional(),
  margin: z
    .strictObject({
      top: LengthSchema.optional(),
      bottom: LengthSchema.optional(),
      left: LengthSchema.optional(),
      right: LengthSchema.optional(),
    })
    .optional(),
  preferCSSPageSize: z.boolean().default(false),
  tagged: z.boolean().default(false),
  outline: z.boolean().default(false),
  timeout: TimeoutSchema.optional(),
})

export type NativePDFOptions = z.input<typeof PDFSchema>
export type NativeArtifactRunner = (
  params: Record<string, UntrustedInput>,
  signal?: AbortSignal,
) => Promise<UntrustedInput>

const ArtifactResultSchema = z.strictObject({
  data: z.string().max(Math.ceil((128 * 1024 * 1024) / 3) * 4),
})
const PAPER_INCHES = {
  Letter: [8.5, 11],
  Legal: [8.5, 14],
  Tabloid: [11, 17],
  Ledger: [17, 11],
  A0: [33.1, 46.8],
  A1: [23.4, 33.1],
  A2: [16.54, 23.4],
  A3: [11.7, 16.54],
  A4: [8.27, 11.7],
  A5: [5.83, 8.27],
  A6: [4.13, 5.83],
} as const

function screenshotType(options: NativeScreenshotOptions): 'png' | 'jpeg' | 'webp' {
  if (options.type) return options.type
  if (!options.path) return 'png'
  switch (extname(options.path).toLowerCase()) {
    case '.jpg':
    case '.jpeg':
      return 'jpeg'
    case '.webp':
      return 'webp'
    case '.png':
      return 'png'
    default:
      throw new Error(
        `path: unsupported mime type "${extname(options.path).toLowerCase() === '.txt' ? 'text/plain' : 'application/octet-stream'}"`,
      )
  }
}

function decodeArtifact(value: UntrustedInput, kind: 'png' | 'jpeg' | 'webp' | 'pdf'): Buffer {
  const { data } = withoutNativeRetryOperation(() => ArtifactResultSchema.parse(value))
  if (!data.length || data.length % 4 !== 0 || !/^[A-Za-z\d+/]*={0,2}$/.test(data))
    throw new Error('Invalid native artifact encoding')
  const bytes = Buffer.from(data, 'base64')
  if (bytes.length > 128 * 1024 * 1024) throw new Error('Native artifact exceeds 128 MiB')
  const valid =
    kind === 'png'
      ? bytes.subarray(0, 8).equals(Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]))
      : kind === 'jpeg'
        ? bytes[0] === 255 && bytes[1] === 216 && bytes[2] === 255
        : kind === 'webp'
          ? bytes.toString('ascii', 0, 4) === 'RIFF' && bytes.toString('ascii', 8, 12) === 'WEBP'
          : bytes.toString('ascii', 0, 5) === '%PDF-'
  if (!valid) throw new Error(`Native artifact is not a ${kind} document`)
  return bytes
}

/** Typed screenshot options shared by capture and explicit-baseline assertions. */
export const NativeScreenshotAssertionCaptureSchema = ScreenshotSchema.omit({
  type: true,
  path: true,
  quality: true,
  beforeScreenshot: true,
  __testHookBeforeScreenshot: true,
}).extend({
  animations: z.enum(['allow', 'disabled']).default('disabled'),
  scale: z.enum(['css', 'device']).default('css'),
})

/** Builds typed native parameters without dispatching capture or owning retries. */
export function prepareNativeScreenshot(options: NativeScreenshotOptions, page?: object) {
  const config = withoutNativeRetryOperation(() => ScreenshotSchema.parse(options))
  config.signal?.throwIfAborted()
  const format = screenshotType(config)
  if (format === 'png' && config.quality !== undefined)
    throw new Error('options.quality is unsupported for the png screenshot format')
  const params: Record<string, UntrustedInput> = { ...config }
  delete params.path
  delete params.timeout
  delete params.type
  delete params.signal
  delete params.mask
  delete params.beforeScreenshot
  delete params.__testHookBeforeScreenshot
  if (config.mask?.length) {
    params.masks = config.mask.map((mask) => {
      const descriptor = mask.nativeScreenshotDescriptor()
      if (!page || descriptor.page !== page)
        throw new Error('Screenshot mask locator belongs to a different native page')
      return descriptor.target
    })
  }
  if (format !== 'png') params.quality = config.quality ?? (format === 'webp' ? 100 : 80)
  if (format === 'jpeg') params.omitBackground = false
  return { config, format, params }
}

/** Captures encoded pixels through an operation owned by the native engine. */
export async function captureNativeScreenshot(
  run: NativeArtifactRunner,
  options: NativeScreenshotOptions = {},
  page?: object,
  source?: NativeBrowser,
  lifetimeSignal?: AbortSignal,
  element?: Record<string, UntrustedInput>,
): Promise<Buffer> {
  return runNativeRetryOperation(async () => {
    const { config, format, params } = prepareNativeScreenshot(options, page)
    if (config.beforeScreenshot && config.__testHookBeforeScreenshot)
      throw new Error('Provide a single beforeScreenshot callback')
    const hook = config.beforeScreenshot ?? config.__testHookBeforeScreenshot
    if (hook && !source) throw new Error('Native beforeScreenshot scheduling is unavailable')
    const action = hook ? new AbortController() : undefined
    const registration =
      hook && source && action ? subscribeNativeActionHook(source, hook, action) : undefined
    const signals = [
      lifetimeSignal ?? source?.pageSignal ?? source?.signal,
      config.signal,
      action?.signal,
    ].filter((signal): signal is AbortSignal => signal !== undefined)
    const signal = signals.length > 1 ? AbortSignal.any(signals) : signals[0]
    if (element) params.element = element
    if (registration) {
      params.beforeScreenshot = true
      params.clientToken = registration.clientToken
    }
    try {
      const bytes = decodeArtifact(
        await runNativeArtifactConfiguration(
          run,
          source,
          encodeNativeStringFields('artifact', { action: 'screenshot', format, ...params }),
          config.timeout,
          signal,
        ),
        format,
      )
      if (config.path) await writeFileAtomic(config.path, bytes)
      return bytes
    } finally {
      registration?.dispose()
    }
  })
}

/** Uses the native locator's frame-aware geometry inside the capture operation. */
export async function captureNativeElementScreenshot(
  run: NativeArtifactRunner,
  element: Record<string, UntrustedInput>,
  options: NativeLocatorScreenshotOptions = {},
  page?: object,
  source?: NativeBrowser,
  lifetimeSignal?: AbortSignal,
): Promise<Buffer> {
  const config = withoutNativeRetryOperation(() => LocatorScreenshotSchema.parse(options))
  return captureNativeScreenshot(run, config, page, source, lifetimeSignal, element)
}

function inches(value: string | number): number {
  if (typeof value === 'number') return value / 96
  const match = /^\s*(\d+(?:\.\d*)?|\.\d+)\s*(px|in|cm|mm)?\s*$/i.exec(value)
  if (!match?.[1]) throw new Error('Invalid PDF length')
  const amount = Number(match[1])
  switch (match[2]?.toLowerCase()) {
    case 'in':
      return amount
    case 'cm':
      return amount / 2.54
    case 'mm':
      return amount / 25.4
    case 'px':
    case undefined:
    default:
      return amount / 96
  }
}

/** Prints using Chromium's native paged layout and PDF encoder. */
export async function printNativePDF(
  run: NativeArtifactRunner,
  options: NativePDFOptions = {},
  source?: NativeBrowser,
  lifetimeSignal?: AbortSignal,
): Promise<Buffer> {
  return runNativeRetryOperation(async () => {
    const config = withoutNativeRetryOperation(() => PDFSchema.parse(options))
    const [paperWidth, paperHeight] = config.format
      ? PAPER_INCHES[config.format]
      : [inches(config.width ?? '8.5in'), inches(config.height ?? '11in')]
    if (paperWidth <= 0 || paperHeight <= 0 || paperWidth > 200 || paperHeight > 200)
      throw new Error('PDF paper dimensions must be greater than zero and at most 200 inches')
    const params: Record<string, UntrustedInput> = { ...config }
    for (const key of ['path', 'timeout', 'format', 'width', 'height', 'margin']) delete params[key]
    const request = {
      action: 'pdf',
      ...params,
      paperWidth,
      paperHeight,
      marginTop: inches(config.margin?.top ?? 0),
      marginBottom: inches(config.margin?.bottom ?? 0),
      marginLeft: inches(config.margin?.left ?? 0),
      marginRight: inches(config.margin?.right ?? 0),
    }
    const bytes = decodeArtifact(
      await runNativeArtifactConfiguration(
        run,
        source,
        request,
        config.timeout,
        lifetimeSignal ?? source?.pageSignal ?? source?.signal,
      ),
      'pdf',
    )
    if (config.path) await writeFileAtomic(config.path, bytes)
    return bytes
  })
}

const MediaSchema = z.strictObject({
  media: z.enum(['screen', 'print']).nullable().optional(),
  colorScheme: z.enum(['light', 'dark', 'no-preference']).nullable().optional(),
  reducedMotion: z.enum(['reduce', 'no-preference']).nullable().optional(),
  forcedColors: z.enum(['active', 'none']).nullable().optional(),
  contrast: z.enum(['more', 'no-preference']).nullable().optional(),
})
export type NativeMediaOptions = z.input<typeof MediaSchema>

/** Updates persistent media overrides; omitted settings retain their prior value. */
export async function emulateNativeMedia(
  run: NativeArtifactRunner,
  options: NativeMediaOptions = {},
): Promise<void> {
  const config = withoutNativeRetryOperation(() => MediaSchema.parse(options))
  await run({ action: 'emulateMedia', ...config })
}
