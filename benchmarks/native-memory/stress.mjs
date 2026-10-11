import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { createWriteStream } from 'node:fs'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { createServer as socketServer } from 'node:net'
import { join, resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'

const directory = resolve(process.env.MEMORY_DIRECTORY)
await mkdir(directory, { recursive: true })
const duration = Number(process.env.MEMORY_DURATION_MS ?? 1800000)
const fixture = `<!doctype html><body style="height:4000px"><select><option>A</option><option>B</option></select><canvas width="640" height="360"></canvas><script>
const canvas=document.querySelector('canvas'),ctx=canvas.getContext('2d');let n=0;
function draw(){ctx.fillStyle='rgb('+(n++%200)+',50,90)';ctx.fillRect(0,0,640,360);requestAnimationFrame(draw)}draw();
const audio=new AudioContext({sampleRate:48000}),osc=audio.createOscillator(),gain=audio.createGain();gain.gain.value=.1;osc.connect(gain).connect(audio.destination);osc.start();audio.resume();
</script>`
const server = createServer((_req, res) => {
  res.setHeader('content-type', 'text/html')
  res.end(fixture)
})
await new Promise((ready) => server.listen(0, '127.0.0.1', ready))
const url = `http://127.0.0.1:${server.address().port}`
const probe = socketServer()
await new Promise((ready) => probe.listen(0, '127.0.0.1', ready))
const port = probe.address().port
await new Promise((ready) => probe.close(ready))
const destination = `rtmp://127.0.0.1:${port}/live/memory`
const receiver = spawn(
  'ffmpeg',
  [
    '-v',
    'error',
    '-y',
    '-listen',
    '1',
    '-i',
    destination,
    '-c',
    'copy',
    join(directory, 'receiver.flv'),
  ],
  { stdio: ['ignore', 'ignore', 'pipe'] },
)
receiver.stderr.pipe(createWriteStream(join(directory, 'receiver.log')))
const receiverClosed = once(receiver, 'close')
void receiverClosed.catch(() => {})
let engine, record, stream, sampler
const stats = createWriteStream(join(directory, 'output-status.jsonl'))
const result = {
  requestedMs: duration,
  status: 'starting',
  startedAt: new Date().toISOString(),
  viewport: [640, 360],
  fps: 30,
  audio: 'continuous oscillator',
  contextCycles: 0,
  pauseCycles: 0,
  reloads: 0,
  shortRecordingCycles: 0,
  rawSubscribers: false,
}
try {
  const deadline = performance.now() + 5000
  const portHex = port.toString(16).toUpperCase().padStart(4, '0')
  while (
    !(await readFile('/proc/net/tcp', 'utf8')).split('\n').some((line) => {
      const parts = line.trim().split(/\s+/)
      return parts[1]?.endsWith(`:${portHex}`) && parts[3] === '0A'
    })
  ) {
    if (performance.now() > deadline) throw new Error('RTMP receiver startup timeout')
    await delay(20)
  }
  engine = await launchEngine({
    executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
    width: 640,
    height: 360,
    framesPerSecond: 30,
  })
  const page = engine.browser.contexts()[0].pages()[0]
  await page.goto(url)
  await delay(1000)
  record = await engine.record(page, { destination: join(directory, 'recording.mp4') })
  stream = await engine.stream(page, { destination })
  const start = performance.now()
  sampler = spawn(
    'python3',
    [
      'benchmarks/obs-cef/profile-pipeline.py',
      join(directory, 'processes.jsonl'),
      String(process.pid),
    ],
    { stdio: 'ignore' },
  )
  result.status = 'running'
  result.readyAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  let nextCycle = start,
    nextSample = start,
    nextReload = start + 60000
  while (performance.now() - start < duration) {
    await delay(250)
    const now = performance.now()
    if (now >= nextCycle) {
      const context = await engine.browser.newContext()
      try {
        const other = await context.newPage()
        await other.goto(url)
        const session = await context.newCDPSession(other)
        await session.send('Runtime.enable')
        await session.detach()
        await other.mouse.wheel(0, 100)
      } finally {
        await context.close()
      }
      result.contextCycles++
      await record.pause(true)
      await record.pause(false)
      result.pauseCycles++
      if (result.contextCycles % 6 === 0) {
        const short = await engine.record(page, {
          destination: join(directory, 'short.mkv'),
          videoCodec: 'ffv1',
          audioCodec: 'pcm_s16le',
          width: 64,
          height: 36,
        })
        await delay(80)
        const stopped = await short.stop()
        assert.equal(stopped.error, '')
        assert.equal(stopped.queuedAudioBytes + stopped.queuedVideoBytes, 0)
        result.shortRecordingCycles++
      }
      nextCycle = now + 10000
    }
    if (now >= nextReload) {
      await page.reload()
      result.reloads++
      nextReload = now + 60000
    }
    if (now >= nextSample) {
      const outputs = await Promise.all([record.status(), stream.status()])
      for (const output of outputs) {
        assert.equal(output.error, '')
        assert(output.queuedVideoBytes <= 768 * 1024 * 1024)
        assert(output.queuedAudioBytes <= 1920000)
      }
      stats.write(
        JSON.stringify({
          atMs: now - start,
          outputs,
          node: process.memoryUsage(),
          contexts: engine.browser.contexts().length,
        }) + '\n',
      )
      nextSample = now + 1000
    }
  }
  result.elapsedMs = performance.now() - start
  result.final = await Promise.all([record.stop(), stream.stop()])
  for (const output of result.final) {
    assert.equal(output.error, '')
    assert.equal(output.queuedAudioBytes + output.queuedVideoBytes, 0)
  }
  await Promise.race([
    receiverClosed,
    delay(10000).then(() => {
      throw new Error('Receiver finalization timeout')
    }),
  ])
  result.status = 'passed'
} catch (error) {
  result.status = 'failed'
  result.error = error.stack
  throw error
} finally {
  sampler?.kill('SIGTERM')
  await record?.stop().catch(() => {})
  await stream?.stop().catch(() => {})
  await engine?.close()
  if (receiver.exitCode === null) receiver.kill('SIGTERM')
  stats.end()
  server.closeAllConnections()
  await new Promise((ready) => server.close(ready))
  result.finishedAt = new Date().toISOString()
  await writeFile(join(directory, 'result.json'), JSON.stringify(result, null, 2))
  console.log(JSON.stringify(result, null, 2))
}
