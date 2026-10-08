/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- JavaScript benchmark uses JSDoc for its buffered tag. */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { mkdir, writeFile } from 'node:fs/promises'
import { cpus, release } from 'node:os'
import { dirname, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { parseArgs } from 'node:util'

import { z } from 'zod'

import { LIVE_FLV_HEADER, LiveFlvParser } from '../dist/live-flv.js'

const { values } = parseArgs({
  options: {
    trials: { type: 'string', default: '7' },
    output: { type: 'string' },
  },
})

const trials = z.coerce.number().int().min(3).max(100).parse(values.trials)

const rows = []

for (const payloadBytes of [1024, 16 * 1024, 256 * 1024, 1024 * 1024, 4 * 1024 * 1024]) {
  const tag = Buffer.alloc(payloadBytes + 15, 0x17)
  tag[0] = 9
  tag.writeUIntBE(payloadBytes, 1, 3)
  tag.writeUInt32BE(tag.length - 4, tag.length - 4)

  for (const fragmentBytes of [1024, 16 * 1024, 64 * 1024]) {
    // Enough small frames to make timer resolution insignificant; cap large-frame work.
    const tagsPerTrial = Math.max(4, Math.ceil((1024 * 1024) / payloadBytes))
    const timings = []

    for (let trial = -1; trial < trials; trial++) {
      const parser = new LiveFlvParser()
      let received = 0
      /** @type {Buffer | undefined} */
      let lastTag
      await parser.push(LIVE_FLV_HEADER, () => {
        throw new Error('Header emitted as a tag')
      })
      const start = performance.now()

      for (let index = 0; index < tagsPerTrial; index++) {
        for (let offset = 0; offset < tag.length; offset += fragmentBytes) {
          await parser.push(tag.subarray(offset, offset + fragmentBytes), (parsed) => {
            received++
            lastTag = parsed

            return Promise.resolve()
          })
        }
      }

      const elapsed = performance.now() - start
      assert.equal(received, tagsPerTrial)
      assert.ok(lastTag?.equals(tag), 'FLV payload changed')

      if (trial >= 0) timings.push(elapsed)
    }

    timings.sort((a, b) => a - b)
    const medianMs = timings[Math.floor(timings.length / 2)]
    const p95Ms = timings[Math.ceil(timings.length * 0.95) - 1]
    assert.ok(medianMs !== undefined && p95Ms !== undefined)
    rows.push({
      payloadBytes,
      fragmentBytes,
      tagsPerTrial,
      medianMs,
      p95Ms,
      medianMsPerTag: medianMs / tagsPerTrial,
      payloadMiBPerSecond: (payloadBytes * tagsPerTrial) / 1048576 / (medianMs / 1000),
    })
  }
}

const report = {
  benchmark: 'flv-parser',
  node: process.version,
  platform: process.platform,
  arch: process.arch,
  os: release(),
  cpu: cpus()[0]?.model,
  trials,
  warmups: 1,
  methodology:
    'Synthetic FLV framing, sequential awaited consumption, validation outside timing. Includes buffer allocation and copying. Does not measure encoding, networking, or end-to-end stream latency.',
  rows,
}

const json = `${JSON.stringify(report, null, 2)}\n`

process.stdout.write(json)

if (values.output !== undefined) {
  const output = resolve(values.output)
  await mkdir(dirname(output), { recursive: true })
  await writeFile(output, json)
}
