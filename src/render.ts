import { randomUUID } from 'node:crypto'
import { access, mkdir, readFile, rename, rm } from 'node:fs/promises'
import { homedir, tmpdir } from 'node:os'
import { basename, dirname, extname, resolve } from 'node:path'

import * as z from 'zod'

import { raceWithAbort } from './abort.js'
import { formattedJson, writeTextFileAtomic } from './atomic-file.js'
import { decodeManifest } from './manifest.js'
import { resolveFfmpeg, runProcess, type SuiteCutProcessResult } from './process.js'
import { createSuiteCutReportRedactor } from './report-redaction.js'
import { type SuiteCutEasing } from './schemas.js'
import { buildSuiteCutSequence, type SuiteCutSequenceItem } from './timeline.js'
import {
  type SuiteCutArtifact,
  type SuiteCutAttempt,
  type SuiteCutDiagnostic,
  type SuiteCutManifest,
  type SuiteCutPageId,
  type SuiteCutTest,
  type SuiteCutZoomEvent,
} from './types.js'
import { toError, type UntrustedInput } from './untrusted.js'
import {
  resolveSuiteCutCameraRuns,
  type ResolvedSuiteCutCameraZoom,
  type SuiteCutCameraRun,
} from './zoom.js'

const OutputContainerSchema = z.enum(['mp4', 'webm', 'mov', 'mkv'])
const RenderQualitySchema = z.enum(['standard', 'high', 'master'])
const RenderFailureModeSchema = z.enum(['strict', 'best-effort'])
const ColorRangeSchema = z.enum(['auto', 'full', 'limited'])
const FfmpegNameSchema = z
  .string()
  .trim()
  .min(1)
  .regex(/^[A-Za-z0-9][A-Za-z0-9_.-]*$/u, 'must be an FFmpeg name, not an option')

const OutputSchema = z
  .strictObject({
    container: OutputContainerSchema.exactOptional(),
    format: OutputContainerSchema.exactOptional(),
    videoCodec: FfmpegNameSchema.exactOptional(),
    audioCodec: FfmpegNameSchema.exactOptional(),
    pixelFormat: FfmpegNameSchema.exactOptional(),
    colorRange: ColorRangeSchema.exactOptional(),
    width: z.number().int().positive().max(7680).multipleOf(2).exactOptional(),
    height: z.number().int().positive().max(4320).multipleOf(2).exactOptional(),
    framesPerSecond: z.union([z.literal(30), z.literal(60)]).exactOptional(),
    quality: RenderQualitySchema.exactOptional(),
  })
  .superRefine((output, context) => {
    if ((output.width === undefined) !== (output.height === undefined)) {
      context.addIssue({
        code: 'custom',
        message: 'width and height must be set together',
        path: output.width === undefined ? ['width'] : ['height'],
      })
    }
    if (
      output.container !== undefined &&
      output.format !== undefined &&
      output.container !== output.format
    ) {
      context.addIssue({
        code: 'custom',
        message: 'container and the deprecated format alias must match',
        path: ['format'],
      })
    }
  })

const RenderConfigSchema = z.strictObject({
  output: OutputSchema.exactOptional(),
  narrationEnabled: z.boolean().exactOptional(),
  resultHoldMs: z.number().nonnegative().exactOptional(),
  backgroundColor: z.string().min(1).exactOptional(),
  ffmpegPath: z.string().min(1).exactOptional(),
  failureMode: RenderFailureModeSchema.exactOptional(),
})

const RenderRequestSchema = z.strictObject({
  manifestPath: z.string().min(1),
  outputPath: z.string().min(1),
  signal: z.instanceof(AbortSignal).exactOptional(),
  selection: z
    .strictObject({
      testId: z.string().min(1).exactOptional(),
      retry: z.number().int().nonnegative().exactOptional(),
    })
    .exactOptional(),
  config: RenderConfigSchema.exactOptional(),
})

/** A video container supported by the SuiteCut renderer. */
export type SuiteCutOutputContainer = z.infer<typeof OutputContainerSchema>
/** Backward-compatible name for SuiteCutOutputContainer. */
export type SuiteCutOutputFormat = SuiteCutOutputContainer
/** How SuiteCut maps output sample values to black and white. */
export type SuiteCutColorRange = z.infer<typeof ColorRangeSchema>
/** Named encoder settings with codec-specific CRF and speed values. */
export type SuiteCutRenderQuality = z.infer<typeof RenderQualitySchema>
/** Controls whether FFmpeg diagnostics stop the render. */
export type SuiteCutRenderFailureMode = z.infer<typeof RenderFailureModeSchema>
/** Validated dimensions, cadence, container, and encoder quality. */
export type SuiteCutRenderOutputConfig = z.infer<typeof OutputSchema>
/** Video, audio, layout, and failure settings for one render. */
export type SuiteCutRenderConfig = z.infer<typeof RenderConfigSchema>
/** Selects a recorded attempt and names its rendered output file. */
export type SuiteCutRenderRequest = z.infer<typeof RenderRequestSchema>

const DEFAULT_RENDER_WIDTH = 1920
const DEFAULT_RENDER_HEIGHT = 1080
const DEFAULT_RENDER_FRAMES_PER_SECOND = 30
const DEFAULT_RENDER_QUALITY: SuiteCutRenderQuality = 'standard'

const ENCODER_QUALITY_PROFILES = {
  standard: {
    h264Crf: 22,
    hevcCrf: 28,
    vpxCrf: 32,
    av1Crf: 35,
    preset: 'medium',
    vpxCpuUsed: 4,
    av1Preset: 8,
    audioBitrate: '160k',
    webAudioBitrate: '128k',
  },
  high: {
    h264Crf: 18,
    hevcCrf: 22,
    vpxCrf: 24,
    av1Crf: 28,
    preset: 'slow',
    vpxCpuUsed: 2,
    av1Preset: 6,
    audioBitrate: '192k',
    webAudioBitrate: '160k',
  },
  master: {
    h264Crf: 14,
    hevcCrf: 17,
    vpxCrf: 18,
    av1Crf: 20,
    preset: 'slower',
    vpxCpuUsed: 1,
    av1Preset: 4,
    audioBitrate: '256k',
    webAudioBitrate: '192k',
  },
} as const

type ResolvedColorRange = Exclude<SuiteCutColorRange, 'auto'>

interface ResolvedOutputSettings {
  container: SuiteCutOutputContainer
  videoCodec: string
  audioCodec: string
  pixelFormat: string
  colorRange: ResolvedColorRange
}

const encoderCache = new Map<string, Promise<ReadonlySet<string>>>()

function inferredContainer(outputPath: string): SuiteCutOutputContainer {
  const extension = extname(outputPath).toLowerCase()
  if (extension === '.webm') return 'webm'
  if (extension === '.mov') return 'mov'
  if (extension === '.mkv') return 'mkv'
  return 'mp4'
}

function defaultVideoCodec(container: SuiteCutOutputContainer): string {
  return container === 'webm' ? 'libvpx-vp9' : 'libx264'
}

function defaultAudioCodec(container: SuiteCutOutputContainer): string {
  return container === 'webm' ? 'libopus' : 'aac'
}

function ffmpegMuxer(container: SuiteCutOutputContainer): string {
  return container === 'mkv' ? 'matroska' : container
}

function defaultPixelFormat(videoCodec: string): string {
  if (videoCodec.startsWith('prores')) return 'yuv422p10le'
  return 'yuv420p'
}

function isRgbPixelFormat(pixelFormat: string): boolean {
  return /^(?:argb|bgra|bgr|gbr|rgb|rgba)/u.test(pixelFormat)
}

function frameColorMetadata(settings: ResolvedOutputSettings): string {
  const values = [`range=${settings.colorRange}`, 'color_primaries=bt709', 'color_trc=bt709']
  if (!isRgbPixelFormat(settings.pixelFormat)) values.push('colorspace=bt709')
  return `setparams=${values.join(':')}`
}

function resolveOutputSettings(
  outputPath: string,
  output: SuiteCutRenderOutputConfig | undefined,
): ResolvedOutputSettings {
  const container = output?.container ?? output?.format ?? inferredContainer(outputPath)
  const videoCodec = output?.videoCodec ?? defaultVideoCodec(container)
  const audioCodec = output?.audioCodec ?? defaultAudioCodec(container)
  const pixelFormat = output?.pixelFormat ?? defaultPixelFormat(videoCodec)
  const requestedColorRange = output?.colorRange ?? 'auto'
  const colorRange =
    requestedColorRange === 'auto'
      ? isRgbPixelFormat(pixelFormat)
        ? 'full'
        : 'limited'
      : requestedColorRange
  return { container, videoCodec, audioCodec, pixelFormat, colorRange }
}

function videoQualityArgs(videoCodec: string, quality: SuiteCutRenderQuality): string[] {
  const profile = ENCODER_QUALITY_PROFILES[quality]
  if (videoCodec === 'libx264') {
    return ['-crf', String(profile.h264Crf), '-preset', profile.preset, '-profile:v', 'high']
  }
  if (videoCodec === 'libx265') {
    return ['-crf', String(profile.hevcCrf), '-preset', profile.preset]
  }
  if (videoCodec === 'libvpx' || videoCodec === 'libvpx-vp9') {
    return [
      '-crf',
      String(profile.vpxCrf),
      '-b:v',
      '0',
      '-cpu-used',
      String(profile.vpxCpuUsed),
      '-row-mt',
      '1',
    ]
  }
  if (videoCodec === 'libsvtav1') {
    return [
      '-crf',
      String(profile.av1Crf),
      '-preset',
      String(profile.av1Preset),
      '-svtav1-params',
      'fast-decode=1',
    ]
  }
  if (videoCodec.startsWith('prores')) {
    const proresProfile = quality === 'standard' ? '2' : '3'
    return ['-profile:v', proresProfile]
  }
  return []
}

function audioQualityArgs(audioCodec: string, quality: SuiteCutRenderQuality): string[] {
  if (audioCodec === 'libopus' || audioCodec === 'libvorbis') {
    return ['-b:a', ENCODER_QUALITY_PROFILES[quality].webAudioBitrate]
  }
  if (
    audioCodec === 'aac' ||
    audioCodec === 'aac_at' ||
    audioCodec === 'libmp3lame' ||
    audioCodec === 'ac3'
  ) {
    return ['-b:a', ENCODER_QUALITY_PROFILES[quality].audioBitrate]
  }
  return []
}

async function installedEncoders(executable: string): Promise<ReadonlySet<string>> {
  const cacheKey = `${executable}\0${process.env.PATH ?? ''}`
  const cached = encoderCache.get(cacheKey)
  if (cached !== undefined) return cached
  const pending = runProcess(executable, ['-hide_banner', '-encoders'], { timeoutMs: 30_000 })
    .then((result) => {
      if (result.exitCode !== 0) {
        throw new Error(`SuiteCut could not inspect FFmpeg encoders: ${result.stderr.trim()}`)
      }
      const encoders = new Set<string>()
      for (const line of result.stdout.split('\n')) {
        const match = /^\s*[VAS][A-Z.]{5}\s+(\S+)/u.exec(line)
        if (match?.[1] !== undefined) encoders.add(match[1])
      }
      return encoders
    })
    .catch((error: UntrustedInput) => {
      encoderCache.delete(cacheKey)
      throw toError(error)
    })
  encoderCache.set(cacheKey, pending)
  return pending
}

async function requireEncoder(
  executable: string,
  encoder: string,
  kind: 'video' | 'audio',
): Promise<void> {
  const encoders = await installedEncoders(executable)
  if (!encoders.has(encoder)) {
    throw new Error(
      `FFmpeg does not provide the requested ${kind} encoder ${encoder}. Run "${executable} -encoders" to list this build's encoders.`,
    )
  }
}

/** Maps a span of test time to its position in the presentation timeline. */
export interface SuiteCutEditSegment {
  kind: 'play' | 'hold'
  pageId: SuiteCutPageId
  executionStartMs: number
  executionEndMs: number
  presentationStartMs: number
  presentationEndMs: number
}

/** Records the inputs, timing edits, diagnostics, and FFmpeg result of a render. */
export interface SuiteCutRenderReport {
  attemptId: string
  testId: string
  startedAt: string
  endedAt: string
  status: 'rendered' | 'failed'
  outputPath?: string
  executionDurationMs: number
  presentationDurationMs: number
  edits: SuiteCutEditSegment[]
  diagnostics: SuiteCutDiagnostic[]
  ffmpeg?: {
    executable: string
    args: string[]
    exitCode: number | null
    signal: NodeJS.Signals | null
    stderrTail: string
  }
}

interface NarrationPlacement {
  eventId: string
  startMs: number
  durationMs: number
}

function selectAttempt(
  manifest: SuiteCutManifest,
  selection: { testId?: string; retry?: number } | undefined,
): { test: SuiteCutTest; attempt: SuiteCutAttempt } {
  const test =
    selection?.testId === undefined
      ? manifest.tests[0]
      : manifest.tests.find((candidate) => candidate.id === selection.testId)
  if (test === undefined) throw new Error('SuiteCut could not find the selected test')
  const attempt =
    selection?.retry === undefined
      ? test.attempts.at(-1)
      : test.attempts.find((candidate) => candidate.retry === selection.retry)
  if (attempt === undefined) throw new Error('SuiteCut could not find the selected test attempt')
  return { test, attempt }
}

function artifactPath(manifestPath: string, artifact: SuiteCutArtifact): string {
  if (artifact.pathKind === 'absolute') return artifact.path
  return resolve(dirname(manifestPath), artifact.path)
}

function buildNarrationPlacements(
  attempt: SuiteCutAttempt,
  narrationArtifacts: readonly SuiteCutArtifact[],
): NarrationPlacement[] {
  const placements: NarrationPlacement[] = []
  const artifactBySourceEventId = new Map(
    narrationArtifacts.map((artifact) => [artifact.sourceEventId, artifact]),
  )
  const mediaByArtifactId = new Map(attempt.media.map((media) => [media.artifactId, media]))
  const narrationEvents = attempt.events
    .filter((event) => event.type === 'narration')
    .sort((left, right) => left.atMs - right.atMs)
  for (const event of narrationEvents) {
    const artifact = artifactBySourceEventId.get(event.id)
    if (artifact === undefined) continue
    const media = mediaByArtifactId.get(artifact.id)
    if (media === undefined) continue
    placements.push({
      eventId: event.id,
      startMs: event.atMs,
      durationMs: media.durationMs,
    })
  }
  return placements
}

function ffmpegEasingExpression(easing: SuiteCutEasing, progress: string): string {
  if (easing === 'linear') return progress
  if (easing === 'ease-in') return `pow(${progress},3)`
  if (easing === 'ease-out') return `1-pow(1-${progress},3)`
  return `if(lt(${progress},0.5),4*pow(${progress},3),1-pow(-2*${progress}+2,3)/2)`
}

interface CameraTarget {
  scale: number
  travelX: number
  travelY: number
}

interface CameraPiece {
  startMs: number
  endMs: number
  scale: string
  travelX: string
  travelY: string
}

function cameraTarget(
  zoom: ResolvedSuiteCutCameraZoom,
  sourceWidth: number,
  sourceHeight: number,
): CameraTarget {
  const scale = zoom.frame.scale
  const centerX = (zoom.frame.centerX / zoom.event.viewport.width) * sourceWidth
  const centerY = (zoom.frame.centerY / zoom.event.viewport.height) * sourceHeight
  const travelX = (scale * centerX - sourceWidth / 2) / (scale - 1)
  const travelY = (scale * centerY - sourceHeight / 2) / (scale - 1)
  return {
    scale,
    travelX: Math.max(0, Math.min(sourceWidth, travelX)),
    travelY: Math.max(0, Math.min(sourceHeight, travelY)),
  }
}

function fixed(value: number): string {
  return value.toFixed(6)
}

function interpolate(from: number, to: number, progress: string): string {
  if (Math.abs(to - from) <= 0.000_001) return fixed(to)
  return `${fixed(from)}+${fixed(to - from)}*(${progress})`
}

function cameraProgress(
  clock: string,
  startMs: number,
  endMs: number,
  easing: SuiteCutEasing,
): string {
  const startSeconds = fixed(startMs / 1_000)
  const durationSeconds = fixed((endMs - startMs) / 1_000)
  const linear = `max(0,min(1,((${clock})-${startSeconds})/${durationSeconds}))`
  return ffmpegEasingExpression(easing, linear)
}

function cameraPieces(
  run: SuiteCutCameraRun,
  sourceWidth: number,
  sourceHeight: number,
  clock: string,
): CameraPiece[] {
  const pieces: CameraPiece[] = []
  const first = run.zooms[0]
  if (first === undefined) return pieces
  let previous = cameraTarget(first, sourceWidth, sourceHeight)
  if (first.enterEndMs > first.startMs) {
    const progress = cameraProgress(
      clock,
      first.startMs,
      first.enterEndMs,
      first.resolved.enter.easing,
    )
    pieces.push({
      startMs: first.startMs,
      endMs: first.enterEndMs,
      scale: interpolate(1, previous.scale, progress),
      travelX: fixed(previous.travelX),
      travelY: fixed(previous.travelY),
    })
  }

  let steadyStartMs = first.enterEndMs
  for (const zoom of run.zooms.slice(1)) {
    if (zoom.startMs > steadyStartMs) {
      pieces.push({
        startMs: steadyStartMs,
        endMs: zoom.startMs,
        scale: fixed(previous.scale),
        travelX: fixed(previous.travelX),
        travelY: fixed(previous.travelY),
      })
    }
    const next = cameraTarget(zoom, sourceWidth, sourceHeight)
    if (zoom.enterEndMs > zoom.startMs) {
      const progress = cameraProgress(
        clock,
        zoom.startMs,
        zoom.enterEndMs,
        zoom.resolved.enter.easing,
      )
      pieces.push({
        startMs: zoom.startMs,
        endMs: zoom.enterEndMs,
        scale: interpolate(previous.scale, next.scale, progress),
        travelX: interpolate(previous.travelX, next.travelX, progress),
        travelY: interpolate(previous.travelY, next.travelY, progress),
      })
    }
    previous = next
    steadyStartMs = zoom.enterEndMs
  }

  const last = run.zooms.at(-1)
  if (last === undefined) return pieces
  if (last.exitStartMs > steadyStartMs) {
    pieces.push({
      startMs: steadyStartMs,
      endMs: last.exitStartMs,
      scale: fixed(previous.scale),
      travelX: fixed(previous.travelX),
      travelY: fixed(previous.travelY),
    })
  }
  if (last.endMs > last.exitStartMs) {
    const progress = cameraProgress(clock, last.exitStartMs, last.endMs, last.resolved.exit.easing)
    pieces.push({
      startMs: last.exitStartMs,
      endMs: last.endMs,
      scale: interpolate(previous.scale, 1, progress),
      travelX: fixed(previous.travelX),
      travelY: fixed(previous.travelY),
    })
  }
  return pieces.filter((piece) => piece.endMs > piece.startMs)
}

function piecewiseExpression(
  pieces: readonly CameraPiece[],
  clock: string,
  select: (piece: CameraPiece) => string,
  fallback: string,
): string {
  return pieces.reduceRight((expression, piece) => {
    const start = fixed(piece.startMs / 1_000)
    const end = fixed(piece.endMs / 1_000)
    return `if(gte(${clock},${start})*lt(${clock},${end}),${select(piece)},${expression})`
  }, fallback)
}

function cameraVisualFilter(
  runs: readonly SuiteCutCameraRun[],
  item: SuiteCutSequenceItem,
  sourceWidth: number,
  sourceHeight: number,
  framesPerSecond: number,
): string {
  const relevantRuns = runs.filter(
    (run) => run.endMs > item.executionStartMs && run.startMs < item.executionEndMs,
  )
  if (relevantRuns.length === 0) return ''

  const clock = `(on/${framesPerSecond}+${fixed(item.executionStartMs / 1_000)})`
  const pieces = relevantRuns.flatMap((run) => cameraPieces(run, sourceWidth, sourceHeight, clock))
  const scale = piecewiseExpression(pieces, clock, (piece) => piece.scale, '1')
  const travelX = piecewiseExpression(
    pieces,
    clock,
    (piece) => piece.travelX,
    fixed(sourceWidth / 2),
  )
  const travelY = piecewiseExpression(
    pieces,
    clock,
    (piece) => piece.travelY,
    fixed(sourceHeight / 2),
  )
  const left = `-((${scale})-1)*(${travelX})`
  const right = `${sourceWidth}*(${scale})-((${scale})-1)*(${travelX})`
  const top = `-((${scale})-1)*(${travelY})`
  const bottom = `${sourceHeight}*(${scale})-((${scale})-1)*(${travelY})`
  const camera =
    `perspective=x0='${left}':y0='${top}':x1='${right}':y1='${top}':` +
    `x2='${left}':y2='${bottom}':x3='${right}':y3='${bottom}':` +
    'sense=destination:eval=frame:interpolation=linear'
  return `,${camera}`
}

function color(value: string, opacity = 1): string {
  const named: Record<string, string> = {
    black: '000000',
    white: 'FFFFFF',
    red: 'EF4444',
    green: '22C55E',
    blue: '3B82F6',
    yellow: 'FACC15',
  }
  const source = named[value.toLowerCase()] ?? value.replace(/^#/u, '')
  const expanded = /^[0-9A-Fa-f]{3}$/u.test(source)
    ? source
        .split('')
        .map((character) => character.repeat(2))
        .join('')
    : source
  if (!/^[0-9A-Fa-f]{6}$/u.test(expanded)) {
    throw new Error(`SuiteCut renderer supports hex and basic named colors, received ${value}`)
  }
  return `0x${expanded}@${opacity}`
}

function seconds(milliseconds: number): string {
  return (milliseconds / 1_000).toFixed(6)
}

function sourceTrimFilter(
  item: SuiteCutSequenceItem,
  sourceStartedAtMs: number,
  framesPerSecond: number,
): string {
  const sourceAtMs = Math.max(0, item.executionStartMs - sourceStartedAtMs)
  if (item.kind === 'hold') {
    return `trim=start=${seconds(sourceAtMs)}:duration=${seconds(1_000 / framesPerSecond)},setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=${seconds(item.durationMs)},trim=duration=${seconds(item.durationMs)},fps=${framesPerSecond}`
  }

  const leadingHoldMs = Math.min(
    item.durationMs,
    Math.max(0, sourceStartedAtMs - item.executionStartMs),
  )
  const sourceDurationMs = Math.max(40, item.durationMs - leadingHoldMs)
  const leadingPad =
    leadingHoldMs > 0 ? `,tpad=start_mode=clone:start_duration=${seconds(leadingHoldMs)}` : ''
  return `trim=start=${seconds(sourceAtMs)}:duration=${seconds(sourceDurationMs)},setpts=PTS-STARTPTS${leadingPad},tpad=stop_mode=clone:stop_duration=${seconds(item.durationMs)},trim=duration=${seconds(item.durationMs)},fps=${framesPerSecond}`
}

function ffmpegReport(
  result: SuiteCutProcessResult,
  redact: (text: string) => string,
): NonNullable<SuiteCutRenderReport['ffmpeg']> {
  return {
    executable: basename(result.executable),
    args: result.args.map(redact),
    exitCode: result.exitCode,
    signal: result.signal,
    stderrTail: redact(result.stderr.slice(-8_000)),
  }
}

function edits(sequence: readonly SuiteCutSequenceItem[]): SuiteCutEditSegment[] {
  let cursor = 0
  return sequence.map((item) => {
    const edit: SuiteCutEditSegment = {
      kind: item.kind,
      pageId: item.pageId,
      executionStartMs: item.executionStartMs,
      executionEndMs: item.executionEndMs,
      presentationStartMs: cursor,
      presentationEndMs: cursor + item.durationMs,
    }
    cursor += item.durationMs
    return edit
  })
}

/** Renders one recorded SuiteCut attempt with an installed FFmpeg encoder combination. */
export async function renderSuiteCut(input: SuiteCutRenderRequest): Promise<SuiteCutRenderReport> {
  const request = RenderRequestSchema.parse(input)
  request.signal?.throwIfAborted()
  const manifestPath = resolve(request.manifestPath)
  const outputPath = resolve(request.outputPath)
  const reportPath = `${outputPath}.suitecut.json`
  const temporaryOutputPath = resolve(
    dirname(outputPath),
    `.${basename(outputPath)}.${randomUUID()}.tmp`,
  )
  const startedAt = new Date()
  const manifest = decodeManifest(
    JSON.parse(await readFile(manifestPath, 'utf8')) as UntrustedInput,
  )
  const { test, attempt } = selectAttempt(manifest, request.selection)
  const baseRedactions = [
    { value: manifestPath, replacement: '<manifest>' },
    { value: temporaryOutputPath, replacement: '<output>' },
    { value: dirname(outputPath), replacement: '<output-directory>' },
    { value: manifest.rootDirectory, replacement: '<project>' },
    { value: homedir(), replacement: '<home>' },
    { value: tmpdir(), replacement: '<temporary-directory>' },
  ]
  let redactReportText = createSuiteCutReportRedactor(baseRedactions)
  const diagnostics: SuiteCutDiagnostic[] = attempt.diagnostics.map((diagnostic) => ({
    ...diagnostic,
    message: redactReportText(diagnostic.message),
  }))
  const baseReport: SuiteCutRenderReport = {
    attemptId: attempt.id,
    testId: test.id,
    startedAt: startedAt.toISOString(),
    endedAt: startedAt.toISOString(),
    status: 'failed',
    executionDurationMs: attempt.durationMs,
    presentationDurationMs: 0,
    edits: [],
    diagnostics,
  }

  try {
    const outputSettings = resolveOutputSettings(outputPath, request.config?.output)
    const width = request.config?.output?.width ?? DEFAULT_RENDER_WIDTH
    const height = request.config?.output?.height ?? DEFAULT_RENDER_HEIGHT
    const fps = request.config?.output?.framesPerSecond ?? DEFAULT_RENDER_FRAMES_PER_SECOND
    const quality = request.config?.output?.quality ?? DEFAULT_RENDER_QUALITY
    const failureMode = request.config?.failureMode ?? 'strict'
    const resultHoldMs = request.config?.resultHoldMs ?? 0
    const background = color(request.config?.backgroundColor ?? '#0B1020')
    const sourceArtifacts = attempt.artifacts
      .filter((artifact) => artifact.role === 'source-video')
      .map((artifact) => ({ artifact, path: artifactPath(manifestPath, artifact) }))
    if (sourceArtifacts.length === 0) throw new Error('The selected attempt has no source video')
    const allNarrationArtifacts = attempt.artifacts
      .filter((artifact) => artifact.role === 'narration-audio')
      .map((artifact) => ({ artifact, path: artifactPath(manifestPath, artifact) }))
    let narrationArtifacts =
      request.config?.narrationEnabled === false ? [] : [...allNarrationArtifacts]
    if (narrationArtifacts.length > 0) {
      const availableNarrationArtifacts: typeof narrationArtifacts = []
      for (const narrationArtifact of narrationArtifacts) {
        try {
          await access(narrationArtifact.path)
          availableNarrationArtifacts.push(narrationArtifact)
        } catch (error) {
          if (failureMode === 'strict') {
            throw new Error(
              `SuiteCut narration artifact is unavailable: ${narrationArtifact.path}`,
              {
                cause: error,
              },
            )
          }
          diagnostics.push({
            level: 'warning',
            code: 'OPTIONAL_TRACK_OMITTED',
            message: `Omitted unavailable narration artifact ${narrationArtifact.artifact.id}.`,
            artifactId: narrationArtifact.artifact.id,
          })
        }
      }
      narrationArtifacts = availableNarrationArtifacts
    }

    const executable = await raceWithAbort(
      resolveFfmpeg(request.config?.ffmpegPath),
      request.signal,
    )
    await raceWithAbort(
      requireEncoder(executable, outputSettings.videoCodec, 'video'),
      request.signal,
    )
    if (narrationArtifacts.length > 0) {
      try {
        await raceWithAbort(
          requireEncoder(executable, outputSettings.audioCodec, 'audio'),
          request.signal,
        )
      } catch (error) {
        request.signal?.throwIfAborted()
        if (failureMode === 'strict') throw error
        diagnostics.push({
          level: 'warning',
          code: 'OPTIONAL_TRACK_OMITTED',
          message: `Omitted narration because FFmpeg does not provide encoder ${outputSettings.audioCodec}.`,
        })
        narrationArtifacts = []
      }
    }
    const mediaInputs = [...sourceArtifacts, ...narrationArtifacts]
    redactReportText = createSuiteCutReportRedactor([
      ...baseRedactions,
      { value: executable, replacement: '<ffmpeg>' },
      ...mediaInputs.map((inputArtifact, index) => ({
        value: resolve(inputArtifact.path),
        replacement: `<input-${String(index + 1)}>`,
      })),
    ])
    for (const diagnostic of diagnostics) {
      diagnostic.message = redactReportText(diagnostic.message)
    }
    const protectedInputPaths = new Set([
      manifestPath,
      ...mediaInputs.map((inputArtifact) => resolve(inputArtifact.path)),
    ])
    if (protectedInputPaths.has(outputPath) || protectedInputPaths.has(reportPath)) {
      throw new Error('SuiteCut output and report paths cannot overwrite an input file')
    }

    const narrationPlacements = buildNarrationPlacements(
      attempt,
      narrationArtifacts.map(({ artifact }) => artifact),
    )
    const sequence = buildSuiteCutSequence(attempt, resultHoldMs, fps)
    const editMap = edits(sequence)
    const presentationDurationMs = editMap.at(-1)?.presentationEndMs ?? 0
    const cameraRuns = resolveSuiteCutCameraRuns(
      attempt.events.filter((event): event is SuiteCutZoomEvent => event.type === 'zoom'),
    )
    const cameraRunsByPage = new Map<SuiteCutPageId, SuiteCutCameraRun[]>()
    for (const run of cameraRuns) {
      const pageRuns = cameraRunsByPage.get(run.pageId) ?? []
      pageRuns.push(run)
      cameraRunsByPage.set(run.pageId, pageRuns)
    }

    const inputIndexByPage = new Map<string, number>()
    sourceArtifacts.forEach(({ artifact }, index) => {
      if (artifact.pageId !== undefined) inputIndexByPage.set(artifact.pageId, index)
    })
    const timingByPage = new Map(attempt.videoTiming.map((timing) => [timing.pageId, timing]))
    const mediaByArtifactId = new Map(attempt.media.map((media) => [media.artifactId, media]))
    const eventById = new Map(attempt.events.map((event) => [event.id, event]))
    const placementByEventId = new Map(
      narrationPlacements.map((placement) => [placement.eventId, placement]),
    )
    const filters: string[] = []
    const videoLabels: string[] = []
    for (const [index, item] of sequence.entries()) {
      const inputIndex = inputIndexByPage.get(item.pageId)
      if (inputIndex === undefined) throw new Error(`Missing source video for page ${item.pageId}`)
      const timing = timingByPage.get(item.pageId)
      const sourceStartMs = timing?.sourceStartedAtMs ?? 0
      const label = `segment${index}`
      const trim = sourceTrimFilter(item, sourceStartMs, fps)
      const sourceArtifact = sourceArtifacts[inputIndex]?.artifact
      const sourceMedia =
        sourceArtifact === undefined ? undefined : mediaByArtifactId.get(sourceArtifact.id)
      const sourceVideo = sourceMedia?.streams.find((stream) => stream.kind === 'video')
      if (sourceVideo === undefined) {
        throw new Error(`Missing video stream metadata for page ${item.pageId}`)
      }
      let visual = trim
      visual += cameraVisualFilter(
        cameraRunsByPage.get(item.pageId) ?? [],
        item,
        sourceVideo.width,
        sourceVideo.height,
        fps,
      )
      const inputRange = sourceVideo.colorRange ?? 'auto'
      visual += `,scale=${width}:${height}:flags=lanczos+accurate_rnd+full_chroma_int:force_original_aspect_ratio=decrease:in_range=${inputRange}:out_range=${outputSettings.colorRange},pad=${width}:${height}:(ow-iw)/2:(oh-ih)/2:${background},setsar=1,format=${outputSettings.pixelFormat}`
      filters.push(`[${inputIndex}:v]${visual}[${label}]`)
      videoLabels.push(`[${label}]`)
    }
    const joinedVideoLabel = videoLabels.length === 1 ? videoLabels[0] : '[joinedvideo]'
    if (videoLabels.length > 1) {
      filters.push(
        `${videoLabels.join('')}concat=n=${videoLabels.length}:v=1:a=0${joinedVideoLabel}`,
      )
    }
    filters.push(
      `${joinedVideoLabel}tpad=stop_mode=clone:stop_duration=${seconds(presentationDurationMs)},trim=duration=${seconds(presentationDurationMs)},format=${outputSettings.pixelFormat},${frameColorMetadata(outputSettings)}[video0]`,
    )

    const audioLabels: string[] = []
    narrationArtifacts.forEach(({ artifact }, narrationIndex) => {
      const event =
        artifact.sourceEventId === undefined ? undefined : eventById.get(artifact.sourceEventId)
      if (event === undefined) return
      const inputIndex = sourceArtifacts.length + narrationIndex
      const placement = placementByEventId.get(event.id)
      if (placement === undefined) return
      const delayMs = Math.round(placement.startMs)
      const label = `audio${narrationIndex}`
      filters.push(`[${inputIndex}:a]asetpts=PTS-STARTPTS,adelay=${delayMs}|${delayMs}[${label}]`)
      audioLabels.push(`[${label}]`)
    })
    if (audioLabels.length > 0) {
      filters.push(
        `${audioLabels.join('')}amix=inputs=${audioLabels.length}:duration=longest:dropout_transition=0:normalize=0,volume=6dB,alimiter=limit=0.75:attack=5:release=50:level=false[audioout]`,
      )
    }

    const args = ['-hide_banner', '-loglevel', 'error', '-nostats', '-y']
    for (const inputArtifact of mediaInputs) args.push('-i', inputArtifact.path)
    args.push('-filter_complex', filters.join(';'), '-map', '[video0]')
    if (audioLabels.length > 0) args.push('-map', '[audioout]')
    args.push('-r', String(fps), '-t', seconds(presentationDurationMs))
    args.push(
      '-c:v',
      outputSettings.videoCodec,
      ...videoQualityArgs(outputSettings.videoCodec, quality),
      '-pix_fmt',
      outputSettings.pixelFormat,
      '-color_range',
      outputSettings.colorRange === 'full' ? 'pc' : 'tv',
      '-color_primaries',
      'bt709',
      '-color_trc',
      'bt709',
    )
    if (!isRgbPixelFormat(outputSettings.pixelFormat)) args.push('-colorspace', 'bt709')
    if (outputSettings.container === 'mp4' || outputSettings.container === 'mov') {
      args.push('-movflags', '+faststart')
    }
    if (audioLabels.length > 0) {
      args.push(
        '-c:a',
        outputSettings.audioCodec,
        ...audioQualityArgs(outputSettings.audioCodec, quality),
      )
    }
    args.push('-f', ffmpegMuxer(outputSettings.container))
    args.push(temporaryOutputPath)

    await mkdir(dirname(outputPath), { recursive: true })
    const processResult = await runProcess(
      executable,
      args,
      request.signal === undefined ? {} : { signal: request.signal },
    )
    baseReport.ffmpeg = ffmpegReport(processResult, redactReportText)
    baseReport.presentationDurationMs = presentationDurationMs
    baseReport.edits = editMap
    if (processResult.exitCode !== 0) {
      diagnostics.push({
        level: 'error',
        code: 'FFMPEG_FAILED',
        message: 'FFmpeg failed to render the selected attempt.',
      })
      throw new Error(
        redactReportText(
          `FFmpeg failed with exit code ${String(processResult.exitCode)}: ${processResult.stderr.slice(-2_000)}`,
        ),
      )
    }
    await rename(temporaryOutputPath, outputPath)
    baseReport.status = 'rendered'
    baseReport.outputPath = basename(outputPath)
    baseReport.endedAt = new Date().toISOString()
    await writeTextFileAtomic(reportPath, formattedJson(baseReport))
    return baseReport
  } catch (error) {
    await rm(temporaryOutputPath, { force: true }).catch(() => undefined)
    baseReport.endedAt = new Date().toISOString()
    if (!diagnostics.some((diagnostic) => diagnostic.code === 'FFMPEG_FAILED')) {
      diagnostics.push({
        level: 'error',
        code: 'RENDER_FAILED',
        message: redactReportText(error instanceof Error ? error.message : String(error)),
      })
    }
    await writeTextFileAtomic(reportPath, formattedJson(baseReport))
    throw error
  }
}
