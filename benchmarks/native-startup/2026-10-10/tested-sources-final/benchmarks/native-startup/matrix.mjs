import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { join, resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'

const directory = resolve(process.env.STARTUP_DIRECTORY)
await mkdir(directory, { recursive: true })
const fixture = await readFile('benchmarks/obs-cef/fixture.html')
const server = createServer((_req, res) => res.end(fixture))
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const definitions = [
  ['cold-immediate-pcm', 'immediate', 'pcm_s16le', 6000, false],
  ['cold-packet-pcm', 'wait-for-packet', 'pcm_s16le', 6000, false],
  ['cold-signal-pcm', 'wait-for-signal', 'pcm_s16le', 6000, false],
  ['cold-signal-aac', 'wait-for-signal', 'aac', 6000, false],
  ['cold-signal-aac-mp4', 'wait-for-signal', 'aac', 6000, false],
  ['quiet-signal-aac', 'wait-for-signal', 'aac', 20000, false],
  ['stress-signal-aac', 'wait-for-signal', 'aac', 20000, true],
]
if (process.env.STARTUP_LABEL_FILTER) {
  const selected = definitions.filter(([label]) =>
    process.env.STARTUP_LABEL_FILTER.split(',').includes(label),
  )
  definitions.splice(0, definitions.length, ...selected)
}
const summary = process.env.STARTUP_APPEND
  ? JSON.parse(await readFile(join(directory, 'matrix.json'), 'utf8'))
  : { startedAt: new Date().toISOString(), cases: [] }
let engine, sampler
try {
  for (const [label, audioStartup, audioCodec, duration, stall] of definitions) {
    const caseDirectory = join(directory, label)
    await mkdir(caseDirectory, { recursive: true })
    process.env.SUITECUT_AUDIO_TRACE_DIRECTORY = caseDirectory
    engine = await launchEngine({
      executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
      width: 1920,
      height: 1080,
      framesPerSecond: 30,
    })
    summary.cefVersion = engine.source.cefVersion
    sampler = spawn(
      'python3',
      [
        'benchmarks/obs-cef/profile-pipeline.py',
        join(caseDirectory, 'processes.jsonl'),
        String(process.pid),
      ],
      { stdio: 'ignore' },
    )
    const page = engine.browser.contexts()[0].pages()[0]
    await page.goto(`http://127.0.0.1:${server.address().port}`)
    await page.evaluate(() => window.activate('canvas'))
    const audioState = await page.evaluate(() => window.startAudio())
    assert.equal(audioState, 'running')
    const destination = join(
      caseDirectory,
      `${audioCodec}.${label.endsWith('-mp4') ? 'mp4' : 'mkv'}`,
    )
    const requested = performance.now()
    const output = await engine.record(page, { destination, audioCodec, audioStartup })
    const readyLatencyMs = performance.now() - requested
    const initial = await output.status()
    const captureStartedMonotonic = Number(process.hrtime.bigint()) / 1e9
    const began = performance.now()
    let blocked = false,
      nextScroll = 1000
    const samples = []
    while (performance.now() - began < duration) {
      const elapsed = performance.now() - began
      if (stall && !blocked && elapsed > 5000) {
        blocked = true
        const until = performance.now() + 4000
        while (performance.now() < until) {}
      }
      if (elapsed > nextScroll) {
        await page.mouse.wheel(0, nextScroll % 8000 < 4000 ? 650 : -650)
        nextScroll += 2000
      }
      samples.push({ atMs: performance.now() - began, status: await output.status() })
      await delay(250)
    }
    const final = await output.stop()
    assert.equal(final.error, '')
    const entry = {
      cefVersion: engine.source.cefVersion,
      label,
      audioStartup,
      audioCodec,
      audioState,
      destination,
      readyLatencyMs,
      initial,
      final,
      captureStartedMonotonic,
      captureEndedMonotonic: Number(process.hrtime.bigint()) / 1e9,
      requestedDurationMs: duration,
      stallMs: stall ? 4000 : 0,
      samples,
    }
    await engine.close()
    engine = undefined
    sampler.kill('SIGTERM')
    sampler = undefined
    await writeFile(
      join(caseDirectory, 'results.json'),
      JSON.stringify({ cases: [entry] }, null, 2),
    )
    summary.cases.push(entry)
    await writeFile(join(directory, 'matrix.json'), JSON.stringify(summary, null, 2))
    console.log(
      `${label}: ready=${readyLatencyMs.toFixed(1)}ms missing=${(final.missingAudioSamples / 48).toFixed(3)}ms`,
    )
  }
} finally {
  await engine?.close()
  sampler?.kill('SIGTERM')
  server.closeAllConnections()
  await new Promise((r) => server.close(r))
  summary.finishedAt = new Date().toISOString()
  await writeFile(join(directory, 'matrix.json'), JSON.stringify(summary, null, 2))
}
