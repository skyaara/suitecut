import { launchEngine } from '../../dist/engine.js'
import { createServer } from 'node:http'
import { once } from 'node:events'
import { readFile, mkdir, writeFile } from 'node:fs/promises'
import { resolve, join } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { spawn } from 'node:child_process'
import { setPriority } from 'node:os'

const directory = resolve(process.env.SUITECUT_AUDIO_TRACE_DIRECTORY)
await mkdir(directory, { recursive: true })
const fixture = await readFile('benchmarks/obs-cef/fixture.html')
const server = createServer((req, res) => res.end(fixture))
server.listen(0, '127.0.0.1')
await once(server, 'listening')
let engine, sampler
const results = { startedAt: new Date().toISOString(), cases: [] }
try {
  engine = await launchEngine({
    executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
    width: Number(process.env.AUDIO_WIDTH ?? 1920),
    height: Number(process.env.AUDIO_HEIGHT ?? 1080),
    framesPerSecond: 30,
  })
  sampler = spawn(
    'python3',
    [
      'benchmarks/obs-cef/profile-pipeline.py',
      join(directory, 'processes.jsonl'),
      String(process.pid),
    ],
    { stdio: 'ignore' },
  )
  // Change only the controller after spawning CEF, for a scheduling control.
  if (process.env.AUDIO_CONTROLLER_NICE)
    setPriority(process.pid, Number(process.env.AUDIO_CONTROLLER_NICE))
  results.controllerNice = Number(process.env.AUDIO_CONTROLLER_NICE ?? 0)
  results.stall = !process.env.AUDIO_NO_STALL
  results.warmupMs = Number(process.env.AUDIO_WARMUP_MS ?? 0)
  const page = engine.browser.contexts()[0].pages()[0]
  await page.goto(`http://127.0.0.1:${server.address().port}`)
  await page.evaluate(() => window.activate('canvas'))
  results.audioState = await page.evaluate(() => window.startAudio())
  await delay(Number(process.env.AUDIO_WARMUP_MS ?? 0))
  for (const audioCodec of ['pcm_s16le', 'aac']) {
    const destination = join(directory, `${audioCodec}.mkv`)
    const output = await engine.record(page, { destination, audioCodec })
    while (!(await output.status()).ready) await delay(20)
    const start = performance.now()
    let stalled = false,
      nextScroll = 1000
    const duration = Number(process.env.AUDIO_DURATION_MS ?? 30000)
    while (performance.now() - start < duration) {
      const elapsed = performance.now() - start
      if (!process.env.AUDIO_NO_STALL && !stalled && elapsed > 5000) {
        stalled = true
        const stall = performance.now()
        while (performance.now() - stall < 4000) {}
      }
      if (elapsed > nextScroll) {
        await page.mouse.wheel(0, nextScroll % 8000 < 4000 ? 650 : -650)
        nextScroll += 2000
      }
      await delay(100)
    }
    results.cases.push({ audioCodec, destination, final: await output.stop() })
    await page.evaluate(() => scrollTo(0, 0))
    console.log(audioCodec, JSON.stringify(results.cases.at(-1).final))
  }
} finally {
  await engine?.close()
  sampler?.kill('SIGTERM')
  server.closeAllConnections()
  await new Promise((r) => server.close(r))
  results.finishedAt = new Date().toISOString()
  await writeFile(join(directory, 'results.json'), JSON.stringify(results, null, 2))
}
