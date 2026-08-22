import { randomUUID } from 'node:crypto'
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises'
import { dirname, isAbsolute, relative, resolve } from 'node:path'

import {
  type FullConfig,
  type FullResult,
  type Reporter,
  type Suite,
  type TestCase,
  type TestError,
  type TestResult,
} from '@playwright/test/reporter'

import { SUITECUT_EVENT_ATTACHMENT } from './constants.js'
import { decodeEventAttachment, decodeManifest } from './manifest.js'
import { probeMedia } from './media.js'
import { type SuiteCutReporterOptions, SuiteCutReporterOptionsSchema } from './schemas.js'
import { normalizeExecutionSteps } from './steps.js'
import {
  type SuiteCutArtifact,
  type SuiteCutAttempt,
  type SuiteCutAttemptClock,
  type SuiteCutDiagnostic,
  type SuiteCutError,
  type SuiteCutEventAttachment,
  type SuiteCutManifest,
  type SuiteCutMedia,
  type SuiteCutTest,
} from './types.js'
import { toError } from './untrusted.js'
import { type UntrustedInput } from './untrusted.js'

export type { SuiteCutReporterOptions } from './schemas.js'

type PlaywrightAttachment = TestResult['attachments'][number]

function normalizeError(error: TestError): SuiteCutError {
  const output: SuiteCutError = {
    message: error.message ?? error.value ?? 'Playwright test failed',
  }
  if (error.stack !== undefined) output.stack = error.stack
  if (error.location !== undefined) {
    output.location = {
      file: error.location.file,
      line: error.location.line,
      column: error.location.column,
    }
  }
  return output
}

async function readAttachment(attachment: PlaywrightAttachment): Promise<Buffer> {
  if (attachment.body !== undefined) return attachment.body
  if (attachment.path !== undefined) return readFile(attachment.path)
  throw new Error(`Attachment ${attachment.name} has no body or path`)
}

function projectName(test: TestCase): string {
  let current: Suite | undefined = test.parent
  while (current !== undefined) {
    const project = current.project()
    if (project !== undefined) return project.name
    current = current.parent
  }
  return ''
}

/** Collects SuiteCut Playwright attachments into one validated run manifest. */
export default class SuiteCutReporter implements Reporter {
  readonly options: SuiteCutReporterOptions
  #config: FullConfig | undefined
  #startedAt = new Date()
  #outputFile = ''
  #order = new Map<string, number>()
  #tests = new Map<string, SuiteCutTest>()
  #errors: Error[] = []
  #pending: Promise<void>[] = []

  /** Creates a reporter with validated output and path options. */
  constructor(options: SuiteCutReporterOptions = {}) {
    const publicOptions = Object.fromEntries(
      Object.entries(options).filter(([key]) => key !== 'configDir' && !key.startsWith('_')),
    )
    this.options = SuiteCutReporterOptionsSchema.parse(publicOptions)
  }

  /** Starts manifest collection for a Playwright run. */
  onBegin(config: FullConfig, suite: Suite): void {
    this.#config = config
    this.#startedAt = new Date()
    const configuredOutput =
      process.env.SUITECUT_MANIFEST_PATH ?? this.options.outputFile ?? '.suitecut/latest-run.json'
    this.#outputFile = isAbsolute(configuredOutput)
      ? configuredOutput
      : resolve(process.cwd(), configuredOutput)
    suite.allTests().forEach((test, index) => this.#order.set(test.id, index))
  }

  /** Queues one completed Playwright attempt for collection. */
  onTestEnd(test: TestCase, result: TestResult): void {
    this.#pending.push(
      this.#recordTest(test, result).catch((error: UntrustedInput) => {
        this.#errors.push(toError(error))
      }),
    )
  }

  async #recordTest(test: TestCase, result: TestResult): Promise<void> {
    if (this.#config === undefined) throw new Error('SuiteCut reporter did not receive onBegin')
    const eventAttachment = result.attachments.find(
      (attachment) => attachment.name === SUITECUT_EVENT_ATTACHMENT,
    )
    let captured: SuiteCutEventAttachment | undefined
    const diagnostics: SuiteCutDiagnostic[] = []
    if (eventAttachment !== undefined) {
      const body = await readAttachment(eventAttachment)
      captured = decodeEventAttachment(JSON.parse(body.toString('utf8')) as UntrustedInput)
    } else {
      diagnostics.push({
        level: 'error',
        code: 'MISSING_ATTACHMENT',
        message: `Test result did not include ${SUITECUT_EVENT_ATTACHMENT}`,
      })
    }

    const clock: SuiteCutAttemptClock = captured?.clock ?? {
      originEpochMs: result.startTime.getTime(),
      originMonotonicMs: 0,
      startedAt: result.startTime.toISOString(),
    }
    const artifacts: SuiteCutArtifact[] = []
    const media: SuiteCutMedia[] = []
    const usedAttachmentNames = new Set<string>([SUITECUT_EVENT_ATTACHMENT])

    if (captured !== undefined) {
      for (const item of captured.artifacts) {
        const attachment = result.attachments.find(
          (candidate) => candidate.name === item.attachmentName,
        )
        if (attachment?.path === undefined) {
          throw new Error(`SuiteCut could not resolve attachment ${item.attachmentName}`)
        }
        usedAttachmentNames.add(item.attachmentName)
        const details = await stat(attachment.path)
        const artifact: SuiteCutArtifact = {
          id: item.id,
          name: item.attachmentName,
          role: item.role,
          contentType: item.contentType,
          path: this.#manifestPath(attachment.path),
          pathKind: this.options.pathKind ?? 'absolute',
          sizeBytes: details.size,
        }
        if (item.role === 'checkpoint') {
          artifact.pageId = item.pageId
          artifact.createdAtMs = item.capturedAtMs
        } else {
          artifact.createdAtMs = item.createdAtMs
          artifact.sourceEventId = item.sourceEventId
          artifact.provider = item.provider
          artifact.voice = item.voice
        }
        artifacts.push(artifact)
        if (item.role === 'narration-audio') {
          media.push(await probeMedia(artifact, attachment.path))
        }
      }

      for (const video of captured.videos) {
        const attachment = result.attachments.find(
          (candidate) => candidate.name === video.attachmentName,
        )
        if (attachment?.path === undefined) {
          throw new Error(`SuiteCut could not resolve source video ${video.attachmentName}`)
        }
        usedAttachmentNames.add(video.attachmentName)
        const details = await stat(attachment.path)
        const artifact: SuiteCutArtifact = {
          id: video.artifactId,
          name: video.attachmentName,
          role: 'source-video',
          contentType: 'video/webm',
          path: this.#manifestPath(attachment.path),
          pathKind: this.options.pathKind ?? 'absolute',
          sizeBytes: details.size,
          pageId: video.pageId,
          createdAtMs: video.sourceStartedAtMs,
        }
        artifacts.push(artifact)
        media.push(await probeMedia(artifact, attachment.path))
      }
    }

    for (const attachment of result.attachments) {
      if (usedAttachmentNames.has(attachment.name) || attachment.path === undefined) continue
      const details = await stat(attachment.path)
      artifacts.push({
        id: randomUUID(),
        name: attachment.name,
        role:
          attachment.name === 'trace' || attachment.path.endsWith('trace.zip')
            ? 'playwright-trace'
            : 'other',
        contentType: attachment.contentType,
        path: this.#manifestPath(attachment.path),
        pathKind: this.options.pathKind ?? 'absolute',
        sizeBytes: details.size,
      })
    }

    const videoTiming =
      captured?.videos.map((video) => {
        const probed = media.find((candidate) => candidate.artifactId === video.artifactId)
        if (probed === undefined)
          throw new Error(`SuiteCut did not probe video ${video.artifactId}`)
        return {
          pageId: video.pageId,
          mediaId: probed.id,
          firstFrameEpochMs: video.firstFrameEpochMs,
          sourceStartedAtMs: video.sourceStartedAtMs,
        }
      }) ?? []

    const attempt: SuiteCutAttempt = {
      id: captured?.attemptId ?? randomUUID(),
      retry: result.retry,
      status: result.status,
      clock,
      durationMs: captured?.endedAtMs ?? result.duration,
      pages: captured?.pages ?? [],
      events: captured?.events ?? [],
      steps: normalizeExecutionSteps(result.steps, clock),
      artifacts,
      media,
      videoTiming,
      errors: result.errors.map(normalizeError),
      diagnostics,
    }

    const existing = this.#tests.get(test.id)
    if (existing === undefined) {
      this.#tests.set(test.id, {
        id: test.id,
        order: this.#order.get(test.id) ?? this.#tests.size,
        title: test.title,
        titlePath: test.titlePath(),
        projectName: projectName(test),
        location: {
          file: test.location.file,
          line: test.location.line,
          column: test.location.column,
        },
        attempts: [attempt],
      })
    } else {
      existing.attempts.push(attempt)
      existing.attempts.sort((left, right) => left.retry - right.retry)
    }
  }

  /** Validates and writes the manifest after all attempts finish. */
  async onEnd(result: FullResult): Promise<void> {
    if (this.#config === undefined) throw new Error('SuiteCut reporter did not receive onBegin')
    await Promise.all(this.#pending)
    if (this.#errors.length > 0) {
      throw new AggregateError(
        this.#errors,
        'SuiteCut reporter failed to collect one or more tests',
      )
    }
    const manifest: SuiteCutManifest = {
      startedAt: this.#startedAt.toISOString(),
      endedAt: new Date().toISOString(),
      status: result.status,
      rootDirectory: this.#config.rootDir,
      tests: [...this.#tests.values()].sort((left, right) => left.order - right.order),
    }
    const decoded = decodeManifest(manifest)
    await mkdir(dirname(this.#outputFile), { recursive: true })
    await writeFile(this.#outputFile, `${JSON.stringify(decoded, null, 2)}\n`, 'utf8')
  }

  #manifestPath(absolutePath: string): string {
    if ((this.options.pathKind ?? 'absolute') === 'absolute') return absolutePath
    return relative(dirname(this.#outputFile), absolutePath)
  }
}
