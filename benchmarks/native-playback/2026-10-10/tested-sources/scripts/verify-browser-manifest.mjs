import { readFile, stat } from 'node:fs/promises'
import process from 'node:process'

import * as z from 'zod'

const BrowserManifestSchema = z.object({
  tests: z.array(
    z.object({
      projectName: z.string(),
      attempts: z.array(
        z.object({
          status: z.string(),
          events: z.array(z.object({ type: z.string() })),
          artifacts: z.array(
            z.object({
              id: z.string(),
              role: z.string(),
              path: z.string(),
              sizeBytes: z.number().int().nonnegative(),
            }),
          ),
          media: z.array(
            z.object({
              artifactId: z.string(),
              durationMs: z.number().nonnegative(),
              streams: z.array(
                z.object({
                  kind: z.string(),
                  width: z.number().int().positive().optional(),
                  height: z.number().int().positive().optional(),
                  frameRate: z.number().positive().optional(),
                }),
              ),
            }),
          ),
        }),
      ),
    }),
  ),
})

const manifestPath = process.argv[2]
if (manifestPath === undefined) {
  throw new Error('Usage: node scripts/verify-browser-manifest.mjs <manifest>')
}

const manifest = BrowserManifestSchema.parse(JSON.parse(await readFile(manifestPath, 'utf8')))
const expectedProjects = ['chromium', 'firefox', 'webkit']
const verified = []

for (const projectName of expectedProjects) {
  const recordedTest = manifest.tests.find((test) => test.projectName === projectName)
  if (recordedTest === undefined) {
    throw new Error(`Browser manifest is missing the ${projectName} project`)
  }

  const attempt = recordedTest.attempts.at(-1)
  if (attempt?.status !== 'passed') {
    throw new Error(`${projectName} did not produce a passing recording attempt`)
  }

  const sourceVideo = attempt.artifacts.find((artifact) => artifact.role === 'source-video')
  if (sourceVideo === undefined) {
    throw new Error(`${projectName} did not attach a source video`)
  }
  const sourceStats = await stat(sourceVideo.path)
  if (sourceStats.size !== sourceVideo.sizeBytes || sourceStats.size === 0) {
    throw new Error(`${projectName} source video size does not match its manifest metadata`)
  }

  const videoMedia = attempt.media.find((media) => media.artifactId === sourceVideo.id)
  const videoStream = videoMedia?.streams.find((stream) => stream.kind === 'video')
  if (
    videoMedia?.durationMs === undefined ||
    videoMedia.durationMs <= 0 ||
    videoStream?.width === undefined ||
    videoStream.height === undefined ||
    videoStream.frameRate !== 30
  ) {
    throw new Error(`${projectName} source video metadata is incomplete`)
  }

  const eventTypes = new Set(attempt.events.map((event) => event.type))
  for (const requiredEvent of ['checkpoint', 'highlight', 'click', 'hold']) {
    if (!eventTypes.has(requiredEvent)) {
      throw new Error(`${projectName} recording is missing its ${requiredEvent} event`)
    }
  }

  verified.push(
    `${projectName} ${videoStream.width}x${videoStream.height} ${videoStream.frameRate}fps ${Math.round(videoMedia.durationMs)}ms`,
  )
}

process.stdout.write(`Verified SuiteCut browser recordings: ${verified.join(', ')}\n`)
