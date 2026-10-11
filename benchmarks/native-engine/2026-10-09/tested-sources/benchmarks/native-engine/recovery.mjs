import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import { mkdir, readlink, writeFile } from 'node:fs/promises'
import { join, resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { promisify } from 'node:util'
import { createServer } from 'node:http'
import { launchEngine } from '../../dist/engine.js'

const execute = promisify(execFile)
const directory = resolve('benchmarks/native-engine/2026-10-09/crash-recovery')
await mkdir(directory, { recursive: true })
const destination = join(directory, 'interrupted.mp4')
const server = createServer((_request, response) =>
  response.end('<body style="background:#174daa">Recoverable native capture</body>'),
)
await new Promise((ready) => server.listen(0, '127.0.0.1', ready))
const engine = await launchEngine({
  executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
  width: 640,
  height: 360,
  framesPerSecond: 30,
})
try {
  const page = engine.browser.contexts()[0].pages()[0]
  await page.goto(`http://127.0.0.1:${server.address().port}`)
  await page.evaluate(async () => {
    const audio = new AudioContext()
    await audio.resume()
    const oscillator = audio.createOscillator()
    const gain = audio.createGain()
    gain.gain.value = 0.1
    oscillator.connect(gain).connect(audio.destination)
    oscillator.start()
  })
  const output = await engine.record(page, { destination })
  await delay(5500)
  const { stdout: childPids } = await execute('ps', ['-o', 'pid=', '--ppid', String(process.pid)])
  const children = childPids.trim().split(/\s+/).map(Number)
  const helpers = []
  for (const pid of children) {
    const executable = await readlink(`/proc/${pid}/exe`).catch(() => '')
    if (executable.endsWith('/suitecut-browser')) helpers.push(pid)
  }
  assert.equal(helpers.length, 1, "Find this test process's sole native helper")
  process.kill(helpers[0], 'SIGKILL')
  const failure = await Promise.race([
    output.failure.catch((error) => error),
    delay(15000).then(() => {
      throw new Error('Missing native failure notification')
    }),
  ])
  assert(failure instanceof Error)
  const { stdout } = await execute('ffprobe', [
    '-v',
    'error',
    '-show_format',
    '-show_streams',
    '-of',
    'json',
    destination,
  ])
  const probe = JSON.parse(stdout)
  assert(
    Number(probe.format.duration) >= 2,
    'Completed fragments survive abrupt helper termination',
  )
  assert(probe.streams.some((stream) => stream.codec_type === 'video'))
  assert(probe.streams.some((stream) => stream.codec_type === 'audio'))
  const decode = await execute('ffmpeg', [
    '-v',
    'error',
    '-xerror',
    '-i',
    destination,
    '-f',
    'null',
    '-',
  ])
  assert.equal(decode.stderr, '')
  const report = {
    status: 'passed',
    termination: 'SIGKILL without muxer finalization',
    capturedBeforeKillMs: 5500,
    recoverableDurationSeconds: Number(probe.format.duration),
    failureNotification: failure.message,
    decodedWithoutErrors: true,
    probe,
    limitation:
      'Only completed keyframe fragments are recoverable; the final unfinished fragment may be lost.',
  }
  await writeFile(join(directory, 'report.json'), JSON.stringify(report, null, 2))
  console.log(JSON.stringify(report, null, 2))
} finally {
  await engine.close()
  server.closeAllConnections()
  server.close()
}
