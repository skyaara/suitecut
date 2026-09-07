import { randomUUID } from 'node:crypto'

import * as z from 'zod'

import { resolveFfprobe, runProcess } from './process.js'
import { type SuiteCutArtifact, type SuiteCutMedia, type SuiteCutMediaStream } from './types.js'
import { type UntrustedInput } from './untrusted.js'

const FiniteProbeNumberSchema = z
  .union([z.number(), z.string().trim().min(1)])
  .transform((value, context) => {
    const parsed = typeof value === 'number' ? value : Number(value)
    if (!Number.isFinite(parsed)) {
      context.addIssue({ code: 'custom', message: 'must be a finite number' })
      return z.NEVER
    }
    return parsed
  })

const NonNegativeProbeNumberSchema = FiniteProbeNumberSchema.pipe(z.number().nonnegative())
const PositiveProbeNumberSchema = FiniteProbeNumberSchema.pipe(z.number().positive())

const FrameRateSchema = z
  .string()
  .regex(/^\d+(?:\.\d+)?\/\d+(?:\.\d+)?$/u)
  .transform((value, context) => {
    const [numeratorText, denominatorText] = value.split('/')
    const numerator = Number(numeratorText)
    const denominator = Number(denominatorText)
    if (!Number.isFinite(numerator) || !Number.isFinite(denominator) || denominator <= 0) {
      context.addIssue({ code: 'custom', message: 'must have a positive denominator' })
      return z.NEVER
    }
    return numerator / denominator
  })

const ProbeFormatSchema = z.object({
  format_name: z.string().trim().min(1),
  duration: NonNegativeProbeNumberSchema,
})

const JsonValueSchema = z.json()
type JsonValue = z.infer<typeof JsonValueSchema>

const ProbeDocumentSchema = z.object({
  streams: z.array(z.record(z.string(), JsonValueSchema)).min(1),
  format: ProbeFormatSchema,
})

const ProbeVideoStreamSchema = z.object({
  codec_type: z.literal('video'),
  codec_name: z.string().trim().min(1),
  pix_fmt: z.string().trim().min(1).optional(),
  color_range: z.string().trim().min(1).optional(),
  color_space: z.string().trim().min(1).optional(),
  color_transfer: z.string().trim().min(1).optional(),
  color_primaries: z.string().trim().min(1).optional(),
  width: PositiveProbeNumberSchema,
  height: PositiveProbeNumberSchema,
  duration: NonNegativeProbeNumberSchema.optional(),
  avg_frame_rate: FrameRateSchema,
  r_frame_rate: FrameRateSchema,
  time_base: z
    .string()
    .regex(/^\d+\/\d+$/u)
    .optional(),
})

const ProbeAudioStreamSchema = z.object({
  codec_type: z.literal('audio'),
  codec_name: z.string().trim().min(1),
  channels: PositiveProbeNumberSchema,
  sample_rate: PositiveProbeNumberSchema,
  duration: NonNegativeProbeNumberSchema.optional(),
})

function knownProbeValue(value: string | undefined): string | undefined {
  if (value === undefined || value === 'unknown' || value === 'unspecified') return undefined
  return value
}

function probeColorRange(value: string | undefined): 'full' | 'limited' | undefined {
  if (value === 'pc' || value === 'jpeg' || value === 'full') return 'full'
  if (value === 'tv' || value === 'mpeg' || value === 'limited') return 'limited'
  return undefined
}

function parseStream(
  stream: Record<string, JsonValue>,
  formatDurationMs: number,
): SuiteCutMediaStream | undefined {
  if (stream.codec_type === 'video') {
    const parsed = ProbeVideoStreamSchema.parse(stream)
    const colorRange = probeColorRange(parsed.color_range)
    const colorSpace = knownProbeValue(parsed.color_space)
    const colorTransfer = knownProbeValue(parsed.color_transfer)
    const colorPrimaries = knownProbeValue(parsed.color_primaries)
    return {
      kind: 'video',
      codec: parsed.codec_name,
      ...(parsed.pix_fmt === undefined ? {} : { pixelFormat: parsed.pix_fmt }),
      ...(colorRange === undefined ? {} : { colorRange }),
      ...(colorSpace === undefined ? {} : { colorSpace }),
      ...(colorTransfer === undefined ? {} : { colorTransfer }),
      ...(colorPrimaries === undefined ? {} : { colorPrimaries }),
      width: parsed.width,
      height: parsed.height,
      durationMs: (parsed.duration ?? formatDurationMs / 1_000) * 1_000,
      frameRate: parsed.avg_frame_rate,
      ...(parsed.time_base === undefined ? {} : { timeBase: parsed.time_base }),
      hasVariableFrameRate: parsed.avg_frame_rate !== parsed.r_frame_rate,
    }
  }
  if (stream.codec_type === 'audio') {
    const parsed = ProbeAudioStreamSchema.parse(stream)
    return {
      kind: 'audio',
      codec: parsed.codec_name,
      channels: parsed.channels,
      sampleRate: parsed.sample_rate,
      durationMs: (parsed.duration ?? formatDurationMs / 1_000) * 1_000,
    }
  }
  return undefined
}

/** Decodes the JSON document emitted by FFprobe. */
export function decodeProbeDocument(input: UntrustedInput): z.infer<typeof ProbeDocumentSchema> {
  return ProbeDocumentSchema.parse(input)
}

async function readProbeDocument(
  absolutePath: string,
  ffmpegPath?: string,
): Promise<z.infer<typeof ProbeDocumentSchema>> {
  const executable = await resolveFfprobe(ffmpegPath)
  const result = await runProcess(
    executable,
    ['-v', 'error', '-show_format', '-show_streams', '-of', 'json', absolutePath],
    { timeoutMs: 30_000 },
  )
  if (result.exitCode !== 0) {
    throw new Error(`FFprobe failed for ${absolutePath}: ${result.stderr.trim()}`)
  }

  try {
    return decodeProbeDocument(JSON.parse(result.stdout) as UntrustedInput)
  } catch (error) {
    throw new Error(`FFprobe returned invalid metadata for ${absolutePath}`, { cause: error })
  }
}

/** Reads the measured container duration without constructing manifest media records. */
export async function probeMediaDurationMs(
  absolutePath: string,
  ffmpegPath?: string,
): Promise<number> {
  return (await readProbeDocument(absolutePath, ffmpegPath)).format.duration * 1_000
}

export async function probeMedia(
  artifact: SuiteCutArtifact,
  absolutePath: string,
  ffmpegPath?: string,
): Promise<SuiteCutMedia> {
  const document = await readProbeDocument(absolutePath, ffmpegPath)

  const durationMs = document.format.duration * 1_000
  const streams = document.streams
    .map((stream) => parseStream(stream, durationMs))
    .filter((stream): stream is SuiteCutMediaStream => stream !== undefined)
  if (streams.length === 0) throw new Error(`FFprobe found no supported streams in ${absolutePath}`)

  return {
    id: `media-${randomUUID()}`,
    artifactId: artifact.id,
    formatName: document.format.format_name,
    durationMs,
    streams,
  }
}
