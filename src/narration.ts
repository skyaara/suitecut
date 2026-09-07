import { randomUUID } from 'node:crypto'
import { rm } from 'node:fs/promises'

import { type SuiteCutArtifactSink } from './artifact-sink.js'
import { writeTextFileAtomic } from './atomic-file.js'
import { resolveAudioPluginReferences } from './audio-plugin-loader.js'
import { encodeWordTimingArtifact } from './captions.js'
import { probeMediaDurationMs } from './media.js'
import { SuiteCutNarrationWorker } from './narration-worker-client.js'
import { type SuiteCutAudioPluginReference } from './schemas.js'
import {
  type SuiteCutNarrationEvent,
  type SuiteCutRecordingSession,
  type SuiteCutWordTimingArtifact,
} from './types.js'
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
  timingArtifact?: {
    artifact: SuiteCutWordTimingArtifact
    artifactId: string
    attachmentName: string
    outputPath: string
  }
}

interface FailedNarration {
  status: 'failed'
  eventId: string
  error: Error
}

type NarrationResult = CompletedNarration | FailedNarration

export interface SuiteCutNarrationPipeline {
  enqueue(event: SuiteCutNarrationEvent): Promise<SuiteCutNarrationClip>
  finish(): Promise<void>
}

export interface SuiteCutNarrationClip {
  durationMs: number
  timing?: SuiteCutWordTimingArtifact
}

export function createNarrationPipeline(
  output: SuiteCutArtifactSink,
  session: SuiteCutRecordingSession,
  audioPlugins: readonly SuiteCutAudioPluginReference[] = [],
  signal?: AbortSignal,
): SuiteCutNarrationPipeline {
  let worker: SuiteCutNarrationWorker | undefined
  let finishPromise: Promise<void> | undefined
  const jobs: Promise<NarrationResult>[] = []
  const plugins = resolveAudioPluginReferences(audioPlugins)

  const enqueue = (event: SuiteCutNarrationEvent): Promise<SuiteCutNarrationClip> => {
    signal?.throwIfAborted()
    if (finishPromise !== undefined) {
      return Promise.reject(new Error('SuiteCut narration is already finishing'))
    }
    const artifactId = randomUUID()
    const timingArtifactId = randomUUID()
    const provider = event.provider ?? 'kokoro'
    const contentType = provider === 'macos-say' ? 'audio/aiff' : 'audio/wav'
    const extension = provider === 'macos-say' ? 'aiff' : 'wav'
    const attachmentName = `suitecut-narration-${session.attemptId}-${artifactId}.${extension}`
    const outputPath = output.pathFor(attachmentName)
    const timingAttachmentName = `suitecut-word-timings-${session.attemptId}-${timingArtifactId}.json`
    const timingOutputPath = output.pathFor(timingAttachmentName)
    const voice = event.voice ?? (provider === 'kokoro' ? 'af_heart' : 'default')
    const speed = event.speed ?? 1
    const plugin = plugins.get(provider)

    try {
      if (provider !== 'kokoro' && provider !== 'macos-say' && plugin === undefined) {
        throw new Error(`SuiteCut audio plugin is not configured for provider ${provider}`)
      }
      worker ??= new SuiteCutNarrationWorker(signal)
      const slowNotice =
        provider === 'kokoro'
          ? setTimeout(() => {
              process.stderr.write(
                '[SuiteCut] Kokoro is still preparing local narration. The recording clock remains paused.\n',
              )
            }, 5_000)
          : undefined
      slowNotice?.unref()
      const synthesis = worker
        .synthesize({
          jobId: artifactId,
          text: event.text,
          provider,
          voice,
          speed,
          outputPath,
          ...(plugin === undefined ? {} : { plugin }),
        })
        .finally(() => {
          if (slowNotice !== undefined) clearTimeout(slowNotice)
        })
        .then(async (response) => {
          const durationMs = await probeMediaDurationMs(outputPath)
          if (response.timing === undefined) return { durationMs }
          const timingArtifact: SuiteCutWordTimingArtifact = {
            schemaVersion: 1,
            type: 'word-timings',
            sourceEventId: event.id,
            text: response.timing.text,
            durationMs,
            words: response.timing.words,
          }
          await writeTextFileAtomic(timingOutputPath, encodeWordTimingArtifact(timingArtifact))
          return {
            durationMs,
            timingArtifact: {
              artifact: timingArtifact,
              artifactId: timingArtifactId,
              attachmentName: timingAttachmentName,
              outputPath: timingOutputPath,
            },
          }
        })
      const job = synthesis.then<NarrationResult, NarrationResult>(
        ({ durationMs, timingArtifact }) => ({
          status: 'completed',
          artifactId,
          attachmentName,
          outputPath,
          eventId: event.id,
          contentType,
          provider: provider === 'kokoro' ? 'kokoro-82m-wasm' : provider,
          voice,
          durationMs,
          ...(timingArtifact === undefined ? {} : { timingArtifact }),
        }),
        (error: UntrustedInput) => ({
          status: 'failed',
          eventId: event.id,
          error: toError(error),
        }),
      )
      if (session.retainArtifacts !== false) jobs.push(job)
      const clip = synthesis.then(({ durationMs, timingArtifact }) => ({
        durationMs,
        ...(timingArtifact === undefined ? {} : { timing: timingArtifact.artifact }),
      }))
      return session.retainArtifacts === false
        ? clip.finally(async () => {
            await Promise.all([
              rm(outputPath, { force: true }),
              rm(timingOutputPath, { force: true }),
            ])
          })
        : clip
    } catch (error) {
      const failure = error instanceof Error ? error : new Error(String(error))
      if (session.retainArtifacts !== false)
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

  const finishOnce = async (): Promise<void> => {
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
        await output.attach(result.attachmentName, result.outputPath, result.contentType)
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
        if (result.timingArtifact !== undefined) {
          await output.attach(
            result.timingArtifact.attachmentName,
            result.timingArtifact.outputPath,
            'application/json',
          )
          session.artifacts.push({
            id: result.timingArtifact.artifactId,
            attachmentName: result.timingArtifact.attachmentName,
            role: 'captions',
            contentType: 'application/json',
            createdAtMs: session.now(),
            sourceEventId: result.eventId,
          })
        }
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

  const finish = (): Promise<void> => {
    finishPromise ??= finishOnce()
    return finishPromise
  }

  return { enqueue, finish }
}
