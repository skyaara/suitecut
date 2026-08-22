import { randomUUID } from 'node:crypto'

import { type TestInfo } from '@playwright/test'

import { SuiteCutNarrationWorker } from './narration-worker-client.js'
import { type SuiteCutNarrationEvent, type SuiteCutRecordingSession } from './types.js'
import { toError, type UntrustedInput } from './untrusted.js'

interface CompletedNarration {
  status: 'completed'
  artifactId: string
  attachmentName: string
  outputPath: string
  eventId: string
  contentType: string
  provider: string
  voice: string
}

interface FailedNarration {
  status: 'failed'
  eventId: string
  error: Error
}

type NarrationResult = CompletedNarration | FailedNarration

export interface SuiteCutNarrationPipeline {
  enqueue(event: SuiteCutNarrationEvent): void
  finish(): Promise<void>
}

export function createNarrationPipeline(
  testInfo: TestInfo,
  session: SuiteCutRecordingSession,
): SuiteCutNarrationPipeline {
  let worker: SuiteCutNarrationWorker | undefined
  const jobs: Promise<NarrationResult>[] = []

  const enqueue = (event: SuiteCutNarrationEvent): void => {
    const artifactId = randomUUID()
    const provider = event.provider ?? 'macos-say'
    const contentType = provider === 'kokoro' ? 'audio/wav' : 'audio/aiff'
    const extension = provider === 'kokoro' ? 'wav' : 'aiff'
    const attachmentName = `suitecut-narration-${artifactId}.${extension}`
    const outputPath = testInfo.outputPath(attachmentName)
    const voice = event.voice ?? (provider === 'kokoro' ? 'af_heart' : 'default')
    const speed = event.speed ?? 1

    try {
      worker ??= new SuiteCutNarrationWorker()
      const job = worker
        .synthesize({
          jobId: artifactId,
          text: event.text,
          provider,
          voice,
          speed,
          outputPath,
        })
        .then<NarrationResult, NarrationResult>(
          () => ({
            status: 'completed',
            artifactId,
            attachmentName,
            outputPath,
            eventId: event.id,
            contentType,
            provider: provider === 'kokoro' ? 'kokoro-82m-wasm' : 'macos-say',
            voice,
          }),
          (error: UntrustedInput) => ({
            status: 'failed',
            eventId: event.id,
            error: toError(error),
          }),
        )
      jobs.push(job)
    } catch (error) {
      jobs.push(
        Promise.resolve({
          status: 'failed',
          eventId: event.id,
          error: error instanceof Error ? error : new Error(String(error)),
        }),
      )
    }
  }

  const finish = async (): Promise<void> => {
    const results = await Promise.all(jobs)
    const errors: Error[] = []

    for (const result of results) {
      if (result.status === 'failed') {
        errors.push(
          new Error(
            `Failed to synthesize narration event ${result.eventId}: ${result.error.message}`,
            { cause: result.error },
          ),
        )
        continue
      }

      try {
        await testInfo.attach(result.attachmentName, {
          path: result.outputPath,
          contentType: result.contentType,
        })
        session.artifacts.push({
          id: result.artifactId,
          attachmentName: result.attachmentName,
          role: 'narration-audio',
          contentType: result.contentType,
          createdAtMs: session.now(),
          sourceEventId: result.eventId,
          provider: result.provider,
          voice: result.voice,
        })
      } catch (error) {
        errors.push(error instanceof Error ? error : new Error(String(error)))
      }
    }

    try {
      await worker?.close()
    } catch (error) {
      errors.push(error instanceof Error ? error : new Error(String(error)))
    }

    const firstError = errors.at(0)
    if (errors.length === 1 && firstError !== undefined) throw firstError
    if (errors.length > 1) {
      throw new AggregateError(errors, 'Failed to finish SuiteCut narration audio')
    }
  }

  return { enqueue, finish }
}
