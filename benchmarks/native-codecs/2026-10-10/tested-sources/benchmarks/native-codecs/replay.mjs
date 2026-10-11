import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import { writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { promisify } from 'node:util'

import { launchEngine } from '../../dist/engine.js'

const directory = resolve(process.env.CODEC_DIRECTORY ?? 'benchmarks/native-codecs/2026-10-10')
const engine = await launchEngine({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
  width: 640,
  height: 360,
  framesPerSecond: 30,
})
try {
  const outputPath = resolve(directory, 'portable-replay.mp4')
  const report = await engine.render({
    manifestPath: resolve(directory, 'render-fixture/manifest-portable.json'),
    outputPath,
    config: { output: { width: 640, height: 360, framesPerSecond: 30 }, resultHoldMs: 0 },
  })
  const execute = promisify(execFile)
  const probe = JSON.parse(
    (
      await execute('ffprobe', [
        '-v',
        'error',
        '-count_frames',
        '-show_streams',
        '-of',
        'json',
        outputPath,
      ])
    ).stdout,
  )
  assert.equal(probe.streams.find((s) => s.codec_type === 'video').codec_name, 'h264')
  assert.equal(Number(probe.streams.find((s) => s.codec_type === 'video').nb_read_frames), 177)
  assert.equal(probe.streams.find((s) => s.codec_type === 'audio').codec_name, 'aac')
  await execute('ffmpeg', [
    '-v',
    'error',
    '-xerror',
    '-threads',
    '2',
    '-i',
    outputPath,
    '-f',
    'null',
    '-',
  ])
  await writeFile(
    resolve(directory, 'portable-replay.json'),
    JSON.stringify({ status: 'passed', report, probe }, null, 2),
  )
} finally {
  await engine.close()
}
