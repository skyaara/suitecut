import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { spawn } from 'node:child_process'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, writeFile, readFile } from 'node:fs/promises'
import { Socket, createServer } from 'node:net'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { Transform } from 'node:stream'
import { setInterval, clearInterval } from 'node:timers'
import { setTimeout as delay } from 'node:timers/promises'

import { chromium } from 'playwright'
import { z } from 'zod'

import { createLiveAudioTransport } from '../dist/live-audio.js'
import { createPersistentLiveStream } from '../dist/live-publisher.js'
import { createLiveStreamAttempt } from '../dist/live-stream.js'
import { resolveFfmpeg, resolveFfprobe, runProcess } from '../dist/process.js'

const width = process.env.SUITECUT_BITRATE_1080P60 === '1' ? 1920 : 1280
const height = process.env.SUITECUT_BITRATE_1080P60 === '1' ? 1080 : 720
const fps = process.env.SUITECUT_BITRATE_1080P60 === '1' ? 60 : 30
const directory = resolve('.suitecut/bitrate-verification', String(Date.now()))
await mkdir(directory, { recursive: true })
const ffmpeg = await resolveFfmpeg()
let closing = false
let receivers = 0
// eslint-disable-next-line jsdoc/check-tag-names
/** @type {Set<import('node:child_process').ChildProcess>} */
const children = new Set()
async function receive() {
  const reservation = createServer()
  reservation.listen(0, '127.0.0.1')
  await once(reservation, 'listening')
  const bound = reservation.address()
  assert(bound && typeof bound !== 'string')
  const port = bound.port
  await new Promise((resolve) => reservation.close(resolve))
  if (closing) return undefined
  const child = spawn(
    ffmpeg,
    [
      '-hide_banner',
      '-loglevel',
      'error',
      '-listen',
      '1',
      '-i',
      `rtmp://127.0.0.1:${port}/live/test`,
      '-c',
      'copy',
      '-y',
      `${directory}/${++receivers}.flv`,
    ],
    { stdio: 'ignore' },
  )
  children.add(child)
  child.on('close', () => {
    children.delete(child)
  })
  await delay(300)
  return port
}
// Throttle only media sent toward the local RTMP receiver, without changing system networking.
let throttle = false
const throttleKbps = 1000
// eslint-disable-next-line jsdoc/check-tag-names
/** @type {Set<import('node:net').Socket>} */
const sockets = new Set()
const proxy = createServer((client) => {
  const target = new Socket()
  const shaper = new Transform({
    transform(chunk, _encoding, callback) {
      const bytes = Buffer.isBuffer(chunk) ? chunk.length : Buffer.byteLength(String(chunk))
      void delay(throttle ? (bytes * 8) / throttleKbps : 0).then(
        () => callback(null, chunk),
        callback,
      )
    },
  })
  void receive()
    .then((port) => {
      if (port && !target.destroyed) target.connect({ host: '127.0.0.1', port })
    })
    .catch(() => target.destroy())
  client.pause()
  target.once('connect', () => client.pipe(shaper).pipe(target))
  sockets.add(client)
  sockets.add(target)
  const destroy = () => {
    client.destroy()
    shaper.destroy()
    target.destroy()
    sockets.delete(client)
    sockets.delete(target)
  }
  client.on('error', destroy)
  shaper.on('error', destroy)
  target.on('error', destroy)
  client.on('close', destroy)
  target.on('close', destroy)
  target.pipe(client)
})
proxy.listen(0, '127.0.0.1')
await once(proxy, 'listening')
const address = proxy.address()
assert(address && typeof address !== 'string')
const browser = await chromium.launch()
const audio = await createLiveAudioTransport(
  () => undefined,
  () => undefined,
)
const pcm = Buffer.alloc(3840)
for (let i = 0; i < 960; i++) {
  const value = Math.round(4000 * Math.sin((2 * Math.PI * 500 * i) / 48000))
  pcm.writeInt16LE(value, i * 4)
  pcm.writeInt16LE(value, i * 4 + 2)
}
const audioTimer = setInterval(() => audio.buffer.push(performance.now(), pcm), 20)
// eslint-disable-next-line jsdoc/check-tag-names
/** @type {import('../dist/live-stream.js').SuiteCutLiveStream | undefined} */
let stream
// eslint-disable-next-line jsdoc/check-tag-names
/** @type {ReturnType<typeof setInterval> | undefined} */
let timer
// eslint-disable-next-line jsdoc/check-tag-names
/** @type {import('../dist/live-congestion.js').LiveStreamDiagnostic[]} */
const diagnostics = []
try {
  const page = await browser.newPage({ viewport: { width, height } })
  await page.setContent(`<canvas width="${width}" height="${height}"></canvas>`)
  // eslint-disable-next-line jsdoc/check-tag-names
  /** @type {Buffer[]} */
  const frames = []
  for (let frame = 0; frame < 8; frame++) {
    await page.evaluate(
      ({ width, height, frame }) => {
        // eslint-disable-next-line no-undef
        const context = document.querySelector('canvas')?.getContext('2d')
        if (!context) throw Error('Missing canvas')
        const gradient = context.createLinearGradient(0, 0, width, height)
        gradient.addColorStop(0, `hsl(${210 + frame * 3} 70% 24%)`)
        gradient.addColorStop(1, `hsl(${265 + frame * 4} 68% 42%)`)
        context.fillStyle = gradient
        context.fillRect(0, 0, width, height)
        for (let row = 0; row < 6; row++) {
          for (let column = 0; column < 8; column++) {
            const x = (column * width) / 8 + frame * 7
            const y = (row * height) / 6 + ((frame + column) % 3) * 5
            context.fillStyle = `hsla(${(row * 45 + column * 18 + frame * 8) % 360} 85% 68% / 0.72)`
            context.fillRect(x, y, width / 11, height / 10)
          }
        }
        context.fillStyle = 'white'
        context.font = `${Math.round(height / 13)}px sans-serif`
        context.fillText(`SuiteCut 1080p60 · frame ${frame + 1}`, width / 15, height / 7)
      },
      { width, height, frame },
    )
    frames.push(Buffer.from(await page.screenshot({ type: 'jpeg', quality: 70 })))
  }
  stream = createPersistentLiveStream(
    ffmpeg,
    {
      url: `rtmp://127.0.0.1:${address.port}/live/test`,
      bitrateKbps: 6000,
      audio: true,
      onDiagnostic: (event) => {
        diagnostics.push(event)
        console.log(event.event, event.bitrateKbps ?? '')
      },
      reconnect: { initialDelayMs: 500, maxDelayMs: 1000, maxAttempts: 3 },
    },
    fps,
    { width, height },
    undefined,
    audio,
    (videoOptions, output, signal) =>
      createLiveStreamAttempt(
        videoOptions.bitrateKbps === 2000 ? process.execPath : ffmpeg,
        videoOptions,
        fps,
        { width, height },
        signal,
        undefined,
        audio,
        undefined,
        undefined,
        output,
      ),
  )
  void stream.failure.catch((error) => console.error('Publisher failure:', String(error)))
  let index = 0
  timer = setInterval(() => {
    const frame = frames[index++ % frames.length]
    if (frame) stream?.update(frame)
  }, 1000 / fps)
  await stream.ready()
  await delay(5000)
  assert(typeof stream.setBitrate === 'function', 'Missing persistent encoder control')
  await assert.rejects(stream.setBitrate(2000), /could not prepare/)
  assert.equal(receivers, 1, 'Failed replacement interrupted the publisher')
  assert(!diagnostics.some((e) => e.event === 'bitrate-adjusted' && e.bitrateKbps === 2000))
  await stream.setBitrate(3000)
  await delay(5000)
  await stream.setBitrate(6000)
  await delay(5000)
  assert.equal(receivers, 1, 'Manual bitrate change reconnected RTMP')
  const beforeThrottle = diagnostics.length
  throttle = true
  const end = Date.now() + 90000
  while (
    Date.now() < end &&
    !diagnostics
      .slice(beforeThrottle)
      .some((e) => e.event === 'bitrate-adjusted' && e.bitrateKbps < 6000)
  )
    await delay(500)
  assert(
    diagnostics
      .slice(beforeThrottle)
      .some((e) => e.event === 'bitrate-adjusted' && e.bitrateKbps < 6000),
    'Throttled RTMP output did not lower bitrate',
  )
  throttle = false
  await delay(5000)
  assert(stream.health().progressAgeMs < 2000, 'Replacement encoder is not progressing')
  assert.equal(receivers, 1, 'Automatic bitrate adaptation reconnected RTMP')
  await stream.stop()
  await delay(500)
  const probe = z
    .object({
      streams: z.array(z.object({ codec_name: z.string() })),
      format: z.object({ bit_rate: z.string() }),
    })
    .parse(
      JSON.parse(
        (
          await runProcess(await resolveFfprobe(ffmpeg), [
            '-v',
            'error',
            '-show_streams',
            '-show_format',
            '-of',
            'json',
            `${directory}/${receivers}.flv`,
          ])
        ).stdout,
      ),
    )
  assert(
    probe.streams.some((s) => s.codec_name === 'h264') &&
      probe.streams.some((s) => s.codec_name === 'aac'),
  )
  const packets = z
    .object({
      packets: z.array(
        z.object({ stream_index: z.number(), pts_time: z.string(), dts_time: z.string() }),
      ),
    })
    .parse(
      JSON.parse(
        (
          await runProcess(await resolveFfprobe(ffmpeg), [
            '-v',
            'error',
            '-show_packets',
            '-show_entries',
            'packet=stream_index,pts_time,dts_time',
            '-of',
            'json',
            `${directory}/${receivers}.flv`,
          ])
        ).stdout,
      ),
    ).packets
  const gaps = []
  for (let track = 0; track < 2; track++) {
    const timestamps = packets
      .filter((p) => p.stream_index === track)
      .map((p) => Number(p.dts_time))
    assert(timestamps.length > 100)
    let maxGap = 0
    for (let i = 1; i < timestamps.length; i++) {
      const delta = timestamps[i] - timestamps[i - 1]
      assert(delta >= 0, 'DTS went backwards')
      if (track === 1 || timestamps[i] < 20) maxGap = Math.max(maxGap, delta)
    }
    gaps.push(maxGap)
  }
  assert(gaps[1] < 0.03, 'AAC timestamps have a discontinuity')
  assert(gaps[0] < 0.25, 'Manual video handoff left a visible frame gap')
  const decoded = await runProcess(ffmpeg, [
    '-v',
    'error',
    '-xerror',
    '-i',
    `${directory}/${receivers}.flv`,
    '-f',
    'null',
    '-',
  ])
  assert.equal(decoded.exitCode, 0, 'Decoder rejected a handoff')
  const pcmPath = `${directory}/audio.pcm`
  const decodedAudio = await runProcess(ffmpeg, [
    '-v',
    'error',
    '-i',
    `${directory}/${receivers}.flv`,
    '-map',
    '0:a:0',
    '-ac',
    '1',
    '-ar',
    '8000',
    '-f',
    's16le',
    '-y',
    pcmPath,
  ])
  assert.equal(decodedAudio.exitCode, 0)
  const receivedAudio = await readFile(pcmPath)
  let quiet = 0,
    maxQuiet = 0
  for (let offset = 8000; offset < receivedAudio.length - 8000; offset += 160) {
    let peak = 0
    for (let i = offset; i < Math.min(offset + 160, receivedAudio.length); i += 2)
      peak = Math.max(peak, Math.abs(receivedAudio.readInt16LE(i)))
    quiet = peak < 100 ? quiet + 0.01 : 0
    maxQuiet = Math.max(maxQuiet, quiet)
  }
  assert(maxQuiet < 0.08, 'Audible silence during video handoff')
  const receivedKbps = Number(probe.format.bit_rate) / 1000
  assert(receivedKbps > 150, 'Missing encoded media')
  await writeFile(
    `${directory}/result.json`,
    JSON.stringify(
      {
        status: 'passed',
        width,
        height,
        fps,
        receivers,
        receivedKbps,
        maxTimestampGaps: gaps,
        maxQuietSeconds: maxQuiet,
        decoderPassed: true,
        diagnostics,
      },
      null,
      2,
    ),
  )
  console.log(
    `PASS: measured network congestion lowered bitrate and video handoffs preserved one RTMP connection. ${directory}`,
  )
} finally {
  clearInterval(timer)
  clearInterval(audioTimer)
  closing = true
  await stream?.stop().catch(() => undefined)
  for (const socket of sockets) socket.destroy()
  await new Promise((resolve) => proxy.close(resolve))
  for (const child of children) child.kill('SIGKILL')
  await audio.close()
  await browser.close()
}
