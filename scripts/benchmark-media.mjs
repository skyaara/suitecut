/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- JavaScript benchmark uses JSDoc for artifact contracts. */
import assert from 'node:assert/strict'
import { mkdtemp, mkdir, rm, writeFile } from 'node:fs/promises'
import { cpus, release, tmpdir } from 'node:os'
import { dirname, join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { parseArgs } from 'node:util'

import { z } from 'zod'

import { encodePcm16Wav } from '../dist/audio-plugin.js'
import { probeMedia, probeMediaBatch } from '../dist/media.js'

const { values } = parseArgs({
  options: {
    trials: { type: 'string', default: '3' },
    artifacts: { type: 'string', default: '24' },
    output: { type: 'string' },
  },
})

const trials = z.coerce.number().int().min(1).max(20).parse(values.trials)

const count = z.coerce.number().int().min(1).max(128).parse(values.artifacts)

const directory = await mkdtemp(join(tmpdir(), 'suitecut-media-benchmark-'))

try {
  const bytes = encodePcm16Wav(new Float32Array(2400), 24_000)
  /** @type {Array<{ artifact: import('../dist/types.js').SuiteCutArtifact, path: string }>} */
  const inputs = []

  for (let index = 0; index < count; index++) {
    const path = join(directory, `${index}.wav`)
    await writeFile(path, bytes)

    /** @type {import('../dist/types.js').SuiteCutArtifact} */
    const artifact = {
      id: `narration-${index}`,
      name: `${index}.wav`,
      role: 'narration-audio',
      contentType: 'audio/wav',
      path,
      pathKind: 'absolute',
      sizeBytes: bytes.length,
    }

    inputs.push({ artifact, path })
  }

  assert.deepEqual(await probeMediaBatch([]), [])
  const rows = []

  for (const mode of ['unbounded-baseline', 'bounded-four']) {
    const timings = []

    for (let trial = -1; trial < trials; trial++) {
      const start = performance.now()

      const media =
        mode === 'bounded-four'
          ? await probeMediaBatch(inputs)
          : await Promise.all(inputs.map((input) => probeMedia(input.artifact, input.path)))

      const elapsed = performance.now() - start
      assert.deepEqual(
        media.map((item) => item.artifactId),
        inputs.map((input) => input.artifact.id),
      )
      assert.ok(media.every((item) => Math.abs(item.durationMs - 100) < 1))

      if (trial >= 0) timings.push(elapsed)
    }

    timings.sort((a, b) => a - b)
    rows.push({
      mode,
      artifacts: count,
      medianMs: timings[Math.floor(timings.length / 2)],
      timingsMs: timings,
    })
  }

  const first = inputs[0]
  assert.ok(first)
  await assert.rejects(
    probeMediaBatch([
      { artifact: first.artifact, path: join(directory, 'missing.wav') },
      ...inputs,
    ]),
    /FFprobe failed/u,
  )

  const report = {
    benchmark: 'media-probing',
    node: process.version,
    platform: process.platform,
    arch: process.arch,
    os: release(),
    cpu: cpus()[0]?.model,
    trials,
    warmups: 1,
    methodology:
      'Real FFprobe processes reading local 100 ms PCM WAV files. Compares previous unbounded dispatch with batches of four. Includes process startup; excludes fixture creation. Does not model large remote or video files.',
    rows,
  }

  const json = `${JSON.stringify(report, null, 2)}\n`
  process.stdout.write(json)

  if (values.output !== undefined) {
    const output = resolve(values.output)
    await mkdir(dirname(output), { recursive: true })
    await writeFile(output, json)
  }
} finally {
  await rm(directory, { recursive: true, force: true })
}
