import { randomUUID } from 'node:crypto'

import { type TestInfo } from '@playwright/test'

import { probeMediaDurationMs } from './media.js'
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
  durationMs: number
}

interface FailedNarration {
  status: 'failed'
  eventId: string
  error: Error
}

type NarrationResult = CompletedNarration | FailedNarration

export interface SuiteCutNarrationPipeline {
  enqueue(event: SuiteCutNarrationEvent): Promise<number>
  finish(): Promise<void>
}

export function createNarrationPipeline(
  testInfo: TestInfo,
  session: SuiteCutRecordingSession,
): SuiteCutNarrationPipeline {
  let worker: SuiteCutNarrationWorker | undefined
  const jobs: Promise<NarrationResult>[] = []

  const enqueue = (event: SuiteCutNarrationEvent): Promise<number> => {
    const artifactId = randomUUID()
    const provider = event.provider ?? 'kokoro'
    const contentType = provider === 'kokoro' ? 'audio/wav' : 'audio/aiff'
    const extension = provider === 'kokoro' ? 'wav' : 'aiff'
    const attachmentName = `suitecut-narration-${artifactId}.${extension}`
    const outputPath = testInfo.outputPath(attachmentName)
    const voice = event.voice ?? (provider === 'kokoro' ? 'af_heart' : 'default')
    const speed = event.speed ?? 1

    try {
      worker ??= new SuiteCutNarrationWorker()
      const synthesis = worker
        .synthesize({
          jobId: artifactId,
          text: event.text,
          provider,
          voice,
          speed,
          outputPath,
        })
        .then(async () => ({
          durationMs: await probeMediaDurationMs(outputPath),
        }))
      const job = synthesis.then<NarrationResult, NarrationResult>(
        ({ durationMs }) => ({
          status: 'completed',
          artifactId,
          attachmentName,
          outputPath,
          eventId: event.id,
          contentType,
          provider: provider === 'kokoro' ? 'kokoro-82m-wasm' : 'macos-say',
          voice,
          durationMs,
        }),
        (error: UntrustedInput) => ({
          status: 'failed',
          eventId: event.id,
          error: toError(error),
        }),
      )
      jobs.push(job)
      return synthesis.then(({ durationMs }) => durationMs)
    } catch (error) {
      const failure = error instanceof Error ? error : new Error(String(error))
      jobs.push(
        Promise.resolve<FailedNarration>({
          status: 'failed',
          eventId: event.id,
          error: failure,
        }),
      )
      return Promise.reject(failure)
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
