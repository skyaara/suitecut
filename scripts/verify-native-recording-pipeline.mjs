// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { spawnSync } from 'node:child_process'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { access, mkdir, readFile, readdir, rm, stat, writeFile } from 'node:fs/promises'
import { dirname, join, resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { NativeRecordingInfoSchema } from '../dist/native-recording-protocol.js'
import { createNativePage, launchNativeBrowser, recordNative } from '../dist/native.js'

const executable = z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const ffmpeg = z
  .string()
  .min(1)
  .parse(process.env.SUITECUT_FFMPEG ?? '/usr/bin/ffmpeg')
const directory = resolve(
  process.env.SUITECUT_NATIVE_RECORDING_PROOF_DIRECTORY ??
    '.suitecut/native-recording-pipeline-proof',
)
await mkdir(directory, { recursive: true })
await rm(join(directory, 'report.json'), { force: true })
const checks = []
/**
 * Executes a bounded codec inspection process, separate from native recording.
 * @param {string} program
 * @param {string[]} args
 */
const inspect = (program, args) => {
  const result = spawnSync(program, args, { timeout: 120_000, maxBuffer: 64 * 1024 * 1024 })
  if (result.error) throw result.error
  assert.equal(result.status, 0, result.stderr.toString('utf8'))
  return result.stdout
}
/**
 * Hashes exact native/runtime proof inputs.
 * @param {string} path
 */
const hash = async (path) =>
  createHash('sha256')
    .update(await readFile(path))
    .digest('hex')
const source = await launchNativeBrowser({
  executablePath: executable,
  width: 160,
  height: 96,
  framesPerSecond: 120,
  headless: true,
})
const page = createNativePage(source)
/**
 * Sends actual native recording commands and validates their metadata.
 * @param {Record<string, object|string|number|boolean|null>} params
 */
const command = async (params) => {
  if (!source.runRecording) throw new Error('Native recording transport missing')
  return NativeRecordingInfoSchema.parse(await source.runRecording(params))
}
const probeSchema = z.object({
  streams: z.array(
    z.object({
      codec_type: z.string(),
      codec_name: z.string(),
      r_frame_rate: z.string(),
      nb_read_frames: z.string().optional(),
      sample_rate: z.string().optional(),
      channels: z.number().optional(),
    }),
  ),
})
/**
 * Verifies real completed container frames and captured page audio.
 * @param {import('../dist/native-recording-protocol.js').NativeRecordingInfo} info
 * @param {boolean} audio
 */
const verifyFile = async (info, audio) => {
  assert.equal(info.phase, 'complete')
  assert.equal(info.failure, '')
  assert.equal((await stat(info.outputPath)).size, info.outputBytes)
  assert(info.inputFrames >= 1)
  assert(info.outputFrames >= 1)
  assert(info.heldFrames <= info.outputFrames - 1)
  assert(info.outputFrames - info.heldFrames <= info.inputFrames)
  const probe = probeSchema.parse(
    JSON.parse(
      inspect('/usr/bin/ffprobe', [
        '-v',
        'error',
        '-count_frames',
        '-show_streams',
        '-of',
        'json',
        info.outputPath,
      ]).toString('utf8'),
    ),
  )
  const video = probe.streams.find((stream) => stream.codec_type === 'video')
  assert(video)
  assert.equal(video.codec_name, 'vp9')
  assert.equal(Number(video.nb_read_frames), info.outputFrames)
  const [numerator, denominator] = video.r_frame_rate.split('/').map(Number)
  assert(Math.abs(numerator / denominator - info.framesPerSecond) < 1e-6)
  const bytes = inspect(ffmpeg, [
    '-v',
    'error',
    '-i',
    info.outputPath,
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
  assert.equal(bytes.length, info.outputFrames * 160 * 96 * 3)
  const colors = new Set()
  for (let frame = 0; frame < info.outputFrames; frame++) {
    const offset = frame * 160 * 96 * 3 + (48 * 160 + 80) * 3
    colors.add(
      [...bytes.subarray(offset, offset + 3)].map((value) => Math.round(value / 20)).join(','),
    )
  }
  assert(colors.size >= 2, 'native container lost actual animated page images')
  if (audio) {
    const stream = probe.streams.find((value) => value.codec_type === 'audio')
    assert(stream)
    assert.equal(stream.codec_name, 'opus')
    assert.equal(stream.sample_rate, '48000')
    assert.equal(stream.channels, 2)
    assert(info.audioInputFrames > 960, 'no real CEF audio was captured')
    const pcm = inspect(ffmpeg, [
      '-v',
      'error',
      '-i',
      info.outputPath,
      '-map',
      '0:a',
      '-f',
      's16le',
      '-ac',
      '2',
      '-ar',
      '48000',
      'pipe:1',
    ])
    let squares = 0
    for (let offset = 0; offset < pcm.length; offset += 4) squares += pcm.readInt16LE(offset) ** 2
    assert(Math.sqrt(squares / (pcm.length / 4)) > 300, 'recorded page audio is silent')
  }
  return probe
}
const recordings = []
let closingSource
let forcedSource
try {
  await page.setContent(`<style>body{margin:0;overflow:hidden}button{position:absolute;top:0;left:0}</style><canvas width=160 height=96></canvas><button id=start>Start audio</button><script>
    globalThis.mediaFrames=0;
    const ctx=document.querySelector('canvas').getContext('2d');
    function draw(){ctx.fillStyle=(Math.floor(mediaFrames/15)%2)?'#2244ee':'#ee2244';ctx.fillRect(0,0,160,96);mediaFrames++;requestAnimationFrame(draw)}draw();
    document.querySelector('button').onclick=async()=>{const audio=new AudioContext();await audio.resume();const osc=audio.createOscillator();const gain=audio.createGain();gain.gain.value=.15;osc.frequency.value=660;osc.connect(gain).connect(audio.destination);osc.start();globalThis.nativeTone=osc;};
  </script>`)
  await page.click('#start')
  let info = await command({
    action: 'start',
    ffmpegPath: ffmpeg,
    outputPath: join(directory, 'actual-120-audio.webm'),
    framesPerSecond: 120,
    width: 160,
    height: 96,
    audio: true,
  })
  const startCounter = z.number().parse(await page.evaluate('globalThis.mediaFrames'))
  await page.waitForFunction(`globalThis.mediaFrames >= ${z.number().parse(startCounter) + 60}`)
  const active = await command({ action: 'status', recordingId: info.id })
  assert(
    active.outputFrames >= 1 && active.inputFrames >= 1,
    'active native progress is unavailable',
  )
  info = await command({ action: 'stop', recordingId: info.id })
  await verifyFile(info, true)
  recordings.push(info)
  checks.push('actual-CEF-120-CFR-decoded-animation-and-stereo-audio')

  info = await command({
    action: 'start',
    ffmpegPath: ffmpeg,
    outputPath: join(directory, 'actual-pause.webm'),
    framesPerSecond: 120,
    width: 160,
    height: 96,
    audio: true,
  })
  let counter = z.number().parse(await page.evaluate('globalThis.mediaFrames'))
  await page.waitForFunction(`globalThis.mediaFrames >= ${counter + 20}`)
  const paused = await command({ action: 'pause', recordingId: info.id })
  counter = z.number().parse(await page.evaluate('globalThis.mediaFrames'))
  await page.waitForFunction(`globalThis.mediaFrames >= ${counter + 30}`)
  const during = await command({ action: 'status', recordingId: info.id })
  assert.equal(during.inputFrames, paused.inputFrames)
  assert.equal(during.audioInputFrames, paused.audioInputFrames)
  const resumed = await command({ action: 'resume', recordingId: info.id })
  counter = z.number().parse(await page.evaluate('globalThis.mediaFrames'))
  await page.waitForFunction(`globalThis.mediaFrames >= ${counter + 20}`)
  const beforeStop = await command({ action: 'status', recordingId: info.id })
  info = await command({ action: 'stop', recordingId: info.id })
  const pauseDuration = z.number().parse(resumed.timestampMs) - z.number().parse(paused.timestampMs)
  const expected = z.number().parse(beforeStop.timestampMs) - info.firstTimestampMs - pauseDuration
  assert(
    Math.abs(info.durationMs - expected) < 100,
    'native pause did not exclude the real capture interval',
  )
  await verifyFile(info, true)
  recordings.push(info)
  checks.push('actual-native-pause-excludes-video-and-audio-without-host-pump')

  info = await command({
    action: 'start',
    ffmpegPath: ffmpeg,
    outputPath: join(directory, 'actual-fractional.webm'),
    framesPerSecond: 59.94,
    width: 160,
    height: 96,
    audio: false,
  })
  counter = z.number().parse(await page.evaluate('globalThis.mediaFrames'))
  await page.waitForFunction(`globalThis.mediaFrames >= ${counter + 45}`)
  info = await command({ action: 'stop', recordingId: info.id })
  await verifyFile(info, false)
  recordings.push(info)
  checks.push('actual-fractional-59.94-native-resampling')

  info = await command({
    action: 'start',
    ffmpegPath: ffmpeg,
    outputPath: join(directory, 'saturated-finalize.webm'),
    framesPerSecond: 120,
    width: 160,
    height: 96,
    audio: false,
  })
  const abort = new AbortController()
  const evaluationPage = createNativePage(source, abort.signal)
  const evaluations = Array.from({ length: 64 }, () =>
    evaluationPage.evaluate('new Promise(() => {})').then(
      () => null,
      (error) => (error instanceof Error ? error.message : 'native evaluation rejected'),
    ),
  )
  try {
    info = await command({ action: 'stop', recordingId: info.id })
    assert.equal(info.phase, 'complete')
    recordings.push(info)
  } finally {
    abort.abort(new Error('Saturation proof complete'))
    await Promise.all(evaluations)
  }
  checks.push('native-stop-reserved-admission-under-64-pending-evaluations')

  const canceled = await command({
    action: 'start',
    ffmpegPath: ffmpeg,
    outputPath: join(directory, 'canceled.webm'),
    framesPerSecond: 120,
    width: 160,
    height: 96,
    audio: true,
  })
  const cancel = await command({ action: 'cancel', recordingId: canceled.id })
  assert.equal(cancel.phase, 'failed')
  assert.match(cancel.failure, /canceled/)
  await assert.rejects(access(cancel.outputPath))
  checks.push('actual-native-cancel-no-committed-output')

  let encoderFailure
  try {
    const failed = await command({
      action: 'start',
      ffmpegPath: '/bin/false',
      outputPath: join(directory, 'failed-encoder.webm'),
      framesPerSecond: 120,
      width: 160,
      height: 96,
      audio: false,
    })
    await command({ action: 'stop', recordingId: failed.id })
  } catch (error) {
    encoderFailure = error
  }
  assert(encoderFailure instanceof Error, 'failed physical encoder was reported as successful')
  assert(encoderFailure.message.includes('Native encoder'), encoderFailure.message)
  await assert.rejects(access(join(directory, 'failed-encoder.webm')))
  checks.push('actual-encoder-failure-controlled-without-browser-failure')
  const settled = z
    .object({ nativeRecordingPhysical: z.literal(0) })
    .parse(await source.runAutomation('diagnostics'))
  await writeFile(
    join(directory, 'settled-native-diagnostics.json'),
    JSON.stringify(settled, null, 2),
  )
  checks.push('native-physical-recording-ownership-zero-after-stop-cancel-and-encoder-failure')

  info = await command({
    action: 'start',
    ffmpegPath: ffmpeg,
    outputPath: join(directory, 'graceful-page-close.webm'),
    framesPerSecond: 120,
    width: 160,
    height: 96,
    audio: true,
  })
  counter = z.number().parse(await page.evaluate('globalThis.mediaFrames'))
  await page.waitForFunction(`globalThis.mediaFrames >= ${counter + 30}`)
  let terminal
  const detach = source.onRecordingMessage?.((value) => {
    if (value.id === info.id && value.phase === 'complete') terminal = value
  })
  await page.close()
  detach?.()
  assert(terminal, 'graceful page closure did not publish actual completed recording')
  await verifyFile(terminal, true)
  recordings.push(terminal)
  checks.push('graceful-page-close-awaits-real-encoder-commit')
  await source.close()
  closingSource = await launchNativeBrowser({
    executablePath: executable,
    width: 160,
    height: 96,
    framesPerSecond: 120,
    headless: true,
  })
  const closingPage = createNativePage(closingSource)
  await closingPage.setContent(`<style>body{margin:0;overflow:hidden}</style><canvas width=160 height=96></canvas><script>
    globalThis.mediaFrames=0;const ctx=document.querySelector('canvas').getContext('2d');
    function draw(){ctx.fillStyle=(Math.floor(mediaFrames/15)%2)?'#2244ee':'#ee2244';ctx.fillRect(0,0,160,96);mediaFrames++;requestAnimationFrame(draw)}draw();
  </script>`)
  let browserTerminal
  const detachBrowser = closingSource.onRecordingMessage?.((value) => {
    if (value.phase === 'complete' || value.phase === 'failed') browserTerminal = value
  })
  try {
    const captured = await recordNative(
      'actual graceful browser closure',
      async ({ source: live, page: capturedPage }) => {
        const initial = z.number().parse(await capturedPage.evaluate('globalThis.mediaFrames'))
        await capturedPage.waitForFunction(`globalThis.mediaFrames >= ${initial + 45}`)
        await live.close()
      },
      {
        source: closingSource,
        ffmpegPath: ffmpeg,
        capture: { size: { width: 160, height: 96 }, framesPerSecond: 120, audio: false },
        output: {
          directory: join(directory, 'browser-close'),
          manifestPath: join(directory, 'browser-close-manifest.json'),
        },
      },
    )
    assert.equal(captured.manifest.status, 'passed')
    assert(browserTerminal, 'browser EOF did not publish the real encoder completion')
    await verifyFile(browserTerminal, false)
    recordings.push(browserTerminal)
    assert.equal(closingSource.state, 'closed')
    assert.equal(closingSource.processExit?.()?.code, 0)
    checks.push('recordNative-flow-browser-close-awaits-native-EOF-commit-and-manifest')
  } finally {
    detachBrowser?.()
    await closingSource.close().catch(() => undefined)
  }
  const forced = new AbortController()
  forcedSource = await launchNativeBrowser({
    executablePath: executable,
    width: 160,
    height: 96,
    framesPerSecond: 120,
    headless: true,
    signal: forced.signal,
  })
  const forcedPage = createNativePage(forcedSource)
  await forcedPage.setContent(`<canvas width=160 height=96></canvas><script>
    globalThis.mediaFrames=0;const ctx=document.querySelector('canvas').getContext('2d');
    function draw(){ctx.fillStyle=(mediaFrames%30<15)?'#2244ee':'#ee2244';ctx.fillRect(0,0,160,96);mediaFrames++;requestAnimationFrame(draw)}draw();
  </script>`)
  await assert.rejects(
    recordNative(
      'actual forced native parent shutdown',
      async ({ source: live, page: capturedPage }) => {
        const initial = z.number().parse(await capturedPage.evaluate('globalThis.mediaFrames'))
        await capturedPage.waitForFunction(`globalThis.mediaFrames >= ${initial + 20}`)
        forced.abort(new Error('Forced native shutdown verification'))
        await live.closed
      },
      {
        source: forcedSource,
        ffmpegPath: ffmpeg,
        capture: { size: { width: 160, height: 96 }, framesPerSecond: 120, audio: false },
        output: {
          directory: join(directory, 'forced-shutdown'),
          manifestPath: join(directory, 'forced-shutdown-manifest.json'),
        },
      },
    ),
    (error) => {
      assert(error instanceof AggregateError)
      assert(
        error.errors.some(
          (value) => value instanceof Error && value.message.includes('Native browser aborted'),
        ),
        'SDK lost the original native shutdown failure',
      )
      return true
    },
  )
  await forcedSource.closed
  assert.equal(forcedSource.processExit?.()?.signal, 'SIGKILL')
  assert(
    !(await readdir(join(directory, 'forced-shutdown'))).some((name) => name.endsWith('.webm')),
  )
  checks.push('recordNative-forced-native-SIGKILL-preserves-original-failure-and-no-artifact')
  assert(!(await readdir(directory)).some((name) => name.startsWith('.suitecut-media-')))
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256: await hash(executable),
        verifierSHA256: await hash(resolve(z.string().parse(process.argv[1]))),
        cefSHA256: await hash(join(dirname(executable), 'libcef.so')),
        checks,
        recordings,
        sourceCaptureVerified: true,
      },
      null,
      2,
    ),
  )
  console.log('Native recording pipeline actual Source capture/lifecycle proof passed')
} catch (error) {
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      { completed: false, checks, error: error instanceof Error ? error.stack : String(error) },
      null,
      2,
    ),
  )
  throw error
} finally {
  await source.close().catch(() => undefined)
  await closingSource?.close().catch(() => undefined)
  await forcedSource?.close().catch(() => undefined)
}
