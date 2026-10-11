import assert from 'node:assert/strict'
import { spawn, execFile } from 'node:child_process'
import { once } from 'node:events'
import { createWriteStream } from 'node:fs'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { createServer as socketServer } from 'node:net'
import { resolve, join } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { promisify } from 'node:util'
import { launchEngine } from '../../dist/engine.js'

const directory = resolve(process.env.CODEC_DIRECTORY ?? 'benchmarks/native-codecs/2026-10-10')
await mkdir(join(directory, 'captures'), { recursive: true })
await mkdir(join(directory, 'renders'), { recursive: true })
const fixture = await readFile('benchmarks/obs-cef/fixture.html')
const server = createServer((_req, res) => {
  res.setHeader('content-type', 'text/html')
  res.end(fixture)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const cases = [
  ['libx264', 'aac', 'mp4', 'h264'],
  ['libx265', 'aac', 'mp4', 'hevc'],
  ['libvpx', 'libopus', 'webm', 'vp8'],
  ['libvpx-vp9', 'libopus', 'webm', 'vp9'],
  ['libsvtav1', 'aac', 'mp4', 'av1'],
  ['libaom-av1', 'aac', 'mp4', 'av1'],
  ['ffv1', 'flac', 'mkv', 'ffv1'],
  ['ffv1', 'pcm_s16le', 'mkv', 'ffv1'],
]
if (process.env.CODEC_FILTER) {
  const selected = cases.filter(([video]) => process.env.CODEC_FILTER.split(',').includes(video))
  cases.splice(0, cases.length, ...selected)
}
const result = {
  startedAt: new Date().toISOString(),
  captureSize: [1920, 1080],
  fps: 30,
  cases: [],
  negative: [],
  hardware: [],
}
const save = () => writeFile(join(directory, 'matrix.json'), JSON.stringify(result, null, 2))
const execute = promisify(execFile)
const probe = async (path) =>
  JSON.parse(
    (
      await execute('ffprobe', [
        '-v',
        'error',
        '-show_streams',
        '-show_format',
        '-of',
        'json',
        path,
      ])
    ).stdout,
  )
let engine, sampler, receiver, receiverDone
try {
  engine = await launchEngine({
    executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
    width: 1920,
    height: 1080,
    framesPerSecond: 30,
  })
  result.encoders = await engine.encoders()
  assert.equal(result.encoders.length, 14)
  await writeFile(join(directory, 'encoders.json'), JSON.stringify(result.encoders, null, 2))
  const page = engine.browser.contexts()[0].pages()[0]
  await page.goto(`http://127.0.0.1:${server.address().port}`)
  await page.evaluate(() => window.activate('canvas'))
  await page.getByLabel('Text entry').fill('Native multi-codec capture')
  await page.locator('#click').click()
  await page.locator('#choice').selectOption({ label: 'Second' })
  assert.equal(await page.evaluate(() => window.startAudio()), 'running')
  sampler = spawn(
    'python3',
    [
      'benchmarks/obs-cef/profile-pipeline.py',
      join(directory, 'processes.jsonl'),
      String(process.pid),
    ],
    { stdio: 'ignore' },
  )
  for (const [videoCodec, audioCodec, container, decodedCodec] of cases) {
    const entry = {
      videoCodec,
      audioCodec,
      container,
      decodedCodec,
      samples: [],
      status: 'running',
    }
    result.cases.push(entry)
    entry.startMonotonic = Number((await readFile('/proc/uptime', 'utf8')).split(' ')[0])
    entry.startUptimeMs = performance.now()
    entry.startAt = new Date().toISOString()
    const cpu = process.cpuUsage()
    const started = performance.now()
    entry.capturePath = join(directory, 'captures', `${videoCodec}-${audioCodec}.${container}`)
    const output = await engine.record(page, {
      destination: entry.capturePath,
      videoCodec,
      audioCodec,
      container,
    })
    while (!(await output.status()).ready) await delay(20)
    entry.readyMs = performance.now() - started
    const captureStart = performance.now()
    const duration = Number(process.env.CODEC_DURATION_MS ?? 20000)
    let stalled = false,
      nextScroll = 1000
    while (performance.now() - captureStart < duration) {
      const elapsed = performance.now() - captureStart
      if (!stalled && elapsed > 5000) {
        stalled = true
        const start = performance.now()
        const stallCpu = process.cpuUsage()
        while (performance.now() - start < 4000) {
          /* Block SDK only; C++ keeps encoding. */
        }
        entry.nodeStallMs = performance.now() - start
        entry.nodeStallCpu = process.cpuUsage(stallCpu)
      }
      if (elapsed > nextScroll) {
        await page.mouse.wheel(0, nextScroll % 8000 < 4000 ? 650 : -650)
        nextScroll += 4000
      }
      entry.samples.push({
        atMs: performance.now() - captureStart,
        memory: process.memoryUsage(),
        native: await output.status(),
      })
      await delay(1000)
    }
    const stopStart = performance.now()
    entry.final = await output.stop()
    entry.stopMs = performance.now() - stopStart
    assert.deepEqual(await output.stop(), entry.final)
    assert.equal(entry.final.error, '')
    assert.equal(entry.final.droppedAudioSamples, 0)
    assert.equal(entry.final.queuedAudioBytes + entry.final.queuedVideoBytes, 0)
    entry.wallMs = performance.now() - started
    entry.nodeCpu = process.cpuUsage(cpu)
    entry.captureProbe = await probe(entry.capturePath)
    assert.equal(
      entry.captureProbe.streams.find((s) => s.codec_type === 'video').codec_name,
      decodedCodec,
    )
    entry.endAt = new Date().toISOString()
    entry.endMonotonic = Number((await readFile('/proc/uptime', 'utf8')).split(' ')[0])
    entry.status = 'captured'
    await save()
    console.log(
      `${videoCodec}/${audioCodec}: ${entry.final.videoFrames} captured; repeated=${entry.final.repeatedFrames}; stop=${entry.stopMs.toFixed(1)}ms`,
    )
    await page.evaluate(() => scrollTo(0, 0))
  }
  if (!process.env.CODEC_CAPTURE_ONLY) {
    for (const entry of result.cases) {
      entry.renderPath = join(
        directory,
        'renders',
        `${entry.videoCodec}-${entry.audioCodec}.${entry.container}`,
      )
      const start = performance.now()
      entry.renderReport = await engine.render({
        manifestPath: resolve(
          'benchmarks/native-engine/2026-10-09/presentation-visual-fixed/manifest.json',
        ),
        outputPath: entry.renderPath,
        config: {
          output: {
            width: 640,
            height: 360,
            framesPerSecond: 30,
            videoCodec: entry.videoCodec,
            audioCodec: entry.audioCodec,
            container: entry.container,
            quality: 'standard',
          },
          resultHoldMs: 0,
        },
      })
      entry.renderMs = performance.now() - start
      entry.renderProbe = await probe(entry.renderPath)
      assert.equal(
        entry.renderProbe.streams.find((s) => s.codec_type === 'video').codec_name,
        entry.decodedCodec,
      )
      assert(entry.renderProbe.streams.some((s) => s.codec_type === 'audio'))
      entry.status = 'passed'
      await save()
      console.log(
        `${entry.videoCodec}/${entry.audioCodec}: presentation render ${entry.renderMs.toFixed(1)}ms`,
      )
    }
    for (const config of [
      { destination: join(directory, 'invalid.webm'), videoCodec: 'libx264' },
      { destination: join(directory, 'invalid.mp4'), videoCodec: 'libx265', crf: 64 },
      { destination: join(directory, 'invalid.mkv'), videoCodec: 'ffv1', crf: 18 },
      {
        destination: join(directory, 'invalid-audio.webm'),
        videoCodec: 'libvpx-vp9',
        audioCodec: 'aac',
      },
      { destination: join(directory, 'invalid-null.mp4'), crf: null },
      { destination: join(directory, 'invalid-container.mp4'), container: 'unknown' },
    ]) {
      await assert.rejects(engine.record(page, config), (error) => {
        result.negative.push({ config, error: error.message })
        return true
      })
    }
    await assert.rejects(
      engine.stream(page, { destination: 'rtmp://127.0.0.1:1/live/test', videoCodec: 'libx265' }),
      /RTMP requires/,
    )
    for (const videoCodec of ['h264_nvenc', 'hevc_nvenc', 'av1_nvenc']) {
      await assert.rejects(
        engine.record(page, {
          destination: join(directory, `unavailable-${videoCodec}.mp4`),
          videoCodec,
        }),
        (error) => {
          result.hardware.push({ videoCodec, error: error.message, verifiedOnDevice: false })
          return /Open native .* encoder/.test(error.message)
        },
      )
    }
    // Different recording codec and RTMP codec run concurrently from one CEF page.
    const portProbe = socketServer()
    portProbe.listen(0, '127.0.0.1')
    await once(portProbe, 'listening')
    const port = portProbe.address().port
    await new Promise((r) => portProbe.close(r))
    const rtmp = `rtmp://127.0.0.1:${port}/live/codecs`
    receiver = spawn(
      'ffmpeg',
      [
        '-v',
        'error',
        '-y',
        '-listen',
        '1',
        '-i',
        rtmp,
        '-c',
        'copy',
        join(directory, 'concurrent-receiver.flv'),
      ],
      { stdio: ['ignore', 'ignore', 'pipe'] },
    )
    receiver.stderr.pipe(createWriteStream(join(directory, 'receiver.log')))
    receiverDone = once(receiver, 'close')
    void receiverDone.catch(() => {})
    await delay(500)
    const record = await engine.record(page, {
      destination: join(directory, 'concurrent-vp9.webm'),
      videoCodec: 'libvpx-vp9',
      audioCodec: 'libopus',
    })
    const stream = await engine.stream(page, { destination: rtmp })
    const started = performance.now()
    await delay(5000)
    const stall = performance.now()
    while (performance.now() - stall < 4000) {}
    await page.mouse.wheel(0, 800)
    await delay(11000)
    result.concurrent = {
      wallMs: performance.now() - started,
      record: await record.stop(),
      stream: await stream.stop(),
      recordPath: join(directory, 'concurrent-vp9.webm'),
      receiverPath: join(directory, 'concurrent-receiver.flv'),
    }
    await receiverDone
    for (const status of [result.concurrent.record, result.concurrent.stream]) {
      assert.equal(status.error, '')
      assert.equal(status.droppedAudioSamples, 0)
      assert.equal(status.queuedAudioBytes + status.queuedVideoBytes, 0)
    }
  }
  result.fixture = await page.evaluate(() => window.snapshot())
  assert(result.fixture.scrollEvents > 10)
  assert(result.fixture.clicks === 1)
  assert.equal(result.fixture.value, 'Native multi-codec capture')
  global.gc?.()
  result.finalNodeMemory = process.memoryUsage()
  result.status = 'passed'
} catch (error) {
  result.status = 'failed'
  result.error = String(error.stack ?? error)
  throw error
} finally {
  await engine?.close()
  sampler?.kill('SIGTERM')
  if (receiver?.exitCode === null) receiver.kill('SIGTERM')
  server.closeAllConnections()
  await new Promise((r) => server.close(r))
  result.finishedAt = new Date().toISOString()
  await save()
}
