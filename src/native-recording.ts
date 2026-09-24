import { randomUUID } from 'node:crypto'
import { mkdir } from 'node:fs/promises'
import { dirname, resolve } from 'node:path'

import * as z from 'zod'

import { raceWithAbort } from './abort.js'
import { formattedJson, writeTextFileAtomic } from './atomic-file.js'
import { resolveAudioPluginReferences } from './audio-plugin-loader.js'
import { SUITECUT_MANIFEST_SCHEMA_VERSION } from './constants.js'
import { type SuiteCutFixture } from './fixtures.js'
import { decodeManifest } from './manifest.js'
import { createNarrationPipeline } from './narration.js'
import { type NativeBrowser } from './native-browser.js'
import { type NativeLocator, type NativePage } from './native-page.js'
import { createNativeRecordingSession } from './native-session.js'
import { type SuiteCutRecordResult } from './playwright.js'
import { resolveFfmpeg } from './process.js'
import { createAttempt, rawArtifactSink } from './recording-output.js'
import {
  type SuiteCutAudioPluginReference,
  SuiteCutCaptureSizeSchema,
  type SuiteCutOutputOptions,
  SuiteCutOutputOptionsSchema,
} from './schemas.js'
import { createSuiteCutFixture } from './suitecut.js'
import { type SuiteCutEventAttachment } from './types.js'
import { toError, type UntrustedInput } from './untrusted.js'

const NativeRecordingCaptureSchema = z.strictObject({
  size: SuiteCutCaptureSizeSchema.exactOptional(),
  narrationTailMs: z.number().min(0).max(10_000).exactOptional(),
  /** Includes CEF page audio in source clips when explicitly enabled. */
  audio: z.boolean().default(false),
})

export type NativeRecordingCaptureOptions = z.input<typeof NativeRecordingCaptureSchema>

export interface NativeRecordingOptions {
  /** Caller-owned source; it can also supply a native broadcast. */
  source: NativeBrowser
  ffmpegPath?: string
  capture?: NativeRecordingCaptureOptions
  audioPlugins?: readonly SuiteCutAudioPluginReference[]
  output?: SuiteCutOutputOptions
  signal?: AbortSignal
}

export interface NativeRecordingContext {
  source: NativeBrowser
  page: NativePage
  suitecut: SuiteCutFixture<NativePage, NativeLocator>
  signal: AbortSignal
  /** Registers another caller-owned source; select it with suitecut.selectPage(). */
  addSource: (source: NativeBrowser) => Promise<NativePage>
}

/** Records a native browser flow into the standard SuiteCut manifest and render pipeline. */
export async function recordNative(
  name: string,
  callback: (context: NativeRecordingContext) => void | Promise<void>,
  options: NativeRecordingOptions,
): Promise<SuiteCutRecordResult> {
  if (!name.trim()) throw new Error('Native recording name must not be empty')
  for (const key of Object.keys(options)) {
    if (!['source', 'ffmpegPath', 'capture', 'audioPlugins', 'output', 'signal'].includes(key))
      throw new Error(`Unrecognized native recording option: ${key}`)
  }
  const capture = NativeRecordingCaptureSchema.parse(options.capture ?? {})
  const output = SuiteCutOutputOptionsSchema.parse(options.output ?? {})
  const plugins = [...resolveAudioPluginReferences(options.audioPlugins ?? []).values()]
  options.signal?.throwIfAborted()
  if (options.source.state !== 'running') throw new Error('Native source is not running')
  const ffmpegPath = await resolveFfmpeg(options.ffmpegPath)
  const directory = resolve(output.directory ?? `.suitecut/recordings/native-${randomUUID()}`)
  const manifestPath = resolve(output.manifestPath ?? '.suitecut/latest-run.json')
  await Promise.all([
    mkdir(directory, { recursive: true }),
    mkdir(dirname(manifestPath), { recursive: true }),
  ])
  const startedAt = new Date().toISOString()
  const sink = rawArtifactSink(directory)
  const controller = new AbortController()
  const signal = options.signal
    ? AbortSignal.any([options.signal, controller.signal])
    : controller.signal
  const { session, page, addSource } = await createNativeRecordingSession({
    source: options.source,
    output: sink,
    ffmpegPath,
    capture,
    signal,
  })
  const narration = createNarrationPipeline(sink, session, plugins, signal)
  const suitecut = createSuiteCutFixture<NativePage, NativeLocator>(session, narration, capture)
  const errors: Error[] = []
  let captured: SuiteCutEventAttachment | undefined
  try {
    await raceWithAbort(
      Promise.race([
        Promise.resolve().then(() => {
          signal.throwIfAborted()
          return callback({ source: options.source, page, suitecut, signal, addSource })
        }),
        ...(session.streamFailure ? [session.streamFailure] : []),
      ]),
      signal,
    )
  } catch (error) {
    errors.push(toError(error as UntrustedInput))
    controller.abort(error)
  }
  const results = await Promise.allSettled([session.endRecording(), narration.finish()])
  for (const result of results)
    if (result.status === 'rejected') errors.push(toError(result.reason as UntrustedInput))
  try {
    if (results[0]?.status === 'fulfilled') captured = await session.seal()
  } catch (error) {
    errors.push(toError(error as UntrustedInput))
  } finally {
    controller.abort()
  }
  if (!captured) throw new AggregateError(errors, `Native recording "${name}" could not finish`)
  const testId = randomUUID()
  const attempt = await createAttempt(
    captured,
    directory,
    manifestPath,
    output.pathKind ?? 'absolute',
    errors,
  )
  const manifest = decodeManifest({
    schemaVersion: SUITECUT_MANIFEST_SCHEMA_VERSION,
    startedAt,
    endedAt: new Date().toISOString(),
    status: errors.length ? 'failed' : 'passed',
    rootDirectory: process.cwd(),
    tests: [
      {
        id: testId,
        order: 0,
        title: name,
        titlePath: [name],
        projectName: 'native-chromium',
        location: { file: resolve(process.argv[1] ?? 'native-recording.mjs'), line: 1, column: 1 },
        attempts: [attempt],
      },
    ],
  })
  await writeTextFileAtomic(manifestPath, formattedJson(manifest))
  if (errors.length)
    throw new AggregateError(errors, `Native recording "${name}" failed; manifest: ${manifestPath}`)
  return {
    attemptId: captured.attemptId,
    manifest,
    manifestPath,
    outputDirectory: directory,
    testId,
  }
}
