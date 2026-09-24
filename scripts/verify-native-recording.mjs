/* global document, scrollY */
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { pathToFileURL } from 'node:url'
import { promisify } from 'node:util'

import * as z from 'zod'

import { launchNativeBrowser, recordNative } from '../dist/native.js'
import { resolveFfmpeg, resolveFfprobe } from '../dist/process.js'
import { renderSuiteCut } from '../dist/render.js'

const executablePath = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executablePath) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the built CEF executable')
const directory = resolve('.suitecut/native-recording-verification')
await mkdir(directory, { recursive: true })
const plugin = resolve(directory, 'delayed-tone.mjs')
await writeFile(
  plugin,
  `import { writeFile } from 'node:fs/promises';
import { setTimeout } from 'node:timers/promises';
import { encodePcm16Wav } from ${JSON.stringify(pathToFileURL(resolve('dist/audio-plugin.js')).href)};
export default { async synthesize({outputPath}) {
  await setTimeout(1800);
  const samples=Float32Array.from({length:24000},(_,i)=>.2*Math.sin(2*Math.PI*440*i/24000));
  await writeFile(outputPath,encodePcm16Wav(samples,24000));
} }`,
)
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  const frequency = request.url === '/second' ? 880 : 660
  response.end(`<!doctype html><meta charset="utf-8"><style>
  body{margin:0;background:${request.url === '/second' ? '#167546' : '#17354b'};color:white;font:28px system-ui;padding:35px}
  button,input{font:24px system-ui;padding:12px} #target{background:#cf643a;padding:25px;width:320px;margin-top:25px}
  </style><h1>Native SuiteCut recording</h1><input placeholder="Type here"><button onclick="document.querySelector('#target').textContent='Native click passed'">Continue</button>
  <div id="target">Native presentation target</div><div style="height:1000px"></div><p id="bottom">Scroll verified</p>
  <script>
  const audio=new AudioContext({sampleRate:48000}),osc=audio.createOscillator(),gain=audio.createGain();
  osc.frequency.value=${frequency};gain.gain.value=.08;osc.connect(gain).connect(audio.destination);osc.start();
  </script>`)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const url = `http://127.0.0.1:${address.port}`
const sources = []
try {
  const source = await launchNativeBrowser({
    executablePath,
    width: 960,
    height: 540,
    framesPerSecond: 60,
    pixelFormat: 'i420',
  })
  sources.push(source)
  const second = await launchNativeBrowser({
    executablePath,
    width: 640,
    height: 360,
    framesPerSecond: 30,
    pixelFormat: 'bgra',
  })
  sources.push(second)
  await second.navigate(`${url}/second`)
  let callbackWallMs = 0
  const result = await recordNative(
    'Native presentation integration',
    async ({ page, suitecut, addSource }) => {
      const started = performance.now()
      await page.goto(url)
      await suitecut.type(page.locator('input'), 'Native input', { delayMs: 20 })
      assert.equal(
        await page.evaluate(() => document.querySelector('input')?.value),
        'Native input',
      )
      await suitecut.click(page.locator('button'), { moveDurationMs: 100 })
      assert.equal(
        await page.evaluate(() => document.querySelector('#target')?.textContent),
        'Native click passed',
      )
      await suitecut.highlight(page.locator('#target'), { durationMs: 500 })
      await suitecut.zoom(page.locator('#target'), { scale: 1.2, holdMs: 400 })
      await suitecut.narrate('Native narration and captions share the recording timeline.', {
        provider: 'test-tone',
      })
      await suitecut.checkpoint('Main checkpoint', { durationMs: 300 })
      await suitecut.scrollTo(page.locator('#bottom'), { behavior: 'auto', settleMs: 0 })
      assert(await page.evaluate(() => scrollY > 0))
      await suitecut.scrollTop({ behavior: 'auto', settleMs: 0 })
      const next = await addSource(second)
      suitecut.selectPage(next)
      await suitecut.hold(600)
      await suitecut.checkpoint('Second checkpoint', { durationMs: 300 })
      suitecut.selectPage(page)
      await suitecut.hold(400)
      callbackWallMs = performance.now() - started
    },
    {
      source,
      capture: { size: { width: 960, height: 540 }, narrationTailMs: 50, audio: true },
      audioPlugins: [{ provider: 'test-tone', module: pathToFileURL(plugin).href }],
      output: { directory, manifestPath: resolve(directory, 'manifest.json') },
    },
  )
  assert.equal(source.state, 'running', 'Recording must not close caller-owned sources')
  assert.equal(second.state, 'running')
  const attempt = result.manifest.tests[0]?.attempts[0]
  assert(attempt)
  assert.equal(attempt.status, 'passed')
  assert.equal(attempt.videoTiming.length, 2)
  assert.equal(attempt.artifacts.filter((item) => item.role === 'checkpoint').length, 2)
  assert.equal(attempt.artifacts.filter((item) => item.role === 'narration-audio').length, 1)
  const sourceArtifacts = attempt.artifacts.filter((item) => item.role === 'source-video')
  assert.equal(sourceArtifacts.length, 2)
  for (const artifact of sourceArtifacts) {
    const media = attempt.media.find((item) => item.artifactId === artifact.id)
    assert(media?.streams.some((stream) => stream.kind === 'audio'))
  }
  for (const type of ['highlight', 'zoom', 'narration', 'checkpoint', 'page-selected'])
    assert(attempt.events.some((event) => event.type === type))
  assert(
    callbackWallMs - attempt.durationMs > 1500,
    'Synthesis preparation must be excluded from the recording clock',
  )
  const outputPath = resolve(directory, 'native-presentation.mp4')
  await renderSuiteCut({
    manifestPath: result.manifestPath,
    outputPath,
    config: { output: { width: 960, height: 540, framesPerSecond: 60 }, resultHoldMs: 0 },
  })
  const ffmpeg = await resolveFfmpeg()
  const ffprobe = await resolveFfprobe(ffmpeg)
  const execute = promisify(execFile)
  const probe = await execute(ffprobe, ['-v', 'error', '-show_streams', '-of', 'json', outputPath])
  const streams = z
    .object({
      streams: z.array(
        z.object({
          codec_type: z.string(),
          width: z.number().optional(),
          height: z.number().optional(),
          avg_frame_rate: z.string().optional(),
        }),
      ),
    })
    .parse(JSON.parse(probe.stdout)).streams
  assert(
    streams.some(
      (stream) =>
        stream.codec_type === 'video' &&
        stream.width === 960 &&
        stream.height === 540 &&
        stream.avg_frame_rate === '60/1',
    ),
  )
  assert(streams.some((stream) => stream.codec_type === 'audio'))
  const pcmPath = resolve(directory, 'audio.pcm')
  await execute(ffmpeg, [
    '-v',
    'error',
    '-i',
    outputPath,
    '-vn',
    '-f',
    's16le',
    '-ac',
    '1',
    '-ar',
    '48000',
    '-y',
    pcmPath,
  ])
  const pcm = await readFile(pcmPath)
  let audible = 0
  for (let i = 0; i < pcm.length; i += 2) if (Math.abs(pcm.readInt16LE(i)) > 500) audible++
  assert(audible > 10_000, 'Rendered page audio and narration must be audible')
  const narration = attempt.events.find((event) => event.type === 'narration')
  assert(narration)
  const selections = attempt.events
    .filter((event) => event.type === 'page-selected')
    .sort((left, right) => left.atMs - right.atMs)
  assert.equal(selections.length, 2)
  const toneAmplitude = (startMs, durationMs, frequency) => {
    const start = Math.max(0, Math.round((startMs * 48_000) / 1000))
    const count = Math.min(
      Math.round((durationMs * 48_000) / 1000),
      Math.floor(pcm.length / 2) - start,
    )
    let sine = 0
    let cosine = 0
    for (let index = 0; index < count; index++) {
      const sample = pcm.readInt16LE((start + index) * 2)
      const angle = (2 * Math.PI * frequency * index) / 48_000
      sine += sample * Math.sin(angle)
      cosine += sample * Math.cos(angle)
    }
    return (2 * Math.hypot(sine, cosine)) / count
  }
  const second660 = toneAmplitude(selections[0].atMs + 150, 200, 660)
  const second880 = toneAmplitude(selections[0].atMs + 150, 200, 880)
  const returned660 = toneAmplitude(selections[1].atMs + 100, 200, 660)
  const returned880 = toneAmplitude(selections[1].atMs + 100, 200, 880)
  assert(second880 > second660 * 3, 'Rendered page audio must follow selection to source two')
  assert(returned660 > returned880 * 3, 'Rendered page audio must return to the main source')
  const narration440 = toneAmplitude(narration.atMs + 300, 200, 440)
  const narration660 = toneAmplitude(narration.atMs + 300, 200, 660)
  assert(narration440 > 1_000, 'Rendered narration tone must remain audible')
  assert(narration660 > 300, 'Page audio must remain audible while narration is mixed')
  await execute(ffmpeg, [
    '-v',
    'error',
    '-ss',
    String((narration.atMs + 300) / 1000),
    '-i',
    outputPath,
    '-frames:v',
    '1',
    '-y',
    resolve(directory, 'caption.png'),
  ])
  const defaultAudioResult = await recordNative(
    'Default page audio disabled',
    async ({ suitecut }) => suitecut.hold(250),
    {
      source,
      output: {
        directory,
        manifestPath: resolve(directory, 'default-audio-disabled.json'),
      },
    },
  )
  const defaultAttempt = defaultAudioResult.manifest.tests[0]?.attempts[0]
  assert(defaultAttempt)
  const defaultSource = defaultAttempt.artifacts.find((item) => item.role === 'source-video')
  assert(defaultSource)
  const defaultMedia = defaultAttempt.media.find((item) => item.artifactId === defaultSource.id)
  assert(defaultMedia)
  assert(
    !defaultMedia.streams.some((stream) => stream.kind === 'audio'),
    'Native recording audio must be disabled by default',
  )
  const failureManifest = resolve(directory, 'callback-failure.json')
  await assert.rejects(
    recordNative(
      'Expected callback failure',
      async ({ suitecut }) => {
        await suitecut.hold(100)
        throw new Error('Deliberate callback failure')
      },
      { source, output: { directory, manifestPath: failureManifest } },
    ),
    /failed/,
  )
  const failed = z
    .object({ status: z.string() })
    .parse(JSON.parse(await readFile(failureManifest, 'utf8')))
  assert.equal(failed.status, 'failed')
  const abortController = new AbortController()
  await assert.rejects(
    recordNative(
      'Expected cancellation',
      async ({ suitecut, page }) => {
        await suitecut.hold(100)
        abortController.abort(new Error('Deliberate recording cancellation'))
        await page.waitForTimeout(30_000)
      },
      {
        source,
        signal: abortController.signal,
        output: { directory, manifestPath: resolve(directory, 'cancelled.json') },
      },
    ),
    /failed/,
  )
  assert.equal(source.state, 'running')
  const report = {
    recording: 'passed',
    callbackFailure: 'passed',
    cancellation: 'passed',
    nativeInput: 'passed',
    presentation: 'passed',
    checkpoints: 2,
    sources: 2,
    outputFps: 60,
    audibleSamples: audible,
    pageAudioSelection: 'passed',
    narrationMix: 'passed',
    defaultAudioDisabled: 'passed',
    excludedPreparationMs: Math.round(callbackWallMs - attempt.durationMs),
    sourceOwnership: 'passed',
    outputPath,
  }
  await writeFile(resolve(directory, 'report.json'), JSON.stringify(report, null, 2))
  console.log(report)
} finally {
  await Promise.allSettled(sources.map((source) => source.close()))
  server.closeAllConnections()
  server.close()
}
