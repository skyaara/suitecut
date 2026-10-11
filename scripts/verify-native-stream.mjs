import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { createServer as socketServer } from 'node:net'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { launchEngine } from '../dist/engine.js'
import { resolveFfmpeg, resolveFfprobe, runProcess } from '../dist/process.js'

const executablePath = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executablePath) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE')
const directory = resolve(
  process.env.SUITECUT_VERIFICATION_DIRECTORY ?? '.suitecut/native-stream-verification',
)
await mkdir(directory, { recursive: true })
const fixture = await readFile('benchmarks/obs-cef/fixture.html', 'utf8')
const server = createServer((_request, response) => {
  response.setHeader('content-type', 'text/html')
  response.end(fixture)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const ports = socketServer()
ports.listen(0, '127.0.0.1')
await once(ports, 'listening')
const port = ports.address()
assert(port && typeof port !== 'string')
await new Promise((done) => ports.close(done))
const destination = `rtmp://127.0.0.1:${port.port}/live/native`
// FFmpeg is the independent RTMP receiver and validation tool. SuiteCut's
// publishing path uses linked native libraries, without a subprocess.
const ffmpeg = await resolveFfmpeg()
const ffprobe = await resolveFfprobe(ffmpeg)
const receiverPath = resolve(directory, 'received.flv')
const receiver = spawn(
  ffmpeg,
  ['-v', 'error', '-y', '-listen', '1', '-i', destination, '-c', 'copy', receiverPath],
  { stdio: ['ignore', 'ignore', 'pipe'] },
)
let receiverErrors = ''
receiver.stderr.on('data', (data) => {
  receiverErrors = (receiverErrors + String(data)).slice(-8192)
})
const closed = once(receiver, 'close')
void closed.catch(() => undefined)
let engine, output
try {
  engine = await launchEngine({ executablePath, width: 1920, height: 1080, framesPerSecond: 60 })
  const page = engine.browser.contexts()[0].pages()[0]
  await page.goto(`http://127.0.0.1:${address.port}`)
  await page.evaluate("window.activate('canvas')")
  await delay(1000)
  output = await engine.stream(page, { destination, bitrateKbps: 4500 })
  await Promise.race([delay(5000), output.failure])
  const beforeBlock = await output.status()
  Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 4000)
  const afterBlock = await output.status()
  assert(afterBlock.videoFrames - beforeBlock.videoFrames > 180)
  const second = await page.context().newPage()
  await second.goto(`http://127.0.0.1:${address.port}`)
  await second.evaluate("window.activate('canvas')")
  await engine.select(output, second)
  await Promise.race([delay(2000), output.failure])
  await engine.select(output, page)
  await second.close()
  const final = await output.stop()
  assert.equal(final.error, '')
  assert.equal(final.droppedAudioSamples, 0)
  assert.equal(final.queuedVideoBytes, 0)
  assert.equal(final.queuedAudioBytes, 0)
  assert.deepEqual(await output.stop(), final)
  output = undefined
  await Promise.race([closed, delay(5000)])
  if (receiver.exitCode === null) {
    receiver.kill('SIGINT')
    await closed
  }
  const info = z
    .object({
      streams: z.array(
        z.object({
          codec_type: z.string(),
          width: z.number().optional(),
          height: z.number().optional(),
        }),
      ),
    })
    .parse(
      JSON.parse(
        (await runProcess(ffprobe, ['-v', 'error', '-show_streams', '-of', 'json', receiverPath]))
          .stdout,
      ),
    )
  assert(info.streams.some((item) => item.codec_type === 'audio'))
  assert(
    info.streams.some(
      (item) => item.codec_type === 'video' && item.width === 1920 && item.height === 1080,
    ),
  )
  const packets = z
    .object({ packets: z.array(z.object({ pts_time: z.string() })) })
    .parse(
      JSON.parse(
        (
          await runProcess(ffprobe, [
            '-v',
            'error',
            '-select_streams',
            'v',
            '-show_entries',
            'packet=pts_time',
            '-of',
            'json',
            receiverPath,
          ])
        ).stdout,
      ),
    )
    .packets.map((item) => Number(item.pts_time))
  const intervals = packets.slice(1).map((at, index) => at - packets[index])
  assert(intervals.every((interval) => interval > 0))
  intervals.sort((a, b) => a - b)
  const medianIntervalMs = intervals[Math.floor(intervals.length / 2)] * 1000
  assert(medianIntervalMs >= 14 && medianIntervalMs <= 20)
  // Unreachable receivers must produce an observable native failure.
  // Native reconnect is not advertised until it is implemented.
  await assert.rejects(engine.stream(page, { destination }))
  const report = {
    status: 'passed',
    final,
    medianIntervalMs,
    deliveredFps: (packets.length - 1) / (packets.at(-1) - packets[0]),
    framesDuringNodeBlock: afterBlock.videoFrames - beforeBlock.videoFrames,
    pageSwitching: 'passed',
    disconnectFailure: 'passed',
    receiverErrors,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await output?.stop().catch(() => undefined)
  await engine?.close()
  receiver.kill('SIGKILL')
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
}
