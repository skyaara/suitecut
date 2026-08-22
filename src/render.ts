import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { dirname, extname, resolve } from 'node:path'

import { chromium } from '@playwright/test'
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

const OutputSchema = z.strictObject({
  format: z.enum(['mp4', 'webm']).exactOptional(),
  width: z.number().int().positive().max(7680).exactOptional(),
  height: z.number().int().positive().max(4320).exactOptional(),
  framesPerSecond: z.union([z.literal(30), z.literal(60)]).exactOptional(),
  quality: z.number().int().min(0).max(63).exactOptional(),
})

const RenderConfigSchema = z.strictObject({
  output: OutputSchema.exactOptional(),
  narrationEnabled: z.boolean().exactOptional(),
  captionsEnabled: z.boolean().exactOptional(),
  narrationTailMs: z.number().nonnegative().exactOptional(),
  resultHoldMs: z.number().nonnegative().exactOptional(),
  backgroundColor: z.string().exactOptional(),
  ffmpegPath: z.string().min(1).exactOptional(),
  failureMode: z.enum(['strict', 'best-effort']).exactOptional(),
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
export type SuiteCutOutputFormat = 'mp4' | 'webm'
/** Controls whether FFmpeg diagnostics stop the render. */
export type SuiteCutRenderFailureMode = 'strict' | 'best-effort'

/** Video, audio, caption, and failure settings for one render. */
export interface SuiteCutRenderConfig {
  output?: {
    format?: SuiteCutOutputFormat
    width?: number
    height?: number
    framesPerSecond?: 30 | 60
    quality?: number
  }
  narrationEnabled?: boolean
  captionsEnabled?: boolean
  narrationTailMs?: number
  resultHoldMs?: number
  backgroundColor?: string
  ffmpegPath?: string
  failureMode?: SuiteCutRenderFailureMode
}

/** Selects a recorded attempt and names its rendered output file. */
export interface SuiteCutRenderRequest {
  manifestPath: string
  outputPath: string
  selection?: { testId?: string; retry?: number }
  config?: SuiteCutRenderConfig
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

interface SequenceItem {
  kind: 'play' | 'hold'
  pageId: SuiteCutPageId
  executionStartMs: number
  executionEndMs: number
  durationMs: number
}

interface CaptionAsset {
  path: string
  startMs: number
  endMs: number
}

interface NarrationPlacement {
  eventId: string
  pageId: SuiteCutPageId
  executionAtMs: number
  startMs: number
  durationMs: number
  holdMs: number
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

function presentationTime(
  atMs: number,
  attempt: SuiteCutAttempt,
  narrationPlacements: readonly NarrationPlacement[] = [],
): number {
  let holdTimeMs = 0
  for (const event of attempt.events) {
    if (event.type === 'hold' && event.atMs < atMs) holdTimeMs += event.durationMs
  }
  for (const placement of narrationPlacements) {
    if (placement.executionAtMs < atMs) holdTimeMs += placement.holdMs
  }
  return atMs + holdTimeMs
}

function buildNarrationPlacements(
  attempt: SuiteCutAttempt,
  narrationArtifacts: readonly SuiteCutArtifact[],
  narrationTailMs: number,
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
      pageId: event.pageId,
      executionAtMs: event.atMs,
      startMs: presentationTime(event.atMs, attempt, placements),
      durationMs: media.durationMs,
      holdMs: media.durationMs + narrationTailMs,
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

function buildSequence(
  attempt: SuiteCutAttempt,
  narrationPlacements: readonly NarrationPlacement[],
  extraTailMs: number,
): SequenceItem[] {
  const boundarySet = new Set<number>([0, attempt.durationMs])
  for (const event of attempt.events) {
    if (event.atMs >= 0 && event.atMs <= attempt.durationMs) boundarySet.add(event.atMs)
    if (event.type === 'zoom') {
      boundarySet.add(Math.min(attempt.durationMs, event.atMs + (event.options.holdMs ?? 900)))
    }
  }
  const boundaries = [...boundarySet].sort((left, right) => left - right)
  const sequence: SequenceItem[] = []
  for (const placement of narrationPlacements) {
    if (placement.executionAtMs === 0) {
      sequence.push({
        kind: 'hold',
        pageId: placement.pageId,
        executionStartMs: 0,
        executionEndMs: 0,
        durationMs: placement.holdMs,
      })
    }
  }
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
    for (const event of attempt.events) {
      if (event.type === 'hold' && event.atMs === end) {
        sequence.push({
          kind: 'hold',
          pageId: event.pageId,
          executionStartMs: end,
          executionEndMs: end,
          durationMs: event.durationMs,
        })
      }
    }
    for (const placement of narrationPlacements) {
      if (placement.executionAtMs === end) {
        sequence.push({
          kind: 'hold',
          pageId: placement.pageId,
          executionStartMs: end,
          executionEndMs: end,
          durationMs: placement.holdMs,
        })
      }
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

function escapeHtml(value: string): string {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#39;')
}

async function createCaptionAssets(
  outputPath: string,
  width: number,
  attempt: SuiteCutAttempt,
  narrationPlacements: readonly NarrationPlacement[],
): Promise<CaptionAsset[]> {
  if (narrationPlacements.length === 0) return []
  const assetDirectory = `${outputPath}.assets`
  await mkdir(assetDirectory, { recursive: true })
  const browser = await chromium.launch({ headless: true })
  try {
    const page = await browser.newPage({
      viewport: { width: Math.min(width - 80, 1_000), height: 180 },
    })
    const assets: CaptionAsset[] = []
    for (const [index, placement] of narrationPlacements.entries()) {
      const event = attempt.events.find(
        (candidate) => candidate.type === 'narration' && candidate.id === placement.eventId,
      )
      if (event?.type !== 'narration') continue
      const text = event.caption ?? event.text
      if (text.length === 0) continue
      await page.setContent(`
        <style>
          html, body { margin: 0; background: transparent; }
          body { display: grid; place-items: center; min-height: 180px; }
          #caption {
            max-width: 920px; padding: 13px 20px; border: 1px solid #ffffff22;
            border-radius: 13px; color: white; background: #020617dd;
            box-shadow: 0 10px 36px #00000077; font: 650 24px/1.35 ui-sans-serif, system-ui, sans-serif;
            text-align: center;
          }
        </style>
        <div id="caption">${escapeHtml(text)}</div>
      `)
      const path = resolve(assetDirectory, `caption-${index + 1}.png`)
      await page.locator('#caption').screenshot({ path, type: 'png' })
      assets.push({
        path,
        startMs: placement.startMs,
        endMs: placement.startMs + placement.durationMs,
      })
    }
    return assets
  } finally {
    await browser.close()
  }
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
    const width = request.config?.output?.width ?? 1280
    const height = request.config?.output?.height ?? 720
    const fps = request.config?.output?.framesPerSecond ?? 30
    const resultHoldMs = request.config?.resultHoldMs ?? 500
    const narrationTailMs = request.config?.narrationTailMs ?? 350
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

    const narrationPlacements =
      request.config?.narrationEnabled === false && request.config?.captionsEnabled === false
        ? []
        : buildNarrationPlacements(
            attempt,
            allNarrationArtifacts.map(({ artifact }) => artifact),
            narrationTailMs,
          )
    const sequence = buildSequence(attempt, narrationPlacements, resultHoldMs)
    const editMap = edits(sequence)
    const presentationDurationMs = editMap.at(-1)?.presentationEndMs ?? 0
    const captionAssets =
      request.config?.captionsEnabled === false
        ? []
        : await createCaptionAssets(outputPath, width, attempt, narrationPlacements)

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
      visual += `,scale=${width}:${height}:force_original_aspect_ratio=decrease,pad=${width}:${height}:(ow-iw)/2:(oh-ih)/2:${background},setsar=1,fps=${fps},format=yuv420p`
      filters.push(`[${inputIndex}:v]${visual}[${label}]`)
      videoLabels.push(`[${label}]`)
    }
    filters.push(`${videoLabels.join('')}concat=n=${videoLabels.length}:v=1:a=0[video0]`)

    let currentVideo = 'video0'
    let overlayIndex = 0
    const addDrawBox = (
      x: number,
      y: number,
      boxWidth: number,
      boxHeight: number,
      boxColor: string,
      thickness: number,
      startMs: number,
      endMs: number,
    ): void => {
      const next = `video${++overlayIndex}`
      filters.push(
        `[${currentVideo}]drawbox=x=${x.toFixed(2)}:y=${y.toFixed(2)}:w=${boxWidth.toFixed(2)}:h=${boxHeight.toFixed(2)}:color=${boxColor}:t=${thickness}:enable='between(t,${seconds(startMs)},${seconds(endMs)})'[${next}]`,
      )
      currentVideo = next
    }

    const visualEnd = (
      event: SuiteCutAttempt['events'][number],
      preferredEndMs: number,
    ): number => {
      const nextPageEvent = attempt.events.find(
        (candidate) => candidate.atMs > event.atMs && candidate.pageId !== event.pageId,
      )
      return nextPageEvent === undefined
        ? preferredEndMs
        : Math.min(
            preferredEndMs,
            presentationTime(nextPageEvent.atMs, attempt, narrationPlacements),
          )
    }

    for (const event of attempt.events) {
      const atMs = presentationTime(event.atMs, attempt, narrationPlacements)
      if (event.type === 'highlight') {
        const xScale = width / event.viewport.width
        const yScale = height / event.viewport.height
        const padding = event.options.paddingPx ?? 8
        addDrawBox(
          (event.rect.x - padding) * xScale,
          (event.rect.y - padding) * yScale,
          (event.rect.width + padding * 2) * xScale,
          (event.rect.height + padding * 2) * yScale,
          color(event.options.borderColor ?? '#7C3AED'),
          event.options.borderWidthPx ?? 4,
          atMs,
          visualEnd(event, atMs + (event.options.durationMs ?? 1_200)),
        )
      }
      if (
        event.type === 'pointer-move' ||
        event.type === 'pointer-down' ||
        event.type === 'pointer-up' ||
        event.type === 'click'
      ) {
        const x = (event.point.x * width) / event.viewport.width
        const y = (event.point.y * height) / event.viewport.height
        addDrawBox(
          x - 5,
          y - 5,
          10,
          10,
          color('#FFFFFF', 0.9),
          -1,
          atMs,
          visualEnd(event, atMs + 550),
        )
        if (event.type === 'click') {
          addDrawBox(
            x - 12,
            y - 12,
            24,
            24,
            color('#FACC15', 0.75),
            3,
            atMs,
            visualEnd(event, atMs + 250),
          )
        }
      }
    }

    for (const [captionIndex, caption] of captionAssets.entries()) {
      const inputIndex = sourceArtifacts.length + narrationArtifacts.length + captionIndex
      const captionLabel = `caption${captionIndex}`
      const next = `video${++overlayIndex}`
      filters.push(`[${inputIndex}:v]format=rgba[${captionLabel}]`)
      filters.push(
        `[${currentVideo}][${captionLabel}]overlay=x=(W-w)/2:y=H-h-42:enable='between(t,${seconds(caption.startMs)},${seconds(caption.endMs)})'[${next}]`,
      )
      currentVideo = next
    }

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
    for (const caption of captionAssets)
      args.push('-loop', '1', '-framerate', String(fps), '-i', caption.path)
    args.push('-filter_complex', filters.join(';'), '-map', `[${currentVideo}]`)
    if (audioLabels.length > 0) args.push('-map', '[audioout]')
    args.push('-r', String(fps), '-t', seconds(presentationDurationMs))
    if (format === 'mp4') {
      args.push(
        '-c:v',
        'libx264',
        '-crf',
        String(request.config?.output?.quality ?? 22),
        '-preset',
        'medium',
        '-pix_fmt',
        'yuv420p',
        '-movflags',
        '+faststart',
      )
      if (audioLabels.length > 0) args.push('-c:a', 'aac', '-b:a', '160k')
    } else {
      args.push(
        '-c:v',
        'libvpx-vp9',
        '-crf',
        String(request.config?.output?.quality ?? 32),
        '-b:v',
        '0',
        '-pix_fmt',
        'yuv420p',
      )
      if (audioLabels.length > 0) args.push('-c:a', 'libopus', '-b:a', '128k')
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
