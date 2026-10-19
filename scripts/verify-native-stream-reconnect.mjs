// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { spawn, spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer, Socket } from 'node:net'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import {
  launchNativeBrowser,
  NativeBrowserContexts,
  createNativePublisher,
  createNativeBroadcast,
} from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_STREAM_OUTPUT_DIRECTORY ??
    '.suitecut/native-stream-reconnect-source-proof',
)
await mkdir(directory, { recursive: true })
/**
 * Hashes the actual proof input.
 * @param {string} file
 */
const hash = async (file) =>
  createHash('sha256')
    .update(await readFile(file))
    .digest('hex')
/**
 * Runs a bounded independent codec inspection.
 * @param {string} program
 * @param {string[]} args
 */
function inspect(program, args) {
  const result = spawnSync(program, args, { timeout: 60_000, maxBuffer: 64 * 1024 * 1024 })
  if (result.error) throw result.error
  assert.equal(result.status, 0, result.stderr.toString('utf8'))
  return result.stdout
}
/**
 * Fails if an authored native publisher subscribes to host media.
 * @param {import('../dist/native-browser.js').NativeBrowser} source
 */
function noHostMedia(source) {
  source.onFrame = () => {
    throw new Error('Native publisher installed a host frame subscription')
  }
  source.onAudio = () => {
    throw new Error('Native publisher installed a host audio subscription')
  }
}
/**
 * Creates an actual animated CEF page and real page audio.
 * @param {import('../dist/native-page.js').NativePage} page
 * @param {boolean} [variant]
 * @param {boolean} [noise]
 */
async function scene(page, variant = false, noise = false) {
  await page.setContent(`<style>body{margin:0;overflow:hidden}button{position:absolute;left:0;top:0}</style><canvas width=160 height=96></canvas><button>audio</button><script>
    globalThis.frames=0;const ctx=document.querySelector('canvas').getContext('2d');
    function draw(){${noise ? 'const data=ctx.createImageData(160,96);for(let i=0;i<data.data.length;i+=4){data.data[i]=Math.random()*256;data.data[i+1]=Math.random()*256;data.data[i+2]=Math.random()*256;data.data[i+3]=255}ctx.putImageData(data,0,0);' : `ctx.fillStyle=${variant ? "'#22ee44'" : "(Math.floor(frames/15)%2)?'#2244ee':'#ee2244'"};ctx.fillRect(0,0,160,96);`}frames++;requestAnimationFrame(draw)}draw();
    document.querySelector('button').onclick=async()=>{const audio=new AudioContext();await audio.resume();const osc=audio.createOscillator();const gain=audio.createGain();gain.gain.value=.15;osc.frequency.value=660;osc.connect(gain).connect(audio.destination);osc.start();globalThis.nativeAudio=audio;globalThis.nativeTone=osc;};
  </script>`)
  await page.click('button')
  await page.waitForFunction('globalThis.frames>=12')
}
/**
 * Waits for actual renderer frames.
 * @param {import('../dist/native-page.js').NativePage} page
 * @param {number} count
 */
async function advance(page, count) {
  const before = z.number().parse(await page.evaluate('globalThis.frames'))
  await page.waitForFunction(`globalThis.frames>=${before + count}`)
}
const metadataSchema = z.object({
  streams: z.array(
    z.object({
      codec_name: z.string(),
      codec_type: z.string(),
      r_frame_rate: z.string(),
      nb_read_frames: z.string().optional(),
      color_space: z.string().optional(),
      color_range: z.string().optional(),
      color_transfer: z.string().optional(),
      color_primaries: z.string().optional(),
      sample_rate: z.string().optional(),
      channels: z.number().optional(),
    }),
  ),
})
/**
 * Decodes real received native media and validates native counters.
 * @param {string} path
 * @param {import('../dist/native-streaming-protocol.js').NativeStreamingInfo} info
 * @param {number} fps
 * @param {string} [codec]
 * @param {boolean} [audio]
 * @param {boolean} [colorsExpected]
 */
function verify(path, info, fps, codec = 'vp9', audio = false, colorsExpected = false) {
  assert.equal(info.phase, 'complete')
  assert.equal(info.failure, '')
  assert.equal(
    info.diagnostics,
    '',
    'Actual FFmpeg error-level diagnostics must fail the publisher',
  )
  assert.equal(info.physicalEncoderPid, 0)
  const metadata = metadataSchema.parse(
    JSON.parse(
      inspect('/usr/bin/ffprobe', [
        '-v',
        'error',
        '-count_frames',
        '-show_streams',
        '-of',
        'json',
        path,
      ]).toString('utf8'),
    ),
  )
  const video = metadata.streams.find((stream) => stream.codec_type === 'video')
  assert(video)
  assert.equal(video.codec_name, codec)
  const [num, den] = video.r_frame_rate.split('/').map(Number)
  assert(num !== undefined && den !== undefined)
  const rateProof = { probeRate: num / den, requestedRate: fps }
  if (codec === 'h264') {
    // FLV millisecond timestamps can make FFprobe infer a nearby NTSC rate.
    // Check the actual H264 VUI and every real packet timestamp independently.
    const header = spawnSync(
      '/usr/bin/ffmpeg',
      [
        '-v',
        'verbose',
        '-i',
        path,
        '-map',
        '0:v',
        '-c',
        'copy',
        '-bsf:v',
        'trace_headers',
        '-f',
        'null',
        '-',
      ],
      { timeout: 60_000, maxBuffer: 8 * 1024 * 1024 },
    )
    if (header.error) throw header.error
    assert.equal(header.status, 0, header.stderr.toString('utf8'))
    const trace = header.stderr.toString('utf8')
    const tick = Number(/num_units_in_tick[^\n]*= (\d+)/u.exec(trace)?.[1])
    const scale = Number(/time_scale[^\n]*= (\d+)/u.exec(trace)?.[1])
    assert(Number.isFinite(tick) && tick > 0 && Number.isFinite(scale))
    assert(Math.abs(scale / (2 * tick) - fps) < 1e-10)
    assert(/fixed_frame_rate_flag[^\n]*= 1/u.test(trace))
    const packets = z
      .object({ packets: z.array(z.object({ pts_time: z.string() })) })
      .parse(
        JSON.parse(
          inspect('/usr/bin/ffprobe', [
            '-v',
            'error',
            '-select_streams',
            'v:0',
            '-show_packets',
            '-show_entries',
            'packet=pts_time',
            '-of',
            'json',
            path,
          ]).toString('utf8'),
        ),
      ).packets
    assert.equal(packets.length, info.submittedFrames)
    const first = Number(packets[0]?.pts_time)
    for (let i = 0; i < packets.length; ++i)
      assert(
        Math.abs(Number(packets[i]?.pts_time) - first - i / fps) <= 0.001001,
        'FLV packet PTS differs from actual requested CFR',
      )
  } else assert(Math.abs(num / den - fps) < 1e-8)
  assert.equal(Number(video.nb_read_frames), info.encodedFrames)
  assert.equal(info.encodedFrames, info.submittedFrames)
  assert.equal(video.color_space, 'bt709')
  assert.equal(video.color_range, 'tv')
  assert.equal(video.color_primaries, 'bt709')
  assert.equal(video.color_transfer, 'iec61966-2-1')
  const rgb = inspect('/usr/bin/ffmpeg', [
    '-v',
    'error',
    '-i',
    path,
    '-map',
    '0:v',
    '-fps_mode',
    'passthrough',
    '-f',
    'rawvideo',
    '-pix_fmt',
    'rgb24',
    'pipe:1',
  ])
  assert.equal(rgb.length, info.encodedFrames * 160 * 96 * 3)
  const colors = new Set()
  for (let frame = 0; frame < info.encodedFrames; ++frame) {
    const at = frame * 160 * 96 * 3 + (48 * 160 + 80) * 3
    const actual = [...rgb.subarray(at, at + 3)]
    const errors = [
      [238, 34, 68],
      [34, 68, 238],
      [34, 238, 68],
    ].map((target) =>
      actual.reduce((sum, v, c) => sum + Math.abs(v - z.number().parse(target[c])), 0),
    )
    assert(Math.min(...errors) < 30, `Actual Source I420 color changed:${actual.join(',')}`)
    colors.add(errors.indexOf(Math.min(...errors)))
  }
  if (colorsExpected)
    assert(colors.has(2) && colors.size >= 2, 'actual source switching missing from encoded output')
  let decodedAudioFrames
  if (audio) {
    const stream = metadata.streams.find((stream) => stream.codec_type === 'audio')
    assert(stream)
    assert.equal(stream.codec_name, codec === 'vp9' ? 'opus' : 'aac')
    assert.equal(stream.sample_rate, '48000')
    assert.equal(stream.channels, 2)
    assert(info.audioInputFrames > 960)
    const pcm = inspect('/usr/bin/ffmpeg', [
      '-v',
      'error',
      '-i',
      path,
      '-map',
      '0:a',
      '-f',
      's16le',
      '-ar',
      '48000',
      '-ac',
      '2',
      'pipe:1',
    ])
    decodedAudioFrames = pcm.length / 4
    if (codec === 'vp9')
      assert.equal(
        pcm.length / 4,
        info.audioSubmittedFrames,
        'Native publisher lost captured PCM samples before Opus encoding',
      )
    let squares = 0
    for (let at = 0; at < pcm.length; at += 4) squares += pcm.readInt16LE(at) ** 2
    assert(Math.sqrt(squares / (pcm.length / 4)) > 300, 'actual captured CEF page audio is silent')
  }
  return {
    path,
    info,
    metadata,
    rateProof,
    colors: [...colors],
    ...(decodedAudioFrames === undefined ? {} : { decodedAudioFrames }),
  }
}
/**
 * Starts a real RTMP listener and observes its actual listen syscall.
 * @param {string} name
 * @param {number} [requestedPort]
 */
async function rtmpReceiver(name, requestedPort = 0) {
  const reservation = createServer()
  reservation.listen(0, '127.0.0.1')
  await once(reservation, 'listening')
  const address = reservation.address()
  assert(address && typeof address === 'object')
  await new Promise((resolve) => reservation.close(resolve))
  const url = `rtmp://127.0.0.1:${requestedPort || address.port}/live/native`,
    path = join(directory, name)
  const child = spawn(
    '/usr/bin/strace',
    [
      '-f',
      '-e',
      'trace=listen',
      '/usr/bin/ffmpeg',
      '-hide_banner',
      '-loglevel',
      'error',
      '-y',
      '-listen',
      '1',
      '-i',
      url,
      '-map',
      '0',
      '-c',
      'copy',
      '-f',
      'flv',
      path,
    ],
    { stdio: ['ignore', 'ignore', 'pipe'], detached: true },
  )
  let text = ''
  const closed = once(child, 'close')
  void closed.catch(() => undefined)
  const listening = new Promise((resolve, reject) => {
    const deadline = setTimeout(
      () => reject(new Error('Actual RTMP listen syscall deadline expired')),
      10_000,
    )
    /**
     * Validates actual diagnostic bytes.
     * @param {import('../dist/untrusted.js').UntrustedInput} value
     */
    const receive = (value) => {
      const bytes = z.instanceof(Buffer).parse(value)
      text += bytes.toString('utf8')
      if (text.length > 65536) {
        clearTimeout(deadline)
        reject(new Error('RTMP receiver diagnostics bound exceeded'))
      }
      if (/listen\([^)]*\)\s*=\s*0/u.test(text)) {
        clearTimeout(deadline)
        resolve(undefined)
      }
    }
    child.stderr.on('data', receive)
    child.once('error', reject)
    child.once('close', () => {
      clearTimeout(deadline)
      reject(new Error(`RTMP receiver exited before listening:${text}`))
    })
  })
  await listening
  return {
    url,
    port: requestedPort || address.port,
    async finish() {
      await closed
      await writeFile(join(directory, `${name}.listener.log`), text)
      return path
    },
    async close() {
      if (child.exitCode === null && child.signalCode === null) {
        try {
          assert(child.pid)
          process.kill(-child.pid, 'SIGKILL')
        } catch (error) {
          if (error instanceof Error && !('code' in error && error.code === 'ESRCH')) throw error
        }
        await closed
      }
    },
  }
}
/**
 * Admits the actual context transport.
 * @param {import('../dist/native-browser.js').NativeBrowser} source
 */
function contextsFor(source) {
  if (!source.runContext || !source.pageSource || !source.onContextEvent)
    throw new Error('Actual native context transport unavailable')
  return new NativeBrowserContexts(
    Object.assign(source, {
      runContext: source.runContext.bind(source),
      pageSource: source.pageSource.bind(source),
      onContextEvent: source.onContextEvent.bind(source),
    }),
  )
}
/**
 * Observes actual asynchronous state under one verifier deadline.
 * @template T
 * @param {()=>Promise<T|false>} read
 * @param {string} reason
 * @returns {Promise<T>}
 */
async function until(read, reason) {
  const deadline = Date.now() + 10000
  for (;;) {
    const value = await read()
    if (value !== false) return value
    if (Date.now() >= deadline) throw new Error(reason)
    await delay(10)
  }
}
/**
 * Creates an actual TCP receiver that retains each genuine connection separately.
 */
async function tcpReceiver() {
  const peers = z.array(z.instanceof(Socket)).parse([])
  /** @type {{chunks:Buffer[],closed:Promise<void>,eof:boolean,error:string}[]} */
  const attempts = []
  let bytes = 0
  const server = createServer((socket) => {
    peers.push(socket)
    let resolve = () => undefined
    const closed = new Promise((done) => {
      resolve = done
    })
    const record = {
      chunks: z.array(z.instanceof(Buffer)).parse([]),
      closed,
      eof: false,
      error: '',
    }
    attempts.push(record)
    socket.on('data', (data) => {
      const chunk = z.instanceof(Buffer).parse(data)
      bytes += chunk.length
      assert(bytes <= 64 * 1024 * 1024)
      record.chunks.push(chunk)
    })
    socket.on('end', () => {
      record.eof = true
    })
    socket.on('error', (error) => {
      assert(error instanceof Error)
      record.error = error.message
    })
    socket.on('close', () => resolve())
  })
  await new Promise((done) => server.listen(0, '127.0.0.1', done))
  const address = server.address()
  assert(address && typeof address !== 'string')
  const port = address.port
  await new Promise((done) => server.close(done))
  return {
    port,
    attempts,
    peers,
    async listen() {
      await new Promise((done, reject) => {
        server.once('error', reject)
        server.listen(port, '127.0.0.1', () => {
          server.off('error', reject)
          done(undefined)
        })
      })
    },
    async close() {
      for (const peer of peers) peer.destroy()
      if (server.listening) await new Promise((done) => server.close(done))
    },
    /**
     * Commits actual received bytes after genuine EOF.
     * @param {string} name
     */
    async writeLast(name) {
      const record = attempts.at(-1)
      assert(record)
      await record.closed
      assert(record.eof, 'Completed publisher must cause actual receiver EOF')
      assert.equal(record.error, '')
      const file = join(directory, name)
      await writeFile(file, Buffer.concat(record.chunks))
      return file
    },
  }
}
/**
 * Produces a genuine established publication followed by an actual socket reset.
 * @param {import('../dist/native-page.js').NativePage} page
 */
async function disconnected(page) {
  const tcp = await tcpReceiver()
  await tcp.listen()
  const publisher = await createNativePublisher({
    source: page.source,
    ffmpegPath: '/usr/bin/ffmpeg',
    publisher: {
      destination: `tcp://127.0.0.1:${tcp.port}`,
      videoCodec: 'vp9',
      audioCodec: 'opus',
      container: 'webm',
      framesPerSecond: 29.97,
      reconnect: { maxAttempts: 3, initialDelayMs: 3000, maxDelayMs: 3000, timeoutMs: 5000 },
      timeoutMs: 8000,
    },
  })
  await until(async () => {
    const status = await publisher.status()
    return status.encodedFrames >= 10 ? status : false
  }, 'Actual publication never encoded before reset')
  tcp.peers.at(-1)?.resetAndDestroy()
  const offline = await until(async () => {
    const status = await publisher.status()
    return status.phase === 'reconnecting' ? status : false
  }, 'Actual socket reset did not enter native disconnected wait')
  assert.equal(offline.physicalEncoderPid, 0)
  return { tcp, publisher, offline }
}
const nativeStderr = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 160,
  height: 96,
  framesPerSecond: 120,
  pixelFormat: 'i420',
  headless: true,
  onDiagnostic: nativeStderr.observe,
})
noHostMedia(source)
const results = z.array(z.unknown()).parse([]),
  behaviorErrors = z.array(z.unknown()).parse([])
try {
  const contexts = contextsFor(source),
    context = await contexts.newContext()
  const page = await context.newPage()
  noHostMedia(page.source)
  await scene(page)
  const tcp = await tcpReceiver()
  try {
    if (!page.source.onStreamingMessage || !page.source.runStreaming)
      throw new Error('Actual native publisher transport unavailable')
    let detach = () => undefined
    const allocated = new Promise((resolve) => {
      detach = page.source.onStreamingMessage((info) => {
        if (info.phase === 'starting') {
          detach()
          resolve(info)
        }
      })
    })
    const starting = createNativePublisher({
      source: page.source,
      ffmpegPath: '/usr/bin/ffmpeg',
      publisher: {
        destination: `tcp://127.0.0.1:${tcp.port}`,
        videoCodec: 'vp9',
        audioCodec: 'opus',
        container: 'webm',
        framesPerSecond: 29.97,
        audio: true,
        reconnect: { maxAttempts: 3, initialDelayMs: 500, maxDelayMs: 1000, timeoutMs: 5000 },
        timeoutMs: 8000,
      },
    })
    const info = z.object({ id: z.string(), clientToken: z.string() }).parse(await allocated)
    const offline = await until(async () => {
      const status = z
        .object({
          phase: z.string(),
          physicalEncoderPid: z.number(),
          lastAttemptFailure: z.string(),
          attempts: z.array(
            z.object({
              encoderPid: z.number(),
              childReaped: z.boolean(),
              childExitObserved: z.boolean(),
            }),
          ),
        })
        .parse(
          await page.source.runStreaming({ action: 'status', streamId: info.id, timeoutMs: 1000 }),
        )
      return status.phase === 'reconnecting' ? status : false
    }, 'Actual initial native publisher never reported its real refusal')
    assert.equal(offline.physicalEncoderPid, 0)
    assert.match(offline.lastAttemptFailure, /refused/i)
    assert(offline.attempts.every((attempt) => attempt.childReaped && attempt.childExitObserved))
    await tcp.listen()
    const publisher = await starting
    await until(async () => {
      const status = await publisher.status()
      return status.attemptNumber === 2 && status.encodedFrames >= 10 ? status : false
    }, 'Actual second TCP attempt never encoded')
    assert.equal(tcp.attempts.length, 1)
    tcp.peers.at(-1)?.resetAndDestroy()
    const reset = await until(async () => {
      const status = await publisher.status()
      return status.phase === 'reconnecting' ? status : false
    }, 'Actual TCP reset never became native reconnecting state')
    assert.equal(reset.physicalEncoderPid, 0)
    const replacement = await until(async () => {
      const status = await publisher.status()
      return status.attemptNumber === 3 && status.encodedFrames >= 20 ? status : false
    }, 'Native TCP replacement never encoded')
    await page.evaluate('document.querySelector("canvas").getContext("2d").fillStyle="#22ee44"')
    await advance(page, 15)
    const complete = await publisher.stop()
    const file = await tcp.writeLast('actual-reconnected-tcp.webm')
    const decoded = verify(file, complete, 29.97, 'vp9', true)
    assert.equal(complete.reconnectAttempts, 2)
    assert.equal(complete.attempts?.length, 3)
    assert(complete.attempts.every((attempt) => attempt.childReaped && attempt.childExitObserved))
    assert(complete.cumulativeEncodedFrames >= complete.encodedFrames)
    assert.equal(complete.capturedFrameBytes, 0)
    assert.equal(tcp.attempts.length, 2)
    results.push({
      name: 'Source-I420-real-TCP-refusal-reset-two-replacements',
      offline,
      reset,
      replacement,
      complete,
      decoded,
      actualReceiverConnections: tcp.attempts.length,
      noHostMediaSubscriptions: true,
    })
  } finally {
    await tcp.close()
  }
  let receiver = await rtmpReceiver('actual-rtmp-first-interrupted.flv')
  try {
    const publisher = await createNativePublisher({
      source: page.source,
      ffmpegPath: '/usr/bin/ffmpeg',
      publisher: {
        destination: receiver.url,
        framesPerSecond: 29.97,
        audio: true,
        reconnect: { maxAttempts: 3, initialDelayMs: 1000, maxDelayMs: 1000, timeoutMs: 5000 },
        timeoutMs: 8000,
      },
    })
    await until(async () => {
      const status = await publisher.status()
      return status.encodedFrames >= 20 ? status : false
    }, 'Actual first RTMP publication never encoded')
    const port = receiver.port
    await receiver.close()
    const offline = await until(async () => {
      const status = await publisher.status()
      return status.phase === 'reconnecting' ? status : false
    }, 'RTMP receiver death never caused native reconnect state')
    assert.equal(offline.physicalEncoderPid, 0)
    receiver = await rtmpReceiver('actual-rtmp-reconnected.flv', port)
    await until(async () => {
      const status = await publisher.status()
      return status.attemptNumber === 2 && status.encodedFrames >= 30 ? status : false
    }, 'Actual replacement RTMP session never encoded')
    const complete = await publisher.stop()
    const file = await receiver.finish()
    const decoded = verify(file, complete, 29.97, 'h264', true)
    assert.equal(complete.reconnectAttempts, 1)
    assert(complete.attempts?.every((attempt) => attempt.childReaped && attempt.childExitObserved))
    assert.equal(complete.capturedFrameBytes, 0)
    results.push({
      name: 'Source-I420-real-RTMP-listener-death-and-republication',
      offline,
      complete,
      decoded,
      actualReceiverPath: file,
    })
  } finally {
    await receiver.close()
  }
  {
    let receiver = await rtmpReceiver('wrapper-rtmp-first-interrupted.flv')
    try {
      const publisher = await createNativeBroadcast({
        source: page.source,
        ffmpegPath: '/usr/bin/ffmpeg',
        stream: {
          url: receiver.url,
          framesPerSecond: 29.97,
          audio: true,
          reconnect: { maxAttempts: 3, initialDelayMs: 1000, maxDelayMs: 1000 },
        },
      })
      await publisher.ready()
      await until(async () => {
        const status = await publisher.health()
        return status.encodedFrames >= 20 ? status : false
      }, 'Actual public wrapper first RTMP publication never encoded')
      const port = receiver.port
      await receiver.close()
      const offline = await until(async () => {
        const status = await publisher.status()
        return status.phase === 'reconnecting' ? status : false
      }, 'Public wrapper receiver death did not become genuine native reconnect state')
      assert.equal(offline.physicalEncoderPid, 0)
      receiver = await rtmpReceiver('wrapper-rtmp-reconnected.flv', port)
      await until(async () => {
        const status = await publisher.status()
        return status.attemptNumber === 2 && status.encodedFrames >= 30 ? status : false
      }, 'Public wrapper replacement RTMP session never encoded')
      await publisher.stop()
      const complete = await publisher.status()
      const file = await receiver.finish()
      const decoded = verify(file, complete, 29.97, 'h264', true)
      assert.equal(complete.reconnectAttempts, 1)
      assert(
        complete.attempts?.every((attempt) => attempt.childReaped && attempt.childExitObserved),
      )
      assert.equal(complete.capturedFrameBytes, 0)
      results.push({
        name: 'Public-wrapper-I420-real-RTMP-listener-death-and-republication',
        offline,
        complete,
        decoded,
        actualReceiverPath: file,
        noHostMediaSubscriptions: true,
      })
    } finally {
      await receiver.close()
    }
  }
  {
    const target = await tcpReceiver()
    let rejected
    try {
      const began = Date.now()
      await assert.rejects(
        createNativePublisher({
          source: page.source,
          ffmpegPath: '/usr/bin/ffmpeg',
          publisher: {
            destination: `tcp://127.0.0.1:${target.port}`,
            videoCodec: 'vp9',
            audioCodec: 'opus',
            container: 'webm',
            framesPerSecond: 29.97,
            reconnect: {
              maxAttempts: 8,
              initialDelayMs: 5000,
              maxDelayMs: 5000,
              timeoutMs: 120000,
            },
            timeoutMs: 40,
          },
        }),
        (error) => {
          assert(error instanceof Error)
          assert.match(error.message, /Timeout|deadline/)
          rejected = { message: error.message, elapsedMs: Date.now() - began }
          return true
        },
      )
      assert(Date.now() - began < 1000)
      await until(async () => {
        const state = z
          .object({ nativeStreamingPhysical: z.number() })
          .parse(await source.runAutomation('diagnostics'))
        return state.nativeStreamingPhysical === 0 ? state : false
      }, 'Timed-out initial publisher did not physically settle')
      results.push({ name: 'Source-original-start-deadline-not-multiplied', rejected })
    } finally {
      await target.close()
    }
  }
  {
    const live = await disconnected(page)
    try {
      const terminal = await live.publisher.cancel()
      assert.match(terminal.failure, /canceled/)
      assert.equal(terminal.physicalEncoderPid, 0)
      assert.equal(terminal.capturedFrameBytes, 0)
      results.push({
        name: 'Source-cancel-during-native-retry-wait',
        offline: live.offline,
        terminal,
      })
    } finally {
      await live.tcp.close()
    }
  }
  {
    const closed = await context.newPage()
    noHostMedia(closed.source)
    await scene(closed)
    const live = await disconnected(closed)
    try {
      await closed.close()
      await assert.rejects(live.publisher.failure, /stopped|closed|canceled/)
      const terminal = await live.publisher.status()
      assert.equal(terminal.phase, 'failed')
      assert.equal(terminal.physicalEncoderPid, 0)
      assert.equal(terminal.capturedFrameBytes, 0)
      results.push({
        name: 'Source-page-close-during-native-retry-wait',
        offline: live.offline,
        terminal,
      })
    } finally {
      await live.tcp.close()
    }
  }
  {
    const selectedContext = await contexts.newContext()
    const selected = await selectedContext.newPage()
    noHostMedia(selected.source)
    await scene(selected)
    const tcp = await tcpReceiver()
    await tcp.listen()
    try {
      const publisher = await createNativePublisher({
        source: page.source,
        ffmpegPath: '/usr/bin/ffmpeg',
        publisher: {
          destination: `tcp://127.0.0.1:${tcp.port}`,
          videoCodec: 'vp9',
          audioCodec: 'opus',
          container: 'webm',
          framesPerSecond: 29.97,
          reconnect: { maxAttempts: 3, initialDelayMs: 3000, maxDelayMs: 3000, timeoutMs: 5000 },
        },
      })
      const selectedInfo = await publisher.selectSource(selected.source)
      assert.equal(selectedInfo.selectedPageId, selected.source.pageId)
      await until(async () => {
        const status = await publisher.status()
        return status.encodedFrames >= 10 ? status : false
      }, 'Actual selected publication did not encode')
      tcp.peers.at(-1)?.resetAndDestroy()
      await until(async () => {
        const status = await publisher.status()
        return status.phase === 'reconnecting' ? status : false
      }, 'Selected publication did not disconnect')
      const crash = selected.waitForEvent('crash', { timeout: 5000 })
      const actual = selected.source.sendDevToolsCommand('Page.crash').catch((error) => {
        assert(error instanceof Error)
        return error.message
      })
      await crash
      await assert.rejects(publisher.failure, /failed|closed/)
      const terminal = await publisher.status()
      assert.equal(terminal.phase, 'failed')
      assert.equal(terminal.physicalEncoderPid, 0)
      assert.equal(terminal.capturedFrameBytes, 0)
      assert.equal(await page.evaluate('6*7'), 42)
      results.push({
        name: 'Source-selected-other-context-renderer-crash-during-retry',
        terminal,
        actualCrash: await actual,
        healthyOwnerValue: 42,
      })
    } finally {
      await tcp.close()
      await selectedContext.close()
    }
  }
  {
    const closing = await contexts.newContext()
    const child = await closing.newPage()
    noHostMedia(child.source)
    await scene(child)
    const live = await disconnected(child)
    try {
      await closing.close()
      await assert.rejects(live.publisher.failure, /stopped|closed|canceled/)
      const terminal = await live.publisher.status()
      assert.equal(terminal.physicalEncoderPid, 0)
      assert.equal(terminal.capturedFrameBytes, 0)
      results.push({ name: 'Source-context-close-during-native-retry-wait', terminal })
    } finally {
      await live.tcp.close()
    }
  }
  await context.close()
  const afterContextClose = z
    .object({ nativeStreamingPhysical: z.literal(0), nativeRecordingPhysical: z.literal(0) })
    .parse(await source.runAutomation('diagnostics'))
  {
    const closing = await contexts.newContext()
    const child = await closing.newPage()
    noHostMedia(child.source)
    await scene(child)
    const live = await disconnected(child)
    try {
      await source.close()
      await assert.rejects(live.publisher.failure, /stopped|closed|canceled/)
      const terminal = await live.publisher.status()
      assert.equal(terminal.phase, 'failed')
      assert.equal(terminal.physicalEncoderPid, 0)
      assert.equal(terminal.capturedFrameBytes, 0)
      assert.equal(source.state, 'closed')
      results.push({
        name: 'Source-helper-close-during-native-retry-wait',
        terminal,
        actualHelperState: source.state,
      })
    } finally {
      await live.tcp.close()
    }
  }

  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256: await hash(executable),
        results,
        afterContextClose,
        scope:
          'Actual CEF Source capture, native publisher reconnect and physical cleanup. Decoded audio presence/sample quantities do not claim continuous ingress under load.',
      },
      null,
      2,
    ) + '\n',
  )
  console.log(
    'Actual native Source TCP and RTMP reconnection, independent decoding and physical cleanup passed',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      {
        completed: false,
        results,
        error: error instanceof Error ? error.stack : 'Non-Error failure',
      },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, nativeStderr, directory, behaviorErrors)
}
