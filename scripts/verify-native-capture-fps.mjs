// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout } from 'node:timers/promises'
import { promisify } from 'node:util'

import * as z from 'zod'

import { createNativeVideoRecorder } from '../dist/native-recorder.js'
import { launchNativeBrowser } from '../dist/native.js'
import { resolveFfmpeg, resolveFfprobe } from '../dist/process.js'

const framesPerSecond = z.coerce
  .number()
  .int()
  .min(1)
  .max(120)
  .parse(process.argv[2] ?? 120)
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(`.suitecut/native-capture-${framesPerSecond}fps`)
await mkdir(directory, { recursive: true })
const ffmpegPath = await resolveFfmpeg()
const ffprobePath = await resolveFfprobe(ffmpegPath)
const width = 320,
  height = 180
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(`<!doctype html><style>body{margin:0}</style><canvas width="320" height="180"></canvas><script>
  const context=document.querySelector('canvas').getContext('2d');let id=0;
  function paint(){id++;for(let bit=0;bit<16;bit++){context.fillStyle=(id>>bit)&1?'white':'black';context.fillRect(bit*20,0,20,180);}requestAnimationFrame(paint);}paint();
  </script>`)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const source = await launchNativeBrowser({
  executablePath,
  width,
  height,
  framesPerSecond,
  headless: true,
}).catch((error) => {
  server.closeAllConnections()
  server.close()
  throw error
})
const outputPath = `${directory}/capture.webm`
let unsubscribe = () => undefined
try {
  await source.navigate(`http://127.0.0.1:${address.port}`)
  const recorder = createNativeVideoRecorder({
    source,
    framesPerSecond,
    ffmpegPath,
    outputPath,
    size: { width, height },
  })
  const captured = z
    .array(z.object({ id: z.number().int(), timestampMs: z.number(), dropped: z.number().int() }))
    .parse([])
  unsubscribe = source.onFrame((frame) => {
    let id = 0
    for (let bit = 0; bit < 16; bit++)
      if (frame.data[((height / 2) * width + bit * 20 + 10) * 4] > 128) id |= 1 << bit
    captured.push({ id, timestampMs: frame.timestampMs, dropped: frame.droppedFrames })
    void recorder.push(frame.data, frame.timestampMs)
  })
  // A fixed measurement window, not a readiness delay or assertion retry.
  await Promise.race([setTimeout(2000), source.failure, recorder.failure])
  unsubscribe()
  const endedAtMs = performance.now()
  await recorder.stop(endedAtMs)
  const run = promisify(execFile)
  const probe = await run(ffprobePath, [
    '-v',
    'error',
    '-count_frames',
    '-select_streams',
    'v:0',
    '-show_entries',
    'stream=avg_frame_rate,nb_read_frames',
    '-of',
    'json',
    outputPath,
  ])
  const stream = z
    .object({
      streams: z.array(z.object({ avg_frame_rate: z.string(), nb_read_frames: z.coerce.number() })),
    })
    .parse(JSON.parse(probe.stdout)).streams[0]
  assert(stream)
  assert.equal(stream.avg_frame_rate, `${framesPerSecond}/1`)
  const firstAtMs = recorder.firstAtMs
  assert(firstAtMs !== undefined)
  const durationMs = endedAtMs - firstAtMs
  assert.equal(stream.nb_read_frames, Math.ceil((durationMs * framesPerSecond) / 1000))
  const rawPath = `${directory}/decoded.gray`
  await run(ffmpegPath, [
    '-v',
    'error',
    '-i',
    outputPath,
    '-f',
    'rawvideo',
    '-pix_fmt',
    'gray',
    '-y',
    rawPath,
  ])
  const raw = await readFile(rawPath)
  assert.equal(raw.length, stream.nb_read_frames * width * height)
  const ids = z.array(z.number().int()).parse([])
  for (let offset = 0; offset < raw.length; offset += width * height) {
    let id = 0
    for (let bit = 0; bit < 16; bit++)
      if (raw[offset + (height / 2) * width + bit * 20 + 10] > 128) id |= 1 << bit
    ids.push(id)
  }
  const inputIds = new Set(captured.map((frame) => frame.id))
  assert(
    ids.every((id) => inputIds.has(id)),
    'Decoded output contains a frame ID absent from captured input',
  )
  const firstCapture = captured[0],
    lastCapture = captured.at(-1)
  assert(firstCapture && lastCapture)
  const observedDurationMs = lastCapture.timestampMs - firstCapture.timestampMs
  const distinctInput = new Set(captured.map((frame) => frame.id)).size
  const report = {
    cefVersion: source.cefVersion,
    platform: process.platform,
    renderer: 'headless software',
    width,
    height,
    requestedCaptureFps: framesPerSecond,
    encodedFps: stream.avg_frame_rate,
    durationMs,
    capturedCallbacks: captured.length,
    distinctCapturedFrames: distinctInput,
    observedDurationMs,
    observedDistinctCaptureFps:
      observedDurationMs > 0 ? ((distinctInput - 1) * 1000) / observedDurationMs : null,
    decodedFrames: ids.length,
    distinctDecodedFrames: new Set(ids).size,
    heldOutputFrames: ids.filter((id, index) => index > 0 && id === ids[index - 1]).length,
    nativeDroppedFrames: lastCapture.dropped,
    outputPath,
    note: 'Encoded cadence is verified separately from distinct captured frame throughput; no throughput guarantee is inferred from the FPS label.',
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  unsubscribe()
  await source.close()
  server.closeAllConnections()
  server.close()
}
