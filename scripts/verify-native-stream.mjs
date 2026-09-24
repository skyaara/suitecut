/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- JavaScript verification needs JSDoc types. */
import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { createServer as createSocketServer } from 'node:net'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativeBroadcast, launchNativeBrowser } from '../dist/native.js'
import { resolveFfmpeg, resolveFfprobe, runProcess } from '../dist/process.js'

const executablePath = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executablePath) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE')
const ffmpeg = await resolveFfmpeg()
const ffprobe = await resolveFfprobe(ffmpeg)
const directory = resolve('.suitecut/native-stream-verification')
await mkdir(directory, { recursive: true })
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  if (request.url === '/fallback') {
    response.end(
      '<style>body{background:#183044;color:white;font:48px system-ui;margin:60px}</style><h1>Fallback source</h1>',
    )
    return
  }
  response.end(`<!doctype html><style>body{margin:0;background:#183044;color:white;font:40px system-ui}canvas{position:absolute;inset:0}h1{position:relative;margin:180px 40px}</style>
  <canvas width="1920" height="1080"></canvas><h1>${request.url === '/fallback' ? 'Fallback source' : 'Native 1080p60 + PCM'}</h1><script>
  const canvas=document.querySelector('canvas'),ctx=canvas.getContext('2d');
  const audio=new AudioContext({sampleRate:48000}),osc=audio.createOscillator(),gain=audio.createGain();
  gain.gain.value=0;osc.connect(gain).connect(audio.destination);osc.start();
  let next=Math.ceil(audio.currentTime)+1;
  setInterval(()=>{while(next<audio.currentTime+2){gain.gain.setValueAtTime(.2,next);gain.gain.setValueAtTime(0,next+.12);next++}},50);
  function draw(){const t=audio.currentTime;ctx.fillStyle='#183044';ctx.fillRect(0,0,1920,1080);ctx.fillStyle=t%1<.12?'white':'black';ctx.fillRect(0,0,128,128);ctx.fillStyle='#ffc600';ctx.fillRect((t*240)%1800,900,120,120);requestAnimationFrame(draw)}draw();
  </script>`)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const portProbe = createSocketServer()
portProbe.listen(0, '127.0.0.1')
await once(portProbe, 'listening')
const portAddress = portProbe.address()
assert(portAddress && typeof portAddress !== 'string')
await new Promise((resolve) => portProbe.close(resolve))
const rtmp = `rtmp://127.0.0.1:${portAddress.port}/live/native-test`
/** @type {import('../dist/native.js').NativeBrowser[]} */
const sources = []
/** @type {{child: import('node:child_process').ChildProcess, closed: Promise<void>, path: string}[]} */
const receivers = []
const signal = AbortSignal.timeout(120_000)
const diagnostics = []
let broadcast
let sequence = 0
function receiver() {
  const path = `${directory}/received-${sequence++}.flv`
  const child = spawn(
    ffmpeg,
    ['-hide_banner', '-loglevel', 'error', '-y', '-listen', '1', '-i', rtmp, '-c', 'copy', path],
    { stdio: ['ignore', 'ignore', 'pipe'] },
  )
  child.stderr.resume()
  const closed = new Promise((resolve, reject) => {
    child.once('error', reject)
    child.once('close', () => resolve())
  })
  void closed.catch(() => undefined)
  const result = { child, closed, path }
  receivers.push(result)
  return result
}
const launch = async (path) => {
  const source = await launchNativeBrowser({
    executablePath,
    width: 1920,
    height: 1080,
    framesPerSecond: 60,
    pixelFormat: 'i420',
    signal,
  })
  sources.push(source)
  await source.navigate(`http://127.0.0.1:${address.port}${path}`)
  return source
}
try {
  const source = await launch('/live')
  const fallback = await launch('/fallback')
  let captured = 0
  let dropped = 0
  source.onFrame((frame) => {
    captured++
    dropped = frame.droppedFrames
  })
  let receiving = receiver()
  await delay(400, undefined, { signal })
  broadcast = await createNativeBroadcast({
    source,
    ffmpegPath: ffmpeg,
    stream: {
      url: rtmp,
      audio: true,
      bitrateKbps: 4500,
      onDiagnostic: (event) => diagnostics.push(event.event),
    },
    signal,
  })
  await broadcast.ready()
  captured = 0
  const started = performance.now()
  await Promise.race([delay(15_000, undefined, { signal }), source.failure, broadcast.failure])
  const captureFps = captured / ((performance.now() - started) / 1000)
  broadcast.selectSource(fallback)
  const failure = assert.rejects(source.failure)
  await source.sendDevToolsCommand('Page.crash').catch(() => undefined)
  await failure
  await delay(1000, undefined, { signal })
  const replacement = await launch('/live')
  broadcast.selectSource(replacement)
  await delay(3000, undefined, { signal })
  receiving.child.kill('SIGINT')
  await receiving.closed
  const firstPath = receiving.path
  receiving = receiver()
  await Promise.race([delay(10_000, undefined, { signal }), replacement.failure, broadcast.failure])
  await broadcast.stop()
  await Promise.race([receiving.closed, delay(5000).then(() => receiving.child.kill('SIGKILL'))])
  const probe = z.object({
    streams: z.array(
      z.object({
        codec_type: z.string(),
        width: z.number().optional(),
        height: z.number().optional(),
        r_frame_rate: z.string().optional(),
      }),
    ),
  })
  for (const item of receivers) {
    const result = await runProcess(ffprobe, [
      '-v',
      'error',
      '-show_streams',
      '-of',
      'json',
      item.path,
    ])
    const info = probe.parse(JSON.parse(result.stdout))
    const video = info.streams.find((stream) => stream.codec_type === 'video')
    assert(video?.width === 1920 && video.height === 1080)
    // FLV uses millisecond timestamps; ffprobe's inferred frame-rate fraction
    // can be 62.5 or 120 even when consecutive encoded frames are 16/17 ms apart.
    assert(info.streams.some((stream) => stream.codec_type === 'audio'))
  }
  const packetProbe = await runProcess(ffprobe, [
    '-v',
    'error',
    '-select_streams',
    'v',
    '-show_entries',
    'packet=pts_time',
    '-of',
    'json',
    firstPath,
  ])
  const packets = z
    .object({ packets: z.array(z.object({ pts_time: z.string() })) })
    .parse(JSON.parse(packetProbe.stdout))
    .packets.map((packet) => Number(packet.pts_time))
  const intervals = packets.slice(1).map((at, index) => at - packets[index])
  assert(
    intervals.every((value) => value > 0),
    'Encoded video timestamps were not increasing',
  )
  intervals.sort((a, b) => a - b)
  const medianIntervalMs = intervals[Math.floor(intervals.length / 2)] * 1000
  assert(
    medianIntervalMs >= 14 && medianIntervalMs <= 20,
    `Unexpected encoded frame cadence: ${medianIntervalMs}ms`,
  )
  const deliveredFps = (packets.length - 1) / (packets.at(-1) - packets[0])
  // Compare encoded audio bursts to the flashing patch, with both decoded from
  // output time zero. Ignore source-switch intervals and codec startup padding.
  await runProcess(ffmpeg, [
    '-v',
    'error',
    '-y',
    '-i',
    firstPath,
    '-t',
    '12',
    '-vf',
    'crop=64:64:0:0,scale=1:1,fps=60',
    '-pix_fmt',
    'gray',
    '-f',
    'rawvideo',
    `${directory}/luma.raw`,
  ])
  await runProcess(ffmpeg, [
    '-v',
    'error',
    '-y',
    '-i',
    firstPath,
    '-t',
    '12',
    '-vn',
    '-ac',
    '1',
    '-ar',
    '48000',
    '-f',
    's16le',
    `${directory}/audio.raw`,
  ])
  const luma = await readFile(`${directory}/luma.raw`)
  const pcm = await readFile(`${directory}/audio.raw`)
  const visual = []
  const audible = []
  for (let i = 60; i < luma.length; i++) if (luma[i] > 200 && luma[i - 1] < 100) visual.push(i / 60)
  let previouslyAudible = false
  for (let i = 0; i + 960 < pcm.length; i += 960) {
    let peak = 0
    for (let j = i; j < i + 960; j += 2) peak = Math.max(peak, Math.abs(pcm.readInt16LE(j)))
    const sounding = peak > 1000
    if (sounding && !previouslyAudible && i / 96000 > 1) audible.push(i / 96000)
    previouslyAudible = sounding
  }
  assert(visual.length >= 5 && audible.length >= 5, 'Missing encoded flashes or audio bursts')
  const offsets = visual.map((time) =>
    Math.min(...audible.map((audioTime) => Math.abs(time - audioTime))),
  )
  offsets.sort((a, b) => a - b)
  const p95 = offsets[Math.floor((offsets.length - 1) * 0.95)]
  const report = {
    captureFps,
    deliveredFps,
    medianIntervalMs,
    droppedFrames: dropped,
    avOffsetP95Ms: Math.round(p95 * 1000),
    matchedFlashes: offsets.length,
    sourceReplacement: 'passed',
    reconnect: 'passed',
    diagnostics,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
  assert(p95 < 0.2, `Encoded A/V offset is ${p95}s`)
} finally {
  await broadcast?.stop().catch(() => undefined)
  await Promise.all(sources.map((source) => source.close()))
  for (const receiving of receivers) receiving.child.kill('SIGKILL')
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
}
