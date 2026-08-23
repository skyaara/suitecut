import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { dirname, extname, resolve } from 'node:path'

import * as z from 'zod'

import { decodeManifest } from './manifest.js'
import { resolveFfmpeg, runProcess, type SuiteCutProcessResult } from './process.js'
import {
  type SuiteCutArtifact,
  type SuiteCutAttempt,
  type SuiteCutDiagnostic,
  type SuiteCutManifest,
  type SuiteCutPageId,
  type SuiteCutTest,
  type SuiteCutZoomEvent,
} from './types.js'
import { type UntrustedInput } from './untrusted.js'

const OutputFormatSchema = z.enum(['mp4', 'webm'])
const RenderQualitySchema = z.enum(['standard', 'high', 'master'])
const RenderFailureModeSchema = z.enum(['strict', 'best-effort'])

const OutputSchema = z
  .strictObject({
    format: OutputFormatSchema.exactOptional(),
    width: z.number().int().positive().max(7680).multipleOf(2).exactOptional(),
    height: z.number().int().positive().max(4320).multipleOf(2).exactOptional(),
    framesPerSecond: z.union([z.literal(30), z.literal(60)]).exactOptional(),
    quality: RenderQualitySchema.exactOptional(),
  })
  .superRefine((output, context) => {
    if ((output.width === undefined) === (output.height === undefined)) return
    context.addIssue({
      code: 'custom',
      message: 'width and height must be set together',
      path: output.width === undefined ? ['width'] : ['height'],
    })
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
  selection: z
    .strictObject({
      testId: z.string().min(1).exactOptional(),
      retry: z.number().int().nonnegative().exactOptional(),
    })
    .exactOptional(),
  config: RenderConfigSchema.exactOptional(),
})

/** A video container supported by the SuiteCut renderer. */
export type SuiteCutOutputFormat = z.infer<typeof OutputFormatSchema>
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

const DEFAULT_RENDER_WIDTH = 3840
const DEFAULT_RENDER_HEIGHT = 2160
const DEFAULT_RENDER_FRAMES_PER_SECOND = 60
const DEFAULT_RENDER_QUALITY: SuiteCutRenderQuality = 'high'

interface EncoderQualityProfile {
  mp4: {
    crf: number
    preset: 'medium' | 'slow' | 'slower'
    audioBitrate: '160k' | '192k' | '256k'
  }
  webm: {
    crf: number
    cpuUsed: 1 | 2 | 4
    audioBitrate: '128k' | '160k' | '192k'
  }
}

const ENCODER_QUALITY_PROFILES = {
  standard: {
    mp4: { crf: 22, preset: 'medium', audioBitrate: '160k' },
    webm: { crf: 32, cpuUsed: 4, audioBitrate: '128k' },
  },
  high: {
    mp4: { crf: 18, preset: 'slow', audioBitrate: '192k' },
    webm: { crf: 24, cpuUsed: 2, audioBitrate: '160k' },
  },
  master: {
    mp4: { crf: 14, preset: 'slower', audioBitrate: '256k' },
    webm: { crf: 18, cpuUsed: 1, audioBitrate: '192k' },
  },
} as const satisfies Record<SuiteCutRenderQuality, EncoderQualityProfile>

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

interface SequenceItem {
  kind: 'play' | 'hold'
  pageId: SuiteCutPageId
  executionStartMs: number
  executionEndMs: number
  durationMs: number
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

function pageAt(atMs: number, attempt: SuiteCutAttempt): SuiteCutPageId {
  const mainPage = attempt.pages.find((page) => page.kind === 'main') ?? attempt.pages[0]
  if (mainPage === undefined) throw new Error('The selected attempt has no recorded pages')
  let selected = mainPage.id
  for (const event of attempt.events) {
    if (event.atMs > atMs) break
    selected = event.pageId
  }
  return selected
}

function buildNarrationPlacements(
  attempt: SuiteCutAttempt,
  narrationArtifacts: readonly SuiteCutArtifact[],
): NarrationPlacement[] {
  const placements: NarrationPlacement[] = []
  const narrationEvents = attempt.events
    .filter((event) => event.type === 'narration')
    .sort((left, right) => left.atMs - right.atMs)
  for (const event of narrationEvents) {
    const artifact = narrationArtifacts.find((candidate) => candidate.sourceEventId === event.id)
    if (artifact === undefined) continue
    const media = attempt.media.find((candidate) => candidate.artifactId === artifact.id)
    if (media === undefined) continue
    placements.push({
      eventId: event.id,
      startMs: event.atMs,
      durationMs: media.durationMs,
    })
  }
  return placements
}

function activeZoom(
  atMs: number,
  pageId: string,
  attempt: SuiteCutAttempt,
): SuiteCutZoomEvent | undefined {
  return attempt.events.findLast((event): event is SuiteCutZoomEvent => {
    if (event.type !== 'zoom' || event.pageId !== pageId || event.atMs > atMs) return false
    return atMs < event.atMs + (event.options.holdMs ?? 900)
  })
}

function buildSequence(attempt: SuiteCutAttempt, extraTailMs: number): SequenceItem[] {
  const boundarySet = new Set<number>([0, attempt.durationMs])
  for (const event of attempt.events) {
    if (event.atMs >= 0 && event.atMs <= attempt.durationMs) boundarySet.add(event.atMs)
    if (event.type === 'zoom') {
      boundarySet.add(Math.min(attempt.durationMs, event.atMs + (event.options.holdMs ?? 900)))
    }
  }
  const boundaries = [...boundarySet].sort((left, right) => left - right)
  const sequence: SequenceItem[] = []
  for (let index = 0; index < boundaries.length - 1; index += 1) {
    const start = boundaries[index]
    const end = boundaries[index + 1]
    if (start === undefined || end === undefined) continue
    if (end > start) {
      sequence.push({
        kind: 'play',
        pageId: pageAt(start, attempt),
        executionStartMs: start,
        executionEndMs: end,
        durationMs: end - start,
      })
    }
  }
  if (extraTailMs > 0) {
    sequence.push({
      kind: 'hold',
      pageId: pageAt(attempt.durationMs, attempt),
      executionStartMs: attempt.durationMs,
      executionEndMs: attempt.durationMs,
      durationMs: extraTailMs,
    })
  }
  return sequence
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

function ffmpegReport(result: SuiteCutProcessResult): NonNullable<SuiteCutRenderReport['ffmpeg']> {
  return {
    executable: result.executable,
    args: result.args,
    exitCode: result.exitCode,
    signal: result.signal,
    stderrTail: result.stderr.slice(-8_000),
  }
}

function edits(sequence: readonly SequenceItem[]): SuiteCutEditSegment[] {
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

/** Renders one recorded SuiteCut attempt to an MP4 or WebM file. */
export async function renderSuiteCut(input: SuiteCutRenderRequest): Promise<SuiteCutRenderReport> {
  const request = RenderRequestSchema.parse(input)
  const manifestPath = resolve(request.manifestPath)
  const outputPath = resolve(request.outputPath)
  const reportPath = `${outputPath}.suitecut.json`
  const startedAt = new Date()
  const manifest = decodeManifest(
    JSON.parse(await readFile(manifestPath, 'utf8')) as UntrustedInput,
  )
  const { test, attempt } = selectAttempt(manifest, request.selection)
  const diagnostics: SuiteCutDiagnostic[] = [...attempt.diagnostics]
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
    const format =
      request.config?.output?.format ??
      (extname(outputPath).toLowerCase() === '.webm' ? 'webm' : 'mp4')
    const width = request.config?.output?.width ?? DEFAULT_RENDER_WIDTH
    const height = request.config?.output?.height ?? DEFAULT_RENDER_HEIGHT
    const fps = request.config?.output?.framesPerSecond ?? DEFAULT_RENDER_FRAMES_PER_SECOND
    const quality = request.config?.output?.quality ?? DEFAULT_RENDER_QUALITY
    const encoderProfile = ENCODER_QUALITY_PROFILES[quality]
    const resultHoldMs = request.config?.resultHoldMs ?? 0
    const background = color(request.config?.backgroundColor ?? '#0B1020')
    const sourceArtifacts = attempt.artifacts
      .filter((artifact) => artifact.role === 'source-video')
      .map((artifact) => ({ artifact, path: artifactPath(manifestPath, artifact) }))
    if (sourceArtifacts.length === 0) throw new Error('The selected attempt has no source video')
    const allNarrationArtifacts = attempt.artifacts
      .filter((artifact) => artifact.role === 'narration-audio')
      .map((artifact) => ({ artifact, path: artifactPath(manifestPath, artifact) }))
    const narrationArtifacts =
      request.config?.narrationEnabled === false ? [] : allNarrationArtifacts
    const mediaInputs = [...sourceArtifacts, ...narrationArtifacts]
    if (mediaInputs.some((inputArtifact) => resolve(inputArtifact.path) === outputPath)) {
      throw new Error('SuiteCut output cannot overwrite an input artifact')
    }

    const narrationPlacements = buildNarrationPlacements(
      attempt,
      allNarrationArtifacts.map(({ artifact }) => artifact),
    )
    const sequence = buildSequence(attempt, resultHoldMs)
    const editMap = edits(sequence)
    const presentationDurationMs = editMap.at(-1)?.presentationEndMs ?? 0

    const inputIndexByPage = new Map<string, number>()
    sourceArtifacts.forEach(({ artifact }, index) => {
      if (artifact.pageId !== undefined) inputIndexByPage.set(artifact.pageId, index)
    })
    const filters: string[] = []
    const videoLabels: string[] = []
    for (const [index, item] of sequence.entries()) {
      const inputIndex = inputIndexByPage.get(item.pageId)
      if (inputIndex === undefined) throw new Error(`Missing source video for page ${item.pageId}`)
      const timing = attempt.videoTiming.find((candidate) => candidate.pageId === item.pageId)
      const sourceStartMs = timing?.sourceStartedAtMs ?? 0
      const sourceAtMs = Math.max(0, item.executionStartMs - sourceStartMs)
      const label = `segment${index}`
      const trim =
        item.kind === 'play'
          ? `trim=start=${seconds(sourceAtMs)}:duration=${seconds(item.durationMs)},setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=${seconds(item.durationMs)},trim=duration=${seconds(item.durationMs)}`
          : `trim=start=${seconds(sourceAtMs)}:duration=${seconds(40)},setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=${seconds(item.durationMs)},trim=duration=${seconds(item.durationMs)}`
      const zoom = activeZoom(item.executionStartMs, item.pageId, attempt)
      let visual = trim
      if (zoom !== undefined) {
        const scale = zoom.options.scale ?? 1.15
        const centerX = (zoom.rect.x + zoom.rect.width / 2) / zoom.viewport.width
        const centerY = (zoom.rect.y + zoom.rect.height / 2) / zoom.viewport.height
        visual += `,crop=w=iw/${scale}:h=ih/${scale}:x='max(0,min(iw-ow,iw*${centerX}-ow/2))':y='max(0,min(ih-oh,ih*${centerY}-oh/2))'`
      }
      visual += `,scale=${width}:${height}:flags=lanczos+accurate_rnd+full_chroma_int:force_original_aspect_ratio=decrease:in_range=full:out_range=tv,pad=${width}:${height}:(ow-iw)/2:(oh-ih)/2:${background},setsar=1,format=yuv420p`
      filters.push(`[${inputIndex}:v]${visual}[${label}]`)
      videoLabels.push(`[${label}]`)
    }
    filters.push(`${videoLabels.join('')}concat=n=${videoLabels.length}:v=1:a=0[joinedvideo]`)
    filters.push(
      `[joinedvideo]tpad=stop_mode=clone:stop_duration=${seconds(presentationDurationMs)},trim=duration=${seconds(presentationDurationMs)},fps=${fps}[video0]`,
    )

    const audioLabels: string[] = []
    narrationArtifacts.forEach(({ artifact }, narrationIndex) => {
      const event = attempt.events.find((candidate) => candidate.id === artifact.sourceEventId)
      if (event === undefined) return
      const inputIndex = sourceArtifacts.length + narrationIndex
      const placement = narrationPlacements.find((candidate) => candidate.eventId === event.id)
      if (placement === undefined) return
      const delayMs = Math.round(placement.startMs)
      const label = `audio${narrationIndex}`
      filters.push(`[${inputIndex}:a]asetpts=PTS-STARTPTS,adelay=${delayMs}|${delayMs}[${label}]`)
      audioLabels.push(`[${label}]`)
    })
    if (audioLabels.length > 0) {
      filters.push(
        `${audioLabels.join('')}amix=inputs=${audioLabels.length}:duration=longest:dropout_transition=0[audioout]`,
      )
    }

    const executable = await resolveFfmpeg(request.config?.ffmpegPath)
    const args = ['-y']
    for (const inputArtifact of mediaInputs) args.push('-i', inputArtifact.path)
    args.push('-filter_complex', filters.join(';'), '-map', '[video0]')
    if (audioLabels.length > 0) args.push('-map', '[audioout]')
    args.push('-r', String(fps), '-t', seconds(presentationDurationMs))
    if (format === 'mp4') {
      args.push(
        '-c:v',
        'libx264',
        '-crf',
        String(encoderProfile.mp4.crf),
        '-preset',
        encoderProfile.mp4.preset,
        '-profile:v',
        'high',
        '-pix_fmt',
        'yuv420p',
        '-color_primaries',
        'bt709',
        '-color_trc',
        'bt709',
        '-colorspace',
        'bt709',
        '-movflags',
        '+faststart',
      )
      if (audioLabels.length > 0) args.push('-c:a', 'aac', '-b:a', encoderProfile.mp4.audioBitrate)
    } else {
      args.push(
        '-c:v',
        'libvpx-vp9',
        '-crf',
        String(encoderProfile.webm.crf),
        '-b:v',
        '0',
        '-cpu-used',
        String(encoderProfile.webm.cpuUsed),
        '-row-mt',
        '1',
        '-pix_fmt',
        'yuv420p',
      )
      if (audioLabels.length > 0)
        args.push('-c:a', 'libopus', '-b:a', encoderProfile.webm.audioBitrate)
    }
    args.push(outputPath)

    await mkdir(dirname(outputPath), { recursive: true })
    const processResult = await runProcess(executable, args)
    baseReport.ffmpeg = ffmpegReport(processResult)
    baseReport.presentationDurationMs = presentationDurationMs
    baseReport.edits = editMap
    if (processResult.exitCode !== 0) {
      diagnostics.push({
        level: 'error',
        code: 'FFMPEG_FAILED',
        message: 'FFmpeg failed to render the selected attempt.',
      })
      throw new Error(
        `FFmpeg failed with exit code ${String(processResult.exitCode)}: ${processResult.stderr.slice(-2_000)}`,
      )
    }
    baseReport.status = 'rendered'
    baseReport.outputPath = outputPath
    baseReport.endedAt = new Date().toISOString()
    await writeFile(reportPath, `${JSON.stringify(baseReport, null, 2)}\n`, 'utf8')
    return baseReport
  } catch (error) {
    baseReport.endedAt = new Date().toISOString()
    if (!diagnostics.some((diagnostic) => diagnostic.code === 'FFMPEG_FAILED')) {
      diagnostics.push({
        level: 'error',
        code: 'FFMPEG_FAILED',
        message: error instanceof Error ? error.message : String(error),
      })
    }
    await mkdir(dirname(reportPath), { recursive: true })
    await writeFile(reportPath, `${JSON.stringify(baseReport, null, 2)}\n`, 'utf8')
    throw error
  }
}
