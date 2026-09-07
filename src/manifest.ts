import * as z from 'zod'

import { SUITECUT_MANIFEST_SCHEMA_VERSION } from './constants.js'
import {
  SuiteCutHighlightOptionsSchema,
  SuiteCutNarrationProviderSchema,
  SuiteCutPathKindSchema,
  SuiteCutPointSchema,
  SuiteCutRectSchema,
  SuiteCutStepCategorySchema,
  SuiteCutViewportSchema,
  SuiteCutZoomOptionsSchema,
} from './schemas.js'
import { type SuiteCutEventAttachment, type SuiteCutManifest } from './types.js'
import { type UntrustedInput } from './untrusted.js'

const NonEmptyStringSchema = z
  .string()
  .refine((value) => value.trim().length > 0, { message: 'must not be empty' })
const FiniteNumberSchema = z.number()
const NonNegativeNumberSchema = FiniteNumberSchema.nonnegative()
const PositiveNumberSchema = FiniteNumberSchema.positive()
const NonNegativeIntegerSchema = z.number().int().nonnegative()
const PositiveIntegerSchema = z.number().int().positive()
const ISODateTimeSchema = z.iso.datetime({ offset: true })

const SourceLocationSchema = z.strictObject({
  file: NonEmptyStringSchema,
  line: PositiveIntegerSchema,
  column: PositiveIntegerSchema,
})

const ErrorSchema = z.strictObject({
  message: NonEmptyStringSchema,
  name: NonEmptyStringSchema.exactOptional(),
  stack: z.string().exactOptional(),
  location: SourceLocationSchema.exactOptional(),
})

const ClockSchema = z.strictObject({
  originEpochMs: NonNegativeNumberSchema,
  originMonotonicMs: NonNegativeNumberSchema,
  startedAt: ISODateTimeSchema,
})

const PageSchema = z
  .strictObject({
    id: NonEmptyStringSchema,
    kind: z.enum(['main', 'popup', 'secondary']),
    openerPageId: NonEmptyStringSchema.exactOptional(),
    initialUrl: z.string(),
    createdAtMs: NonNegativeNumberSchema,
    closedAtMs: NonNegativeNumberSchema.exactOptional(),
  })
  .superRefine((page, context) => {
    if (page.closedAtMs !== undefined && page.closedAtMs < page.createdAtMs) {
      context.addIssue({
        code: 'custom',
        message: 'must not precede page creation',
        path: ['closedAtMs'],
      })
    }
  })

const EventBase = {
  id: NonEmptyStringSchema,
  atMs: NonNegativeNumberSchema,
  pageId: NonEmptyStringSchema,
}

const PageSelectionEventSchema = z.strictObject({
  ...EventBase,
  type: z.literal('page-selected'),
  reason: z.enum(['opened', 'closed', 'author', 'interaction']),
})

const NarrationEventSchema = z.strictObject({
  ...EventBase,
  type: z.literal('narration'),
  text: NonEmptyStringSchema,
  provider: SuiteCutNarrationProviderSchema.exactOptional(),
  voice: NonEmptyStringSchema.exactOptional(),
  speed: z.number().min(0.5).max(2).exactOptional(),
  caption: z.string().exactOptional(),
})

const CheckpointEventSchema = z.strictObject({
  ...EventBase,
  type: z.literal('checkpoint'),
  label: NonEmptyStringSchema,
  artifactId: NonEmptyStringSchema,
  durationMs: PositiveNumberSchema,
  viewport: SuiteCutViewportSchema,
})

const PointerBase = {
  ...EventBase,
  point: SuiteCutPointSchema,
  pointerType: z.enum(['mouse', 'pen', 'touch']),
  viewport: SuiteCutViewportSchema,
  targetRect: SuiteCutRectSchema.exactOptional(),
}

const PointerMoveEventSchema = z.strictObject({
  ...PointerBase,
  type: z.literal('pointer-move'),
})

const PointerButtonEventSchema = z.strictObject({
  ...PointerBase,
  type: z.enum(['pointer-down', 'pointer-up', 'click']),
  button: z.enum(['none', 'left', 'middle', 'right', 'back', 'forward']),
})

const HighlightEventSchema = z.strictObject({
  ...EventBase,
  type: z.literal('highlight'),
  rect: SuiteCutRectSchema,
  viewport: SuiteCutViewportSchema,
  options: SuiteCutHighlightOptionsSchema,
})

const ZoomEventSchema = z.strictObject({
  ...EventBase,
  type: z.literal('zoom'),
  rect: SuiteCutRectSchema,
  viewport: SuiteCutViewportSchema,
  options: SuiteCutZoomOptionsSchema,
})

const HoldEventSchema = z.strictObject({
  ...EventBase,
  type: z.literal('hold'),
  durationMs: PositiveNumberSchema,
  reason: z.enum(['author', 'narration', 'checkpoint']),
})

const EventSchema = z.discriminatedUnion('type', [
  PageSelectionEventSchema,
  NarrationEventSchema,
  CheckpointEventSchema,
  PointerMoveEventSchema,
  PointerButtonEventSchema,
  HighlightEventSchema,
  ZoomEventSchema,
  HoldEventSchema,
])

const CapturedCheckpointArtifactSchema = z.strictObject({
  id: NonEmptyStringSchema,
  attachmentName: NonEmptyStringSchema,
  role: z.literal('checkpoint'),
  contentType: z.literal('video/webm'),
  capturedAtMs: NonNegativeNumberSchema,
  durationMs: PositiveNumberSchema,
  pageId: NonEmptyStringSchema,
})

const CapturedNarrationArtifactSchema = z.strictObject({
  id: NonEmptyStringSchema,
  attachmentName: NonEmptyStringSchema,
  role: z.literal('narration-audio'),
  contentType: NonEmptyStringSchema,
  createdAtMs: NonNegativeNumberSchema,
  sourceEventId: NonEmptyStringSchema,
  provider: NonEmptyStringSchema,
  voice: NonEmptyStringSchema,
})

const CapturedCaptionArtifactSchema = z.strictObject({
  id: NonEmptyStringSchema,
  attachmentName: NonEmptyStringSchema,
  role: z.literal('captions'),
  contentType: z.literal('application/json'),
  createdAtMs: NonNegativeNumberSchema,
  sourceEventId: NonEmptyStringSchema,
})

const CapturedArtifactSchema = z.discriminatedUnion('role', [
  CapturedCheckpointArtifactSchema,
  CapturedNarrationArtifactSchema,
  CapturedCaptionArtifactSchema,
])

const CapturedVideoSchema = z.strictObject({
  artifactId: NonEmptyStringSchema,
  pageId: NonEmptyStringSchema,
  attachmentName: NonEmptyStringSchema,
  firstFrameEpochMs: NonNegativeNumberSchema,
  sourceStartedAtMs: FiniteNumberSchema,
})

const EventAttachmentSchema = z
  .strictObject({
    attemptId: NonEmptyStringSchema,
    clock: ClockSchema,
    endedAtMs: NonNegativeNumberSchema,
    pages: z.array(PageSchema),
    events: z.array(EventSchema),
    artifacts: z.array(CapturedArtifactSchema),
    videos: z.array(CapturedVideoSchema),
  })
  .superRefine((attachment, context) => {
    const pageIds = new Set(attachment.pages.map((page) => page.id))
    const eventIds = new Set(attachment.events.map((event) => event.id))
    const artifactById = new Map(attachment.artifacts.map((artifact) => [artifact.id, artifact]))

    for (const [index, event] of attachment.events.entries()) {
      if (!pageIds.has(event.pageId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown event page',
          path: ['events', index, 'pageId'],
        })
      }
      if (event.type === 'checkpoint') {
        const artifact = artifactById.get(event.artifactId)
        if (artifact?.role !== 'checkpoint') {
          context.addIssue({
            code: 'custom',
            message: 'unknown checkpoint artifact',
            path: ['events', index, 'artifactId'],
          })
        } else if (
          artifact.pageId !== event.pageId ||
          artifact.capturedAtMs !== event.atMs ||
          artifact.durationMs !== event.durationMs
        ) {
          context.addIssue({
            code: 'custom',
            message: 'checkpoint event does not match its video artifact',
            path: ['events', index, 'artifactId'],
          })
        }
      }
    }
    for (const [index, artifact] of attachment.artifacts.entries()) {
      if (artifact.role === 'checkpoint' && !pageIds.has(artifact.pageId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown checkpoint page',
          path: ['artifacts', index, 'pageId'],
        })
      }
      if (
        (artifact.role === 'narration-audio' || artifact.role === 'captions') &&
        !eventIds.has(artifact.sourceEventId)
      ) {
        context.addIssue({
          code: 'custom',
          message: 'unknown source event',
          path: ['artifacts', index, 'sourceEventId'],
        })
      }
    }
    for (const [index, video] of attachment.videos.entries()) {
      if (!pageIds.has(video.pageId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown video page',
          path: ['videos', index, 'pageId'],
        })
      }
    }
  })

const ExecutionStepSchema = z.strictObject({
  id: NonEmptyStringSchema,
  parentStepId: NonEmptyStringSchema.exactOptional(),
  title: NonEmptyStringSchema,
  category: SuiteCutStepCategorySchema,
  atMs: FiniteNumberSchema,
  durationMs: NonNegativeNumberSchema,
  outcome: z.enum(['passed', 'failed', 'skipped', 'interrupted']),
  location: SourceLocationSchema.exactOptional(),
  error: ErrorSchema.exactOptional(),
})

const ArtifactSchema = z.strictObject({
  id: NonEmptyStringSchema,
  name: NonEmptyStringSchema,
  role: z.enum([
    'checkpoint',
    'source-video',
    'playwright-trace',
    'narration-audio',
    'captions',
    'rendered-video',
    'render-report',
    'other',
  ]),
  contentType: NonEmptyStringSchema,
  path: NonEmptyStringSchema,
  pathKind: SuiteCutPathKindSchema,
  sizeBytes: NonNegativeIntegerSchema.exactOptional(),
  pageId: NonEmptyStringSchema.exactOptional(),
  createdAtMs: FiniteNumberSchema.exactOptional(),
  sourceEventId: NonEmptyStringSchema.exactOptional(),
  provider: NonEmptyStringSchema.exactOptional(),
  voice: NonEmptyStringSchema.exactOptional(),
})

const VideoStreamSchema = z.strictObject({
  kind: z.literal('video'),
  codec: NonEmptyStringSchema,
  pixelFormat: NonEmptyStringSchema.exactOptional(),
  colorRange: z.enum(['full', 'limited']).exactOptional(),
  colorSpace: NonEmptyStringSchema.exactOptional(),
  colorTransfer: NonEmptyStringSchema.exactOptional(),
  colorPrimaries: NonEmptyStringSchema.exactOptional(),
  width: PositiveIntegerSchema,
  height: PositiveIntegerSchema,
  durationMs: NonNegativeNumberSchema,
  frameRate: PositiveNumberSchema,
  timeBase: NonEmptyStringSchema.exactOptional(),
  hasVariableFrameRate: z.boolean(),
})

const AudioStreamSchema = z.strictObject({
  kind: z.literal('audio'),
  codec: NonEmptyStringSchema,
  channels: PositiveIntegerSchema,
  sampleRate: PositiveIntegerSchema,
  durationMs: NonNegativeNumberSchema,
})

const MediaSchema = z.strictObject({
  id: NonEmptyStringSchema,
  artifactId: NonEmptyStringSchema,
  formatName: NonEmptyStringSchema,
  durationMs: NonNegativeNumberSchema,
  streams: z.array(z.discriminatedUnion('kind', [VideoStreamSchema, AudioStreamSchema])),
})

const VideoTimingSchema = z.strictObject({
  pageId: NonEmptyStringSchema,
  mediaId: NonEmptyStringSchema,
  firstFrameEpochMs: NonNegativeNumberSchema,
  sourceStartedAtMs: FiniteNumberSchema,
})

const DiagnosticSchema = z.strictObject({
  level: z.enum(['info', 'warning', 'error']),
  code: z.enum([
    'INVALID_EVENT',
    'MISSING_ATTACHMENT',
    'MISSING_SOURCE_VIDEO',
    'MEDIA_PROBE_FAILED',
    'SCREENCAST_START_FAILED',
    'FIRST_FRAME_TIMESTAMP_MISSING',
    'VIDEO_TIMING_INVALID',
    'POINTER_OUTSIDE_VIEWPORT',
    'LOCATOR_GEOMETRY_MISSING',
    'OPTIONAL_TRACK_OMITTED',
    'RENDER_FAILED',
    'FFMPEG_FAILED',
  ]),
  message: NonEmptyStringSchema,
  atMs: FiniteNumberSchema.exactOptional(),
  eventId: NonEmptyStringSchema.exactOptional(),
  artifactId: NonEmptyStringSchema.exactOptional(),
})

function duplicateIndex<T>(items: readonly T[], id: (item: T) => string): number | undefined {
  const seen = new Set<string>()
  for (const [index, item] of items.entries()) {
    const value = id(item)
    if (seen.has(value)) return index
    seen.add(value)
  }
  return undefined
}

function containsParentCycle<T>(
  items: readonly T[],
  id: (item: T) => string,
  parentId: (item: T) => string | undefined,
): boolean {
  const byId = new Map(items.map((item) => [id(item), item]))
  for (const item of items) {
    const visited = new Set<string>()
    let current: T | undefined = item
    while (current !== undefined) {
      const currentId = id(current)
      if (visited.has(currentId)) return true
      visited.add(currentId)
      const parent = parentId(current)
      current = parent === undefined ? undefined : byId.get(parent)
    }
  }
  return false
}

const AttemptSchema = z
  .strictObject({
    id: NonEmptyStringSchema,
    retry: NonNegativeIntegerSchema,
    status: z.enum(['passed', 'failed', 'timedOut', 'skipped', 'interrupted']),
    clock: ClockSchema,
    durationMs: NonNegativeNumberSchema,
    pages: z.array(PageSchema),
    events: z.array(EventSchema),
    steps: z.array(ExecutionStepSchema),
    artifacts: z.array(ArtifactSchema),
    media: z.array(MediaSchema),
    videoTiming: z.array(VideoTimingSchema),
    errors: z.array(ErrorSchema),
    diagnostics: z.array(DiagnosticSchema),
  })
  .superRefine((attempt, context) => {
    const pageIds = new Set(attempt.pages.map((page) => page.id))
    const artifactById = new Map(attempt.artifacts.map((artifact) => [artifact.id, artifact]))
    const mediaById = new Map(attempt.media.map((media) => [media.id, media]))
    const mediaByArtifactId = new Map(attempt.media.map((media) => [media.artifactId, media]))
    const eventIds = new Set(attempt.events.map((event) => event.id))

    const duplicatePage = duplicateIndex(attempt.pages, (page) => page.id)
    if (duplicatePage !== undefined) {
      context.addIssue({
        code: 'custom',
        message: 'duplicate page ID',
        path: ['pages', duplicatePage, 'id'],
      })
    }
    for (const [index, page] of attempt.pages.entries()) {
      if (page.createdAtMs > attempt.durationMs) {
        context.addIssue({
          code: 'custom',
          message: 'page creation exceeds attempt duration',
          path: ['pages', index, 'createdAtMs'],
        })
      }
      if (page.closedAtMs !== undefined && page.closedAtMs > attempt.durationMs) {
        context.addIssue({
          code: 'custom',
          message: 'page close exceeds attempt duration',
          path: ['pages', index, 'closedAtMs'],
        })
      }
      if (page.openerPageId !== undefined && !pageIds.has(page.openerPageId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown opener page',
          path: ['pages', index, 'openerPageId'],
        })
      }
    }
    if (
      containsParentCycle(
        attempt.pages,
        (page) => page.id,
        (page) => page.openerPageId,
      )
    ) {
      context.addIssue({ code: 'custom', message: 'page opener cycle', path: ['pages'] })
    }

    const duplicateEvent = duplicateIndex(attempt.events, (event) => event.id)
    if (duplicateEvent !== undefined) {
      context.addIssue({
        code: 'custom',
        message: 'duplicate event ID',
        path: ['events', duplicateEvent, 'id'],
      })
    }
    for (const [index, event] of attempt.events.entries()) {
      const previousEvent = attempt.events[index - 1]
      if (previousEvent !== undefined && event.atMs < previousEvent.atMs) {
        context.addIssue({
          code: 'custom',
          message: 'events must be ordered by time',
          path: ['events', index, 'atMs'],
        })
      }
      if (!pageIds.has(event.pageId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown event page',
          path: ['events', index, 'pageId'],
        })
      }
      if (event.atMs > attempt.durationMs) {
        context.addIssue({
          code: 'custom',
          message: 'event time exceeds attempt duration',
          path: ['events', index, 'atMs'],
        })
      }
      if (event.type === 'checkpoint') {
        const artifact = artifactById.get(event.artifactId)
        if (artifact?.role !== 'checkpoint') {
          context.addIssue({
            code: 'custom',
            message: 'unknown checkpoint artifact',
            path: ['events', index, 'artifactId'],
          })
        } else if (artifact.pageId !== event.pageId) {
          context.addIssue({
            code: 'custom',
            message: 'checkpoint page does not match its artifact',
            path: ['events', index, 'artifactId'],
          })
        } else {
          const media = mediaByArtifactId.get(artifact.id)
          if (
            artifact.contentType !== 'video/webm' ||
            !media?.streams.some((stream) => stream.kind === 'video')
          ) {
            context.addIssue({
              code: 'custom',
              message: 'checkpoint artifact must be a probed WebM video',
              path: ['events', index, 'artifactId'],
            })
          }
        }
      }
    }

    const duplicateStep = duplicateIndex(attempt.steps, (step) => step.id)
    if (duplicateStep !== undefined) {
      context.addIssue({
        code: 'custom',
        message: 'duplicate step ID',
        path: ['steps', duplicateStep, 'id'],
      })
    }
    const stepIds = new Set(attempt.steps.map((step) => step.id))
    for (const [index, step] of attempt.steps.entries()) {
      if (step.parentStepId !== undefined && !stepIds.has(step.parentStepId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown parent step',
          path: ['steps', index, 'parentStepId'],
        })
      }
    }
    if (
      containsParentCycle(
        attempt.steps,
        (step) => step.id,
        (step) => step.parentStepId,
      )
    ) {
      context.addIssue({ code: 'custom', message: 'step parent cycle', path: ['steps'] })
    }

    const duplicateArtifact = duplicateIndex(attempt.artifacts, (artifact) => artifact.id)
    if (duplicateArtifact !== undefined) {
      context.addIssue({
        code: 'custom',
        message: 'duplicate artifact ID',
        path: ['artifacts', duplicateArtifact, 'id'],
      })
    }
    for (const [index, artifact] of attempt.artifacts.entries()) {
      if (artifact.role === 'source-video' && artifact.pageId === undefined) {
        context.addIssue({
          code: 'custom',
          message: 'source video requires a page',
          path: ['artifacts', index, 'pageId'],
        })
      }
      if (artifact.pageId !== undefined && !pageIds.has(artifact.pageId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown artifact page',
          path: ['artifacts', index, 'pageId'],
        })
      }
      if (
        (artifact.role === 'narration-audio' || artifact.role === 'captions') &&
        (artifact.sourceEventId === undefined || !eventIds.has(artifact.sourceEventId))
      ) {
        context.addIssue({
          code: 'custom',
          message: 'narration artifact requires a source event',
          path: ['artifacts', index, 'sourceEventId'],
        })
      }
    }
    for (const page of attempt.pages) {
      const sourceVideoCount = attempt.artifacts.filter(
        (artifact) => artifact.role === 'source-video' && artifact.pageId === page.id,
      ).length
      if (sourceVideoCount !== 1) {
        context.addIssue({
          code: 'custom',
          message: 'each page requires one source video',
          path: ['artifacts'],
        })
        break
      }
    }

    const duplicateMedia = duplicateIndex(attempt.media, (media) => media.id)
    if (duplicateMedia !== undefined) {
      context.addIssue({
        code: 'custom',
        message: 'duplicate media ID',
        path: ['media', duplicateMedia, 'id'],
      })
    }
    for (const [index, media] of attempt.media.entries()) {
      if (!artifactById.has(media.artifactId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown media artifact',
          path: ['media', index, 'artifactId'],
        })
      }
    }

    const timingPages = new Set<string>()
    const timingMedia = new Set<string>()
    for (const [index, timing] of attempt.videoTiming.entries()) {
      const page = attempt.pages.find((candidate) => candidate.id === timing.pageId)
      if (page === undefined) {
        context.addIssue({
          code: 'custom',
          message: 'unknown timing page',
          path: ['videoTiming', index, 'pageId'],
        })
      }
      if (timingPages.has(timing.pageId)) {
        context.addIssue({
          code: 'custom',
          message: 'duplicate timing page',
          path: ['videoTiming'],
        })
      }
      timingPages.add(timing.pageId)

      const media = mediaById.get(timing.mediaId)
      if (media === undefined) {
        context.addIssue({
          code: 'custom',
          message: 'unknown timing media',
          path: ['videoTiming', index, 'mediaId'],
        })
      } else {
        const artifact = artifactById.get(media.artifactId)
        if (artifact?.role !== 'source-video' || artifact.pageId !== timing.pageId) {
          context.addIssue({
            code: 'custom',
            message: 'timing media does not match page source video',
            path: ['videoTiming', index, 'mediaId'],
          })
        }
      }
      if (timingMedia.has(timing.mediaId)) {
        context.addIssue({
          code: 'custom',
          message: 'duplicate timing media',
          path: ['videoTiming'],
        })
      }
      timingMedia.add(timing.mediaId)
    }
    if (timingPages.size !== attempt.pages.length) {
      context.addIssue({
        code: 'custom',
        message: 'each page requires one timing record',
        path: ['videoTiming'],
      })
    }

    for (const [index, diagnostic] of attempt.diagnostics.entries()) {
      if (diagnostic.eventId !== undefined && !eventIds.has(diagnostic.eventId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown diagnostic event',
          path: ['diagnostics', index, 'eventId'],
        })
      }
      if (diagnostic.artifactId !== undefined && !artifactById.has(diagnostic.artifactId)) {
        context.addIssue({
          code: 'custom',
          message: 'unknown diagnostic artifact',
          path: ['diagnostics', index, 'artifactId'],
        })
      }
    }
  })

const TestSchema = z
  .strictObject({
    id: NonEmptyStringSchema,
    order: NonNegativeIntegerSchema,
    title: NonEmptyStringSchema,
    titlePath: z.array(z.string()),
    projectName: z.string(),
    location: SourceLocationSchema,
    attempts: z.array(AttemptSchema),
  })
  .superRefine((test, context) => {
    const duplicateAttempt = duplicateIndex(test.attempts, (attempt) => attempt.id)
    if (duplicateAttempt !== undefined) {
      context.addIssue({
        code: 'custom',
        message: 'duplicate attempt ID',
        path: ['attempts', duplicateAttempt, 'id'],
      })
    }
    const retries = new Set<number>()
    for (const [index, attempt] of test.attempts.entries()) {
      if (retries.has(attempt.retry)) {
        context.addIssue({
          code: 'custom',
          message: 'duplicate retry number',
          path: ['attempts', index, 'retry'],
        })
      }
      retries.add(attempt.retry)
    }
  })

const ManifestSchema = z
  .strictObject({
    schemaVersion: z.literal(SUITECUT_MANIFEST_SCHEMA_VERSION),
    startedAt: ISODateTimeSchema,
    endedAt: ISODateTimeSchema,
    status: z.enum(['passed', 'failed', 'timedout', 'interrupted']),
    rootDirectory: NonEmptyStringSchema,
    tests: z.array(TestSchema),
  })
  .superRefine((manifest, context) => {
    if (Date.parse(manifest.endedAt) < Date.parse(manifest.startedAt)) {
      context.addIssue({ code: 'custom', message: 'run end precedes run start', path: ['endedAt'] })
    }

    const duplicateTest = duplicateIndex(manifest.tests, (test) => test.id)
    if (duplicateTest !== undefined) {
      context.addIssue({
        code: 'custom',
        message: 'duplicate test ID',
        path: ['tests', duplicateTest, 'id'],
      })
    }
    const orders = new Set<number>()
    for (const [index, test] of manifest.tests.entries()) {
      if (orders.has(test.order)) {
        context.addIssue({
          code: 'custom',
          message: 'duplicate test order',
          path: ['tests', index, 'order'],
        })
      }
      orders.add(test.order)
    }
  })

function formatPath(path: readonly PropertyKey[]): string {
  let output = ''
  for (const segment of path) {
    if (typeof segment === 'number') {
      output += `[${segment}]`
    } else if (typeof segment === 'string' && /^[A-Za-z_$][\w$]*$/u.test(segment)) {
      output += output.length === 0 ? segment : `.${segment}`
    } else {
      output += `[${JSON.stringify(String(segment))}]`
    }
  }
  return output
}

export class SuiteCutSchemaError extends Error {
  readonly path: string
  readonly code: string

  constructor(issue: z.core.$ZodIssue) {
    const path = formatPath(issue.path)
    super(path.length === 0 ? issue.message : `${path}: ${issue.message}`)
    this.name = 'SuiteCutSchemaError'
    this.path = path
    this.code = issue.code
  }
}

/** Decodes and cross-validates a persisted SuiteCut manifest. */
export function decodeManifest(input: UntrustedInput): SuiteCutManifest {
  const versionedInput =
    typeof input === 'object' &&
    input !== null &&
    !Array.isArray(input) &&
    !Object.prototype.hasOwnProperty.call(input, 'schemaVersion')
      ? { ...input, schemaVersion: SUITECUT_MANIFEST_SCHEMA_VERSION }
      : input
  const result = ManifestSchema.safeParse(versionedInput)
  if (!result.success) {
    const issue = result.error.issues[0]
    if (issue === undefined) throw new Error('Manifest validation failed without an issue')
    throw new SuiteCutSchemaError(issue)
  }
  return result.data
}

/** Decodes the fixture attachment consumed by the SuiteCut reporter. */
export function decodeEventAttachment(input: UntrustedInput): SuiteCutEventAttachment {
  const result = EventAttachmentSchema.safeParse(input)
  if (!result.success) {
    const issue = result.error.issues[0]
    if (issue === undefined) throw new Error('Event attachment validation failed without an issue')
    throw new SuiteCutSchemaError(issue)
  }
  return result.data
}
