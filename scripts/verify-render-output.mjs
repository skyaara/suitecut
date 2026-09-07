/* eslint-disable @typescript-eslint/no-unsafe-argument, @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return */
import { spawnSync } from 'node:child_process'
import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

const EXPECTED_VIDEO = {
  codec_name: 'h264',
  color_primaries: 'bt709',
  color_range: 'tv',
  color_space: 'bt709',
  color_transfer: 'bt709',
  pix_fmt: 'yuv420p',
}

const ManifestSchema = z.object({
  tests: z.array(
    z.object({
      id: z.string(),
      attempts: z.array(
        z.object({
          id: z.string(),
          events: z.array(
            z.object({
              id: z.string(),
              type: z.string(),
              atMs: z.number(),
              pageId: z.string(),
              text: z.string().optional(),
              caption: z.string().optional(),
            }),
          ),
          artifacts: z.array(
            z.object({
              id: z.string(),
              role: z.string(),
              path: z.string(),
              pathKind: z.enum(['absolute', 'relative']),
              pageId: z.string().optional(),
              sourceEventId: z.string().optional(),
            }),
          ),
          media: z.array(
            z.object({
              artifactId: z.string(),
              durationMs: z.number(),
              streams: z.array(
                z.object({
                  kind: z.string(),
                  width: z.number().int().positive().optional(),
                  height: z.number().int().positive().optional(),
                }),
              ),
            }),
          ),
          videoTiming: z.array(z.object({ pageId: z.string(), sourceStartedAtMs: z.number() })),
        }),
      ),
    }),
  ),
})

const RenderReportSchema = z.object({
  testId: z.string(),
  attemptId: z.string(),
  edits: z.array(
    z.object({
      kind: z.enum(['play', 'hold']),
      pageId: z.string(),
      executionStartMs: z.number(),
      executionEndMs: z.number(),
      presentationStartMs: z.number(),
      presentationEndMs: z.number(),
    }),
  ),
})

const ProbeSchema = z.object({
  streams: z.array(
    z.object({
      codec_name: z.string(),
      pix_fmt: z.string(),
      color_range: z.string(),
      color_space: z.string(),
      color_transfer: z.string(),
      color_primaries: z.string(),
      r_frame_rate: z.string(),
      avg_frame_rate: z.string(),
    }),
  ),
})

function parseArguments(argv) {
  const values = { minimumCaptionSsim: 0.94, minimumFrameSsim: 0.97, samples: 3 }
  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index]
    const value = argv[index + 1]
    if (argument === '--manifest' && value !== undefined) values.manifestPath = value
    else if (argument === '--output' && value !== undefined) values.outputPath = value
    else if (argument === '--minimum-frame-ssim' && value !== undefined) {
      values.minimumFrameSsim = Number(value)
    } else if (argument === '--minimum-caption-ssim' && value !== undefined) {
      values.minimumCaptionSsim = Number(value)
    } else if (argument === '--samples' && value !== undefined) values.samples = Number(value)
    else throw new Error(`Unknown or incomplete argument: ${argument}`)
    index += 1
  }
  if (values.manifestPath === undefined || values.outputPath === undefined) {
    throw new Error(
      'Usage: verify-render-output --manifest <manifest.json> --output <video.mp4> [--samples 3]',
    )
  }
  if (!Number.isInteger(values.samples) || values.samples < 1) {
    throw new Error('--samples must be a positive integer')
  }
  for (const [name, value] of [
    ['--minimum-frame-ssim', values.minimumFrameSsim],
    ['--minimum-caption-ssim', values.minimumCaptionSsim],
  ]) {
    if (!Number.isFinite(value) || value < 0 || value > 1) {
      throw new Error(`${name} must be between 0 and 1`)
    }
  }
  return values
}

function run(executable, args) {
  const result = spawnSync(executable, args, {
    encoding: 'utf8',
    maxBuffer: 16 * 1024 * 1024,
  })
  if (result.error !== undefined) throw result.error
  if (result.status !== 0) {
    throw new Error(
      `${executable} failed with exit code ${String(result.status)}: ${(result.stderr || result.stdout).trim()}`,
    )
  }
  return { stderr: result.stderr, stdout: result.stdout }
}

function ffprobePath(ffmpegPath) {
  if (process.env.SUITECUT_FFPROBE_PATH !== undefined) return process.env.SUITECUT_FFPROBE_PATH
  if (ffmpegPath === 'ffmpeg') return 'ffprobe'
  return ffmpegPath.replace(/ffmpeg(?=[^/]*$)/u, 'ffprobe')
}

function assertVideoMetadata(stream) {
  for (const [field, expected] of Object.entries(EXPECTED_VIDEO)) {
    if (stream[field] !== expected) {
      throw new Error(`Expected ${field}=${expected}, received ${String(stream[field])}`)
    }
  }
  if (stream.r_frame_rate !== '60/1' || stream.avg_frame_rate !== '60/1') {
    throw new Error(
      `Expected constant 60 fps, received r=${stream.r_frame_rate} avg=${stream.avg_frame_rate}`,
    )
  }
}

function selectedAttempt(manifest, report) {
  const test = manifest.tests.find((candidate) => candidate.id === report.testId)
  const attempt = test?.attempts.find((candidate) => candidate.id === report.attemptId)
  if (attempt === undefined)
    throw new Error('The render report attempt is missing from the manifest')
  return attempt
}

function presentationAt(edits, executionAtMs, pageId) {
  const edit = edits.find(
    (candidate) =>
      candidate.pageId === pageId &&
      executionAtMs >= candidate.executionStartMs &&
      executionAtMs <= candidate.executionEndMs,
  )
  if (edit === undefined) return undefined
  if (edit.kind === 'hold') return edit.presentationStartMs
  return edit.presentationStartMs + executionAtMs - edit.executionStartMs
}

function samplePoints(attempt, report, maximum) {
  const narrationSamples = attempt.events.flatMap((event) => {
    if (event.type !== 'narration') return []
    const artifact = attempt.artifacts.find(
      (candidate) => candidate.role === 'narration-audio' && candidate.sourceEventId === event.id,
    )
    const media = attempt.media.find((candidate) => candidate.artifactId === artifact?.id)
    const durationMs = media?.durationMs ?? 1_000
    const executionAtMs = event.atMs + Math.min(1_000, durationMs / 2)
    const presentationAtMs = presentationAt(report.edits, executionAtMs, event.pageId)
    return presentationAtMs === undefined
      ? []
      : [
          {
            caption: event.caption ?? event.text ?? '',
            executionAtMs,
            pageId: event.pageId,
            presentationAtMs,
          },
        ]
  })
  if (narrationSamples.length === 0) {
    throw new Error('The selected attempt has no rendered narration frames to compare')
  }
  if (narrationSamples.length <= maximum) return narrationSamples
  return Array.from({ length: maximum }, (_, index) => {
    const sampleIndex = Math.round((index * (narrationSamples.length - 1)) / (maximum - 1 || 1))
    return narrationSamples[sampleIndex]
  })
}

function sourceForSample(attempt, sample, manifestPath) {
  const timing = attempt.videoTiming.find((candidate) => candidate.pageId === sample.pageId)
  const artifact = attempt.artifacts.find(
    (candidate) => candidate.role === 'source-video' && candidate.pageId === sample.pageId,
  )
  const media = attempt.media.find((candidate) => candidate.artifactId === artifact?.id)
  const video = media?.streams.find((candidate) => candidate.kind === 'video')
  if (
    timing === undefined ||
    artifact === undefined ||
    video === undefined ||
    video.width === undefined ||
    video.height === undefined
  ) {
    throw new Error(`Source video metadata is incomplete for page ${sample.pageId}`)
  }
  return {
    height: video.height,
    path:
      artifact.pathKind === 'absolute'
        ? artifact.path
        : resolve(dirname(manifestPath), artifact.path),
    sourceAtMs: Math.max(0, sample.executionAtMs - timing.sourceStartedAtMs),
    width: video.width,
  }
}

function extractFrame(ffmpegPath, inputPath, atMs, width, height, outputPath) {
  run(ffmpegPath, [
    '-hide_banner',
    '-loglevel',
    'error',
    '-y',
    '-ss',
    (atMs / 1_000).toFixed(6),
    '-i',
    inputPath,
    '-frames:v',
    '1',
    '-vf',
    `scale=${width}:${height}:flags=lanczos,format=rgb24`,
    outputPath,
  ])
}

function ssim(ffmpegPath, sourcePath, outputPath, filter = 'ssim') {
  const result = run(ffmpegPath, [
    '-hide_banner',
    '-i',
    sourcePath,
    '-i',
    outputPath,
    '-lavfi',
    filter,
    '-f',
    'null',
    '-',
  ])
  const match = /All:([0-9.]+)/u.exec(result.stderr)
  if (match === null) throw new Error('FFmpeg did not report an SSIM value')
  return Number(match[1])
}

const options = parseArguments(process.argv.slice(2))
const manifestPath = resolve(options.manifestPath)
const outputPath = resolve(options.outputPath)
const reportPath = `${outputPath}.suitecut.json`
const [manifestInput, reportInput] = await Promise.all(
  [manifestPath, reportPath].map(async (path) => JSON.parse(await readFile(path, 'utf8'))),
)
const manifest = ManifestSchema.parse(manifestInput)
const report = RenderReportSchema.parse(reportInput)
const ffmpeg = process.env.SUITECUT_FFMPEG_PATH ?? 'ffmpeg'
const probe = ProbeSchema.parse(
  JSON.parse(
    run(ffprobePath(ffmpeg), [
      '-v',
      'error',
      '-select_streams',
      'v:0',
      '-show_entries',
      'stream=codec_name,pix_fmt,color_range,color_space,color_transfer,color_primaries,r_frame_rate,avg_frame_rate',
      '-of',
      'json',
      outputPath,
    ]).stdout,
  ),
)
const videoStream = probe.streams?.[0]
if (videoStream === undefined) throw new Error('FFprobe found no video stream')
assertVideoMetadata(videoStream)

const attempt = selectedAttempt(manifest, report)
const samples = samplePoints(attempt, report, options.samples)
const temporaryDirectory = await mkdtemp(join(tmpdir(), 'suitecut-render-verify-'))
const comparisons = []
try {
  for (const [index, sample] of samples.entries()) {
    const source = sourceForSample(attempt, sample, manifestPath)
    const sourceFrame = join(temporaryDirectory, `source-${index}.png`)
    const outputFrame = join(temporaryDirectory, `output-${index}.png`)
    extractFrame(ffmpeg, source.path, source.sourceAtMs, source.width, source.height, sourceFrame)
    extractFrame(
      ffmpeg,
      outputPath,
      sample.presentationAtMs,
      source.width,
      source.height,
      outputFrame,
    )
    const frameSsim = ssim(ffmpeg, sourceFrame, outputFrame)
    const captionHeight = Math.max(1, Math.round(source.height * 0.25))
    const captionSsim = ssim(
      ffmpeg,
      sourceFrame,
      outputFrame,
      `[0:v]crop=iw:${captionHeight}:0:ih-${captionHeight}[source];[1:v]crop=iw:${captionHeight}:0:ih-${captionHeight}[output];[source][output]ssim`,
    )
    if (frameSsim < options.minimumFrameSsim) {
      throw new Error(
        `Frame ${index + 1} SSIM ${frameSsim.toFixed(6)} is below ${options.minimumFrameSsim}`,
      )
    }
    if (captionSsim < options.minimumCaptionSsim) {
      throw new Error(
        `Caption ${index + 1} SSIM ${captionSsim.toFixed(6)} is below ${options.minimumCaptionSsim}`,
      )
    }
    comparisons.push({
      caption: sample.caption,
      captionSsim,
      frameSsim,
      outputAtMs: Math.round(sample.presentationAtMs),
      sourceAtMs: Math.round(source.sourceAtMs),
    })
  }
} finally {
  await rm(temporaryDirectory, { recursive: true })
}

process.stdout.write(
  JSON.stringify(
    {
      comparisons,
      output: outputPath,
      video: videoStream,
    },
    null,
    2,
  ),
)
process.stdout.write('\n')
