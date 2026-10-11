import assert from 'node:assert/strict'
import { readFile, stat } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { resolveFfmpeg, resolveFfprobe, runProcess } from '../dist/process.js'

const ArtifactSchema = z.object({ name: z.string(), path: z.string(), role: z.string() })
const ManifestSchema = z.object({
  tests: z.array(
    z.object({
      attempts: z.array(z.object({ status: z.string(), artifacts: z.array(ArtifactSchema) })),
    }),
  ),
})
const ProbeSchema = z.object({
  streams: z.array(z.object({ width: z.number(), height: z.number(), r_frame_rate: z.string() })),
})

const manifestPath = process.argv[2]
if (manifestPath === undefined) {
  throw new Error('Usage: node scripts/verify-device-scale.mjs <manifest>')
}

const manifest = ManifestSchema.parse(JSON.parse(await readFile(manifestPath, 'utf8')))
const attempt = manifest.tests
  .flatMap((test) => test.attempts)
  .find((candidate) => candidate.artifacts.some((artifact) => artifact.role === 'source-video'))
assert.equal(attempt?.status, 'passed', 'Device-scale browser test did not pass')
const sourceVideo = attempt.artifacts.find((artifact) => artifact.role === 'source-video')
assert(sourceVideo, 'Device-scale recording did not retain a source video')
assert((await stat(sourceVideo.path)).size > 0, 'Device-scale source video is empty')

const ffmpeg = await resolveFfmpeg()
const ffprobe = await resolveFfprobe(ffmpeg)
const probe = await runProcess(
  ffprobe,
  [
    '-v',
    'error',
    '-select_streams',
    'v:0',
    '-show_entries',
    'stream=width,height,r_frame_rate',
    '-of',
    'json',
    sourceVideo.path,
  ],
  { timeoutMs: 30_000 },
)
assert.equal(probe.exitCode, 0, 'ffprobe could not read the device-scale recording')
const stream = ProbeSchema.parse(JSON.parse(probe.stdout)).streams[0]
assert.equal(stream?.width, 1920)
assert.equal(stream?.height, 1080)
assert.equal(stream?.r_frame_rate, '30/1')

const visualArtifacts = [
  'device-scale-physical-2x.png',
  'device-scale-native-1x.png',
  'device-scale-2x-downscaled.png',
  'device-scale-comparison.json',
].map((name) => resolve('.suitecut/device-scale-artifacts', name))
await Promise.all(
  visualArtifacts.map(async (path) =>
    assert((await stat(path)).size > 0, `Device-scale artifact is empty: ${path}`),
  ),
)
process.stdout.write(
  `Verified 3840x2160 device capture downscaled to ${String(stream.width)}x${String(stream.height)} at 30 fps; retained ${String(visualArtifacts.length)} visual artifacts.\n`,
)
