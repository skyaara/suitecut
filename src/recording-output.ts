import { stat } from 'node:fs/promises'
import { dirname, relative, resolve } from 'node:path'

import { type SuiteCutArtifactSink } from './artifact-sink.js'
import { probeMedia } from './media.js'
import {
  type SuiteCutArtifact,
  type SuiteCutAttempt,
  type SuiteCutError,
  type SuiteCutEventAttachment,
  type SuiteCutMedia,
  type SuiteCutPathKind,
} from './types.js'

export function rawArtifactSink(outputDirectory: string): SuiteCutArtifactSink {
  return {
    pathFor: (name) => resolve(outputDirectory, name),
    attach: async (_name, path) => {
      await stat(path)
    },
  }
}

function manifestPath(
  absolutePath: string,
  outputFile: string,
  pathKind: SuiteCutPathKind,
): string {
  return pathKind === 'absolute' ? absolutePath : relative(dirname(outputFile), absolutePath)
}

function normalizeError(error: Error): SuiteCutError {
  return {
    message: error.message,
    name: error.name,
    ...(error.stack === undefined ? {} : { stack: error.stack }),
  }
}

export async function createAttempt(
  captured: SuiteCutEventAttachment,
  outputDirectory: string,
  outputFile: string,
  pathKind: SuiteCutPathKind,
  errors: readonly Error[],
): Promise<SuiteCutAttempt> {
  const artifacts: SuiteCutArtifact[] = []
  const mediaJobs: Promise<SuiteCutMedia>[] = []

  for (const item of captured.artifacts) {
    const absolutePath = resolve(outputDirectory, item.attachmentName)
    const details = await stat(absolutePath)
    const artifact: SuiteCutArtifact = {
      id: item.id,
      name: item.attachmentName,
      role: item.role,
      contentType: item.contentType,
      path: manifestPath(absolutePath, outputFile, pathKind),
      pathKind,
      sizeBytes: details.size,
    }
    if (item.role === 'checkpoint') {
      artifact.pageId = item.pageId
      artifact.createdAtMs = item.capturedAtMs
    } else {
      artifact.createdAtMs = item.createdAtMs
      artifact.sourceEventId = item.sourceEventId
      if (item.role === 'narration-audio') {
        artifact.provider = item.provider
        artifact.voice = item.voice
      }
    }
    artifacts.push(artifact)
    if (item.role === 'narration-audio' || item.role === 'checkpoint') {
      mediaJobs.push(probeMedia(artifact, absolutePath))
    }
  }

  for (const video of captured.videos) {
    const absolutePath = resolve(outputDirectory, video.attachmentName)
    const details = await stat(absolutePath)
    const artifact: SuiteCutArtifact = {
      id: video.artifactId,
      name: video.attachmentName,
      role: 'source-video',
      contentType: 'video/webm',
      path: manifestPath(absolutePath, outputFile, pathKind),
      pathKind,
      sizeBytes: details.size,
      pageId: video.pageId,
      createdAtMs: video.sourceStartedAtMs,
    }
    artifacts.push(artifact)
    mediaJobs.push(probeMedia(artifact, absolutePath))
  }

  const media = await Promise.all(mediaJobs)
  const mediaByArtifactId = new Map(media.map((item) => [item.artifactId, item]))
  const videoTiming = captured.videos.map((video) => {
    const probed = mediaByArtifactId.get(video.artifactId)
    if (probed === undefined) throw new Error(`SuiteCut did not probe video ${video.artifactId}`)
    return {
      pageId: video.pageId,
      mediaId: probed.id,
      firstFrameEpochMs: video.firstFrameEpochMs,
      sourceStartedAtMs: video.sourceStartedAtMs,
    }
  })

  return {
    id: captured.attemptId,
    retry: 0,
    status: errors.length === 0 ? 'passed' : 'failed',
    clock: captured.clock,
    durationMs: captured.endedAtMs,
    pages: captured.pages,
    events: captured.events,
    steps: [],
    artifacts,
    media,
    videoTiming,
    errors: errors.map(normalizeError),
    diagnostics: [],
  }
}
