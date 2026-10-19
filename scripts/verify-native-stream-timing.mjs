// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer, Socket } from 'node:net'
import { join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePublisher, createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_STREAM_TIMING_DIRECTORY ??
    '.suitecut/native-stream-timing-source-proof',
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
 * Receives real native publisher TCP bytes.
 * @param {string} name
 */
async function receiver(name) {
  const chunks = z.array(z.instanceof(Buffer)).parse([])
  let bytes = 0
  const active = z.object({ socket: z.instanceof(Socket).optional() }).parse({})
  let eof = Promise.resolve()
  let connected = false
  const server = createServer((socket) => {
    active.socket = socket
    connected = true
    eof = once(socket, 'end').then(() => undefined)
    void eof.catch(() => undefined)
    socket.on('data', (value) => {
      const chunk = z.instanceof(Buffer).parse(value)
      bytes += chunk.length
      if (bytes > 64 * 1024 * 1024) {
        socket.destroy(new Error('Actual publisher bytes exceeded verifier bound'))
        return
      }
      chunks.push(chunk)
    })
  })
  server.listen(0, '127.0.0.1')
  await once(server, 'listening')
  const address = server.address()
  assert(address && typeof address === 'object')
  return {
    url: `tcp://127.0.0.1:${address.port}`,
    async finish() {
      assert(connected, 'Actual publisher never connected')
      await eof
      const path = join(directory, name)
      await writeFile(path, Buffer.concat(chunks))
      return path
    },
    async close() {
      active.socket?.destroy()
      await new Promise((resolve) => server.close(resolve))
    },
  }
}
/**
 * Creates a shared-clock actual canvas and continuous tone with synchronized pulses.
 * @param {import('../dist/native-page.js').NativePage} page
 */
async function scene(page) {
  await page.evaluate('globalThis.nativeAudio?.close()')
  await page.setContent(`<style>body{margin:0;overflow:hidden}button{position:absolute;left:0;top:0}</style><canvas width=160 height=96></canvas><button>audio</button><script>
 document.querySelector('button').onclick=async()=>{
 const audio=new AudioContext();await audio.resume();globalThis.nativeAudio=audio;
 const tone=audio.createOscillator(),gain=audio.createGain();tone.frequency.value=660;gain.gain.value=.15;tone.connect(gain).connect(audio.destination);tone.start();
 const pulse=audio.createOscillator(),pulseGain=audio.createGain();pulse.frequency.value=1320;pulseGain.gain.value=0;pulse.connect(pulseGain).connect(audio.destination);pulse.start();
 for(let at=.5;at<15;at+=.5){pulseGain.gain.setValueAtTime(.3,at);pulseGain.gain.setValueAtTime(0,at+.08)}
 const ctx=document.querySelector('canvas').getContext('2d');globalThis.frames=0;
 function draw(){const t=audio.currentTime;ctx.fillStyle=t>=.5&&t%.5<.08?'white':'black';ctx.fillRect(0,0,160,96);frames++;requestAnimationFrame(draw)}draw();
 };
 </script>`)
  await page.click('button')
  await page.waitForFunction('globalThis.frames>12')
}
/**
 * Advances by actual renderer captures.
 * @param {import('../dist/native-page.js').NativePage} page
 * @param {number} count
 */
async function advance(page, count) {
  const before = z.number().parse(await page.evaluate('globalThis.frames'))
  await page.waitForFunction(`globalThis.frames>=${before + count}`)
}
/**
 * Independently decodes actual received media.
 * @param {string} path
 * @param {import('../dist/native-streaming-protocol.js').NativeStreamingInfo} info
 * @param {number} rate
 * @param {number} pause
 * @param {number} resume
 */
function verify(path, info, rate, pause, resume) {
  assert.equal(info.failure, '')
  assert.equal(info.phase, 'complete')
  assert.equal(info.diagnostics, '')
  assert.equal(info.physicalEncoderPid, 0)
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
  const images = inspect('/usr/bin/ffmpeg', [
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
    'gray',
    'pipe:1',
  ])
  assert.equal(pcm.length / 4, info.audioSubmittedFrames)
  assert.equal(images.length / (160 * 96), info.encodedFrames)
  assert.equal(info.encodedFrames, info.submittedFrames)
  const audioEdges = z.array(z.number()).parse([]),
    visualEdges = z.array(z.number()).parse([]),
    windows = z.array(z.object({ at: z.number(), rms: z.number() })).parse([])
  /**
   * Excludes the actual authored pause transition.
   * @param {number} at
   */
  const excluded = (at) =>
    at > (pause - info.firstTimestampMs) / 1000 - 0.06 &&
    at < (resume - info.firstTimestampMs) / 1000 + 0.06
  let prior = false
  for (let first = 0; first + 240 <= pcm.length / 4; first += 240) {
    let squares = 0
    for (let i = first; i < first + 240; ++i) squares += pcm.readInt16LE(i * 4) ** 2
    const rms = Math.sqrt(squares / 240),
      at = first / 48000
    const pulse = rms > 5500
    if (pulse && !prior) audioEdges.push(at)
    prior = pulse
    if (at > 0.25 && at < pcm.length / 4 / 48000 - 0.1 && !excluded(at)) windows.push({ at, rms })
  }
  assert(windows.length > 300)
  const lowest = windows.reduce((a, b) => (a.rms < b.rms ? a : b))
  assert(
    lowest.rms > 1800,
    `Actual CEF continuous audio dropout at ${lowest.at}s: RMS${lowest.rms}`,
  )
  prior = false
  for (let i = 0; i < info.encodedFrames; ++i) {
    const white = z.number().parse(images[i * 160 * 96 + 48 * 160 + 80]) > 200
    if (white && !prior && i / rate > 0.25 && !excluded(i / rate)) visualEdges.push(i / rate)
    prior = white
  }
  assert(visualEdges.length >= 5)
  const errors = visualEdges.map((at) =>
    Math.min(...audioEdges.map((audio) => Math.abs(audio - at))),
  )
  const maxError = Math.max(...errors)
  const bound = 2 / 120 + 1 / rate + 0.025
  assert(
    maxError < bound,
    `Actual shared-clock CEF visual/audio pulse mismatch ${rate}: ${maxError}s`,
  )
  return {
    path,
    info,
    rate,
    decodedStereoFrames: pcm.length / 4,
    lowestContinuousTone5msRMS: lowest,
    actualAudioEdges: audioEdges,
    actualVisualEdges: visualEdges,
    pulseMaxErrorMs: maxError * 1000,
    pulseBoundMs: bound * 1000,
  }
}
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 160,
  height: 96,
  framesPerSecond: 120,
  pixelFormat: 'i420',
  headless: true,
})
noHostMedia(source)
const page = createNativePage(source)
const results = []
try {
  for (const rate of [120, 59.94, 29.97]) {
    await scene(page)
    const tcp = await receiver(`actual-tone-pulses-${rate}.webm`)
    try {
      const publisher = await createNativePublisher({
        source,
        ffmpegPath: '/usr/bin/ffmpeg',
        publisher: {
          destination: tcp.url,
          container: 'webm',
          videoCodec: 'vp9',
          audioCodec: 'opus',
          audio: true,
          framesPerSecond: rate,
        },
      })
      await advance(page, 130)
      const paused = await publisher.pause()
      await advance(page, 30)
      const resumed = await publisher.resume()
      await page.waitForFunction('globalThis.nativeAudio.currentTime>=6')
      const info = await publisher.stop()
      assert(paused.timestampMs !== undefined && resumed.timestampMs !== undefined)
      results.push(verify(await tcp.finish(), info, rate, paused.timestampMs, resumed.timestampMs))
    } finally {
      await tcp.close()
    }
  }
  const diagnostics = z
    .object({ nativeStreamingPhysical: z.number().int().nonnegative() })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(diagnostics.nativeStreamingPhysical, 0)
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        sourceCaptureVerified: true,
        binarySHA256: await hash(executable),
        results,
        diagnostics,
      },
      null,
      2,
    ) + '\n',
  )
  console.log('Actual CEF native audio continuity and same-clock visual/audio pulse proof passed')
} catch (error) {
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
  await source.close()
}
