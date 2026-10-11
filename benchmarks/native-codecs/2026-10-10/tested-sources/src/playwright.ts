import { randomUUID } from 'node:crypto'
import { mkdir } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'

import {
  chromium,
  firefox,
  webkit,
  type Browser,
  type BrowserContext,
  type BrowserContextOptions,
  type LaunchOptions,
  type Page,
} from 'playwright'

import { raceWithAbort } from './abort.js'
import { formattedJson, writeTextFileAtomic } from './atomic-file.js'
import { resolveAudioPluginReferences } from './audio-plugin-loader.js'
import { DEFAULT_SUITE_CUT_VIEWPORT } from './capture.js'
import { SUITECUT_MANIFEST_SCHEMA_VERSION } from './constants.js'
import { type SuiteCutFixture } from './fixtures.js'
import { decodeManifest } from './manifest.js'
import { createNarrationPipeline } from './narration.js'
import { createAttempt, rawArtifactSink } from './recording-output.js'
import {
  type SuiteCutAudioPluginReference,
  type SuiteCutBrowserName,
  SuiteCutBrowserNameSchema,
  type SuiteCutCaptureOptions,
  type SuiteCutOutputOptions,
  SuiteCutOutputOptionsSchema,
} from './schemas.js'
import { createRecordingSession, createSuiteCutFixture } from './suitecut.js'
import { launchTabAudioContext } from './tab-audio.js'
import {
  type SuiteCutEventAttachment,
  type SuiteCutManifest,
  type SuiteCutSourceLocation,
} from './types.js'
import { toError, type UntrustedInput } from './untrusted.js'
import { parseCaptureOptions } from './validation.js'

export type { LiveStreamDiagnostic } from './live-congestion.js'

export type {
  SuiteCutStreamOptions,
  SuiteCutAudioPluginReference,
  SuiteCutBrowserName,
  SuiteCutOutputOptions,
} from './schemas.js'
export type {
  SuiteCutCaptureOptions,
  SuiteCutCheckpointOptions,
  SuiteCutFixture,
  SuiteCutNarrationOptions,
  SuiteCutPointerActionOptions,
  SuiteCutScrollOptions,
  SuiteCutTypeOptions,
} from './fixtures.js'
export type { Milliseconds, SuiteCutHighlightOptions, SuiteCutZoomOptions } from './types.js'

export type SuiteCutCleanup = () => Promise<void> | void

export interface SuiteCutSetupContext {
  browser: Browser
  context: BrowserContext
  page: Page
  suitecut: SuiteCutFixture
  signal?: AbortSignal
  onCleanup: (cleanup: SuiteCutCleanup) => void
}

export type SuiteCutRecordingContext<Extensions extends object = object> = Omit<
  SuiteCutSetupContext,
  'onCleanup'
> &
  Extensions

export interface SuiteCutRecordOptions {
  browserName?: SuiteCutBrowserName
  launch?: LaunchOptions
  context?: BrowserContextOptions
  capture?: SuiteCutCaptureOptions
  audioPlugins?: readonly SuiteCutAudioPluginReference[]
  output?: SuiteCutOutputOptions
  signal?: AbortSignal
}

export interface SuiteCutDefinitionOptions<
  Extensions extends object = object,
> extends SuiteCutRecordOptions {
  setup?: (context: SuiteCutSetupContext) => Extensions | Promise<Extensions>
}

export interface SuiteCutRecordResult {
  attemptId: string
  manifest: SuiteCutManifest
  manifestPath: string
  outputDirectory: string
  testId: string
}

export type SuiteCutRecordingCallback<Extensions extends object = object> = (
  context: SuiteCutRecordingContext<Extensions>,
) => Promise<void> | void

export type SuiteCutRecorder<Extensions extends object = object> = (
  name: string,
  callback: SuiteCutRecordingCallback<Extensions>,
  options?: SuiteCutRecordOptions,
) => Promise<SuiteCutRecordResult>

interface ResolvedRecordOptions {
  browserName: SuiteCutBrowserName
  launch: LaunchOptions
  context: BrowserContextOptions
  capture: SuiteCutCaptureOptions
  audioPlugins: readonly SuiteCutAudioPluginReference[]
  output: Required<SuiteCutOutputOptions>
  signal?: AbortSignal
}

const RECORD_OPTION_KEYS = new Set([
  'browserName',
  'launch',
  'context',
  'capture',
  'audioPlugins',
  'output',
  'signal',
])
const DEFINITION_OPTION_KEYS = new Set([...RECORD_OPTION_KEYS, 'setup'])
const RESERVED_EXTENSION_KEYS = new Set(['browser', 'context', 'page', 'signal', 'suitecut'])

function validateOptionKeys(options: object, definition: boolean): void {
  const allowed = definition ? DEFINITION_OPTION_KEYS : RECORD_OPTION_KEYS
  for (const key of Object.keys(options)) {
    if (!allowed.has(key)) throw new Error(`Unrecognized SuiteCut recording option: ${key}`)
  }
}

function resolveSignal(value: AbortSignal | undefined): AbortSignal | undefined {
  if (value === undefined || value instanceof AbortSignal) return value
  throw new Error('SuiteCut recording signal must be an AbortSignal')
}

function runDirectoryName(name: string): string {
  const slug = name
    .trim()
    .toLowerCase()
    .replaceAll(/[^a-z0-9]+/gu, '-')
    .replaceAll(/^-|-$/gu, '')
    .slice(0, 64)
  const timestamp = new Date().toISOString().replaceAll(/[:.]/gu, '-')
  return `${slug === '' ? 'recording' : slug}-${timestamp}-${randomUUID().slice(0, 8)}`
}

function sourceLocation(): SuiteCutSourceLocation {
  const file = process.argv[1]
  return {
    file: file === undefined ? resolve(process.cwd(), 'recording.mjs') : resolve(file),
    line: 1,
    column: 1,
  }
}

function resolveOptions(
  defaults: SuiteCutDefinitionOptions<object>,
  overrides: SuiteCutRecordOptions,
  name: string,
): ResolvedRecordOptions {
  validateOptionKeys(defaults, true)
  validateOptionKeys(overrides, false)
  const defaultOutput = SuiteCutOutputOptionsSchema.parse(defaults.output ?? {})
  const overrideOutput = SuiteCutOutputOptionsSchema.parse(overrides.output ?? {})
  const rootDirectory = process.cwd()
  const capture = parseCaptureOptions({ ...defaults.capture, ...overrides.capture })
  const context = {
    viewport: DEFAULT_SUITE_CUT_VIEWPORT,
    ...defaults.context,
    ...overrides.context,
  }
  if (context.recordVideo !== undefined) {
    throw new Error(
      'SuiteCut context.recordVideo must be omitted because SuiteCut owns page screencasts',
    )
  }
  if (
    capture.deviceScaleFactor !== undefined &&
    context.deviceScaleFactor !== undefined &&
    context.deviceScaleFactor !== capture.deviceScaleFactor
  ) {
    throw new Error(
      `SuiteCut capture.deviceScaleFactor ${String(capture.deviceScaleFactor)} conflicts with ` +
        `context.deviceScaleFactor ${String(context.deviceScaleFactor)}. Remove the context value ` +
        'or make both values equal.',
    )
  }
  if (capture.deviceScaleFactor !== undefined) {
    context.deviceScaleFactor = capture.deviceScaleFactor
  }
  const signal = resolveSignal(overrides.signal ?? defaults.signal)
  return {
    browserName: SuiteCutBrowserNameSchema.parse(
      overrides.browserName ?? defaults.browserName ?? 'chromium',
    ),
    launch: { headless: true, ...defaults.launch, ...overrides.launch },
    context,
    capture,
    audioPlugins: [
      ...resolveAudioPluginReferences(
        overrides.audioPlugins ?? defaults.audioPlugins ?? [],
        rootDirectory,
      ).values(),
    ],
    output: {
      directory: resolve(
        rootDirectory,
        overrideOutput.directory ??
          defaultOutput.directory ??
          resolve('.suitecut', 'recordings', runDirectoryName(name)),
      ),
      manifestPath: resolve(
        rootDirectory,
        overrideOutput.manifestPath ?? defaultOutput.manifestPath ?? '.suitecut/latest-run.json',
      ),
      pathKind: overrideOutput.pathKind ?? defaultOutput.pathKind ?? 'absolute',
    },
    ...(signal === undefined ? {} : { signal }),
  }
}

function validateExtensions(value: UntrustedInput): asserts value is object {
  if (typeof value !== 'object' || value === null || Array.isArray(value)) {
    throw new Error('SuiteCut setup must return an object')
  }
  for (const key of Object.keys(value)) {
    if (RESERVED_EXTENSION_KEYS.has(key)) {
      throw new Error(`SuiteCut setup cannot replace the reserved ${key} recording value`)
    }
  }
}

async function launchBrowser(
  browserName: SuiteCutBrowserName,
  options: LaunchOptions,
): Promise<Browser> {
  switch (browserName) {
    case 'chromium':
      return chromium.launch(options)
    case 'firefox':
      return firefox.launch(options)
    case 'webkit':
      return webkit.launch(options)
  }
}

async function runCleanups(cleanups: readonly SuiteCutCleanup[], errors: Error[]): Promise<void> {
  for (const cleanup of cleanups.toReversed()) {
    try {
      await cleanup()
    } catch (error) {
      errors.push(toError(error as UntrustedInput))
    }
  }
}

async function closeResource(
  resource: { close(): Promise<void> } | undefined,
  errors: Error[],
): Promise<void> {
  if (resource === undefined) return
  try {
    await resource.close()
  } catch (error) {
    errors.push(toError(error as UntrustedInput))
  }
}

function throwRecordingErrors(name: string, errors: readonly Error[]): void {
  const firstError = errors[0]
  if (errors.length === 1 && firstError !== undefined) throw firstError
  if (errors.length > 1) {
    throw new AggregateError(errors, `SuiteCut recording "${name}" failed`)
  }
}

/** Creates a raw Playwright recorder with typed setup extensions. */
export function defineSuiteCut<Extensions extends object = object>(
  defaults: SuiteCutDefinitionOptions<Extensions> = {},
): SuiteCutRecorder<Extensions> {
  return async (name, callback, options = {}) => {
    if (name.trim() === '') throw new Error('SuiteCut recording name must not be empty')

    const resolvedOptions = resolveOptions(defaults, options, name)
    resolvedOptions.signal?.throwIfAborted()
    const { output } = resolvedOptions
    await Promise.all([
      mkdir(output.directory, { recursive: true }),
      mkdir(dirname(output.manifestPath), { recursive: true }),
    ])

    const startedAt = new Date()
    const errors: Error[] = []
    const cleanups: SuiteCutCleanup[] = []
    const artifactSink = rawArtifactSink(output.directory)
    let browser: Browser | undefined
    let context: BrowserContext | undefined
    let captured: SuiteCutEventAttachment | undefined

    try {
      if (resolvedOptions.capture.stream?.audio === true) {
        if (resolvedOptions.browserName !== 'chromium')
          throw new Error('SuiteCut tab audio supports Chromium only')
        context = await launchTabAudioContext(resolvedOptions.launch, resolvedOptions.context)
        browser = context.browser() ?? undefined
        if (!browser) throw new Error('SuiteCut audio browser is unavailable')
      } else {
        browser = await launchBrowser(resolvedOptions.browserName, resolvedOptions.launch)
        context = await browser.newContext(resolvedOptions.context)
      }
      const recordingBrowser = browser
      const recordingContext = context
      const page = recordingContext.pages()[0] ?? (await recordingContext.newPage())
      if (resolvedOptions.capture.viewport !== undefined) {
        await page.setViewportSize(resolvedOptions.capture.viewport)
      } else if (page.viewportSize() === null) {
        await page.setViewportSize(DEFAULT_SUITE_CUT_VIEWPORT)
      }

      const session = await createRecordingSession({
        captureOptions: resolvedOptions.capture,
        output: artifactSink,
        page,
        ...(resolvedOptions.signal === undefined ? {} : { signal: resolvedOptions.signal }),
      })
      const narration = createNarrationPipeline(
        artifactSink,
        session,
        resolvedOptions.audioPlugins,
        resolvedOptions.signal,
      )
      const suitecut = createSuiteCutFixture(session, narration, resolvedOptions.capture)

      try {
        let extensions = {} as Extensions
        const setup = defaults.setup
        if (setup !== undefined) {
          const setupResult: UntrustedInput = await raceWithAbort(
            Promise.race([
              Promise.resolve().then(() =>
                setup({
                  browser: recordingBrowser,
                  context: recordingContext,
                  page,
                  suitecut,
                  ...(resolvedOptions.signal === undefined
                    ? {}
                    : { signal: resolvedOptions.signal }),
                  onCleanup: (cleanup) => cleanups.push(cleanup),
                }),
              ),
              ...(session.streamFailure === undefined ? [] : [session.streamFailure]),
            ]),
            resolvedOptions.signal,
          )
          validateExtensions(setupResult)
          extensions = setupResult as Extensions
        }
        await raceWithAbort(
          Promise.race([
            Promise.resolve().then(() =>
              callback({
                ...extensions,
                browser: recordingBrowser,
                context: recordingContext,
                page,
                suitecut,
                ...(resolvedOptions.signal === undefined ? {} : { signal: resolvedOptions.signal }),
              }),
            ),
            ...(session.streamFailure === undefined ? [] : [session.streamFailure]),
          ]),
          resolvedOptions.signal,
        )
      } catch (error) {
        errors.push(toError(error as UntrustedInput))
      }

      const [recordingResult, narrationResult] = await Promise.allSettled([
        session.endRecording(),
        narration.finish(),
      ])
      if (recordingResult.status === 'rejected') {
        errors.push(toError(recordingResult.reason as UntrustedInput))
      }
      if (narrationResult.status === 'rejected') {
        errors.push(toError(narrationResult.reason as UntrustedInput))
      }
      if (recordingResult.status === 'fulfilled') {
        try {
          captured = await session.seal()
        } catch (error) {
          errors.push(toError(error as UntrustedInput))
        }
      }
    } catch (error) {
      errors.push(toError(error as UntrustedInput))
    }

    await runCleanups(cleanups, errors)
    await closeResource(context, errors)
    await closeResource(browser, errors)

    if (captured === undefined) {
      throw new AggregateError(errors, `SuiteCut could not finish recording "${name}"`)
    }

    const attempt = await createAttempt(
      captured,
      output.directory,
      output.manifestPath,
      output.pathKind,
      errors,
    )
    const testId = randomUUID()
    const endedAt = new Date()
    const manifest = decodeManifest({
      schemaVersion: SUITECUT_MANIFEST_SCHEMA_VERSION,
      startedAt: startedAt.toISOString(),
      endedAt: endedAt.toISOString(),
      status: errors.length === 0 ? 'passed' : 'failed',
      rootDirectory: process.cwd(),
      tests: [
        {
          id: testId,
          order: 0,
          title: name,
          titlePath: [name],
          projectName: resolvedOptions.browserName,
          location: sourceLocation(),
          attempts: [attempt],
        },
      ],
    })
    await writeTextFileAtomic(output.manifestPath, formattedJson(manifest))

    const result: SuiteCutRecordResult = {
      attemptId: captured.attemptId,
      manifest,
      manifestPath: output.manifestPath,
      outputDirectory: output.directory,
      testId,
    }
    throwRecordingErrors(name, errors)
    return result
  }
}

/** Records one browser flow with SuiteCut's default lifecycle. */
export const record = defineSuiteCut()
