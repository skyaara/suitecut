// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import { spawn, spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:net'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'

import * as z from 'zod'

import {
  launchNativeBrowser,
  NativeBrowserContexts,
  createNativeBroadcast,
} from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const directory = resolve(
  process.env.SUITECUT_NATIVE_STREAM_OUTPUT_DIRECTORY ?? '.suitecut/native-stream-verification',
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
 */
async function rtmpReceiver(name) {
  const reservation = createServer()
  reservation.listen(0, '127.0.0.1')
  await once(reservation, 'listening')
  const address = reservation.address()
  assert(address && typeof address === 'object')
  await new Promise((resolve) => reservation.close(resolve))
  const url = `rtmp://127.0.0.1:${address.port}/live/native`,
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
const nativeStderr = await captureNativeStderr(directory)
const behaviorErrors = z.array(z.unknown()).parse([])
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
/** @type {import('../dist/native-context.js').NativeBrowserContext | undefined} */
let context
try {
  const contexts = contextsFor(source)
  context = await contexts.newContext()
  const first = await context.newPage(),
    second = await context.newPage()
  noHostMedia(first.source)
  noHostMedia(second.source)
  await scene(first)
  await scene(second, true)
  const rtmp = await rtmpReceiver('actual-wrapper-rtmp.flv')
  try {
    const broadcast = await createNativeBroadcast({
      source: first.source,
      ffmpegPath: '/usr/bin/ffmpeg',
      stream: {
        url: rtmp.url,
        audio: true,
        framesPerSecond: 29.97,
        adaptiveBitrate: false,
        reconnect: false,
      },
    })
    await broadcast.ready()
    await advance(first, 60)
    const before = await broadcast.health()
    assert(before.encodedFrames > 0 && before.selectedPageId === first.source.pageId)
    await broadcast.selectSource(second.source)
    const selected = await broadcast.status()
    assert.equal(selected.selectedPageId, second.source.pageId)
    await advance(second, 60)
    await broadcast.pause()
    await advance(second, 20)
    await broadcast.resume()
    await advance(second, 30)
    await broadcast.stop()
    await broadcast.stop()
    const info = await broadcast.status()
    const output = verify(await rtmp.finish(), info, 29.97, 'h264', true, true)
    const diagnostics = z
      .object({ nativeStreamingPhysical: z.number().int().nonnegative() })
      .parse(await source.runAutomation('diagnostics'))
    assert.equal(diagnostics.nativeStreamingPhysical, 0)
    await writeFile(
      join(directory, 'report.json'),
      JSON.stringify(
        {
          completed: true,
          scope:
            'Integrated public compatibility wrapper with immutable production SDK/native publisher',
          binarySHA256: await hash(executable),
          wrapperSHA256: await hash(
            new URL('../dist/native-broadcast.js', import.meta.url).pathname,
          ),
          output,
          before,
          selected,
          diagnostics,
        },
        null,
        2,
      ) + '\n',
    )
    console.log(
      'Actual native broadcast compatibility wrapper RTMP/audio/source-switch proof passed',
    )
  } finally {
    await rtmp.close()
  }
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      { completed: false, error: error instanceof Error ? error.stack : 'Non-Error failure' },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  await finishNativeProof(
    {
      close: async () => {
        const failures = z.array(z.unknown()).parse([])
        try {
          if (context) await context.close()
        } catch (error) {
          failures.push(error)
        }
        try {
          await source.close()
        } catch (error) {
          failures.push(error)
        }
        if (failures.length === 1) throw failures[0]
        if (failures.length > 1)
          throw new AggregateError(failures, 'Actual context and browser shutdown failed')
      },
    },
    nativeStderr,
    directory,
    behaviorErrors,
  )
}
