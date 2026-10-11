import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createWriteStream } from 'node:fs'
import { readFile, writeFile, mkdir } from 'node:fs/promises'
import { createServer } from 'node:http'
import { createServer as socketServer } from 'node:net'
import { resolve, join } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'
const directory = resolve('benchmarks/native-engine/2026-10-09', process.env.SOAK_LABEL ?? 'soak')
await mkdir(directory, { recursive: true })
await writeFile(
  join(directory, 'browser-tested'),
  await readFile('.suitecut/native-build/suitecut-browser'),
)
const duration = Number(process.env.SOAK_DURATION_MS ?? 1800000)
const fixture = await readFile('benchmarks/obs-cef/fixture.html', 'utf8')
const server = createServer((_req, res) => {
  res.setHeader('content-type', 'text/html')
  res.end(fixture)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const probe = socketServer()
probe.listen(0, '127.0.0.1')
await once(probe, 'listening')
const port = probe.address().port
await new Promise((r) => probe.close(r))
const rtmp = `rtmp://127.0.0.1:${port}/live/native`,
  receiverPath = join(directory, 'receiver.flv'),
  recordingPath = join(directory, 'recording.mp4')
const receiver = spawn(
  'ffmpeg',
  ['-v', 'error', '-y', '-listen', '1', '-i', rtmp, '-c', 'copy', receiverPath],
  { stdio: ['ignore', 'ignore', 'pipe'] },
)
receiver.stderr.pipe(createWriteStream(join(directory, 'receiver.log')))
const closed = once(receiver, 'close')
void closed.catch(() => {})
const samples = createWriteStream(join(directory, 'node-memory.jsonl'))
const result = {
  startedAt: new Date().toISOString(),
  requestedMs: duration,
  width: 1920,
  height: 1080,
  targetFps: 30,
  audio: true,
  recordingPath,
  receiverPath,
  mode: 'C++ libav simultaneous H264 recording + RTMP; Playwright automation over private IPC; no raw media subscribers',
}
result.status = 'starting'
await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
let engine, record, stream, timer, sampler
try {
  sampler = spawn(
    'python3',
    [
      'benchmarks/obs-cef/profile-pipeline.py',
      join(directory, 'processes.jsonl'),
      String(process.pid),
    ],
    { stdio: 'ignore' },
  )
  engine = await launchEngine({
    executablePath:
      process.env.SUITECUT_NATIVE_EXECUTABLE ?? resolve('.suitecut/native-build/suitecut-browser'),
    width: 1920,
    height: 1080,
    framesPerSecond: 30,
  })
  const page = engine.browser.contexts()[0].pages()[0]
  await page.goto(`http://127.0.0.1:${server.address().port}`)
  await page.evaluate(() => window.activate('canvas'))
  await page.evaluate(async () => {
    const canvas = document.createElement('canvas')
    canvas.width = 64
    canvas.height = 64
    Object.assign(canvas.style, { position: 'fixed', left: '400px', top: '0px', zIndex: 1000 })
    document.body.append(canvas)
    const ctx = canvas.getContext('2d')
    const audio = new AudioContext({ sampleRate: 48000 })
    await audio.resume()
    const osc = audio.createOscillator(),
      gain = audio.createGain()
    osc.frequency.value = 440
    gain.gain.value = 0
    osc.connect(gain).connect(audio.destination)
    osc.start()
    let next = Math.ceil(audio.currentTime) + 1
    setInterval(() => {
      while (next < audio.currentTime + 2) {
        gain.gain.setValueAtTime(0.15, next)
        gain.gain.setValueAtTime(0, next + 0.25)
        next++
      }
    }, 50)
    function draw() {
      ctx.fillStyle = audio.currentTime % 1 < 0.25 ? 'white' : 'black'
      ctx.fillRect(0, 0, 64, 64)
      requestAnimationFrame(draw)
    }
    draw()
  })
  await delay(2000)
  record = await engine.record(page, { destination: recordingPath })
  stream = await engine.stream(page, { destination: rtmp, bitrateKbps: 4500 })
  const start = performance.now()
  let scrolls = 0,
    lastScroll = start,
    injected = false
  result.readyAt = new Date().toISOString()
  result.baseline = process.memoryUsage()
  result.status = 'running'
  result.runStartMonotonicSeconds = parseFloat(await readFile('/proc/uptime', 'utf8'))
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  timer = setInterval(
    () =>
      samples.write(
        JSON.stringify({
          atMs: performance.now() - start,
          epochMs: Date.now(),
          monotonicMs: performance.now(),
          ...process.memoryUsage(),
          cpu: process.cpuUsage(),
        }) + '\n',
      ),
    1000,
  )
  while (performance.now() - start < duration) {
    await delay(250)
    if (!injected && performance.now() - start > 40000) {
      injected = true
      result.injection = { atMs: performance.now() - start, durationMs: 4000 }
      Atomics.wait(new Int32Array(new SharedArrayBuffer(4)), 0, 0, 4000)
    }
    if (performance.now() - lastScroll > 5000) {
      await page.mouse.move(600, 400)
      await page.mouse.wheel(0, scrolls++ % 40 < 20 ? 180 : -180)
      lastScroll = performance.now()
    }
    if (scrolls && scrolls % 12 === 0) {
      result.latest = { record: await record.status(), stream: await stream.status() }
      await writeFile(
        join(directory, 'progress.json'),
        JSON.stringify({ atMs: performance.now() - start, scrolls, ...result.latest }, null, 2),
      )
      await writeFile(
        join(directory, 'result.json'),
        JSON.stringify(
          { ...result, activeMs: performance.now() - start, scrollCommands: scrolls },
          null,
          2,
        ),
      )
    }
  }
  result.activeMs = performance.now() - start
  result.scrollCommands = scrolls
  result.snapshot = await page.evaluate(() => window.snapshot())
  result.beforeStop = process.memoryUsage()
  result.recordStatus = await record.stop()
  record = undefined
  result.streamStatus = await stream.stop()
  stream = undefined
  assert.equal(result.recordStatus.droppedAudioSamples, 0)
  assert.equal(result.streamStatus.droppedAudioSamples, 0)
  result.status = 'passed'
} catch (error) {
  result.status = 'failed'
  result.error = String(error.stack ?? error)
  process.exitCode = 1
} finally {
  clearInterval(timer)
  await record?.stop().catch(() => {})
  await stream?.stop().catch(() => {})
  await engine?.close()
  receiver.kill('SIGINT')
  await Promise.race([closed, delay(5000)])
  if (receiver.exitCode === null) receiver.kill('SIGKILL')
  sampler?.kill('SIGTERM')
  samples.end()
  server.closeAllConnections()
  server.close()
  result.afterCleanup = process.memoryUsage()
  globalThis.gc?.()
  await delay(100)
  globalThis.gc?.()
  result.afterCleanupGc = process.memoryUsage()
  result.completedAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  console.log(JSON.stringify(result, null, 2))
}
