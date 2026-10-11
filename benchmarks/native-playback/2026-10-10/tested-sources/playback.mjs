import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { join, resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'
import { launchEngine } from '../../dist/engine.js'

const directory = resolve(
  process.env.SUITECUT_PLAYBACK_DIRECTORY ??
    process.env.MEMORY_DIRECTORY ??
    '.suitecut/native-playback-verification',
)
await mkdir(directory, { recursive: true })
const result = { status: 'starting', tests: [], startedAt: new Date().toISOString() }
const run = async (args) => {
  const process = spawn('ffmpeg', ['-v', 'error', '-y', ...args], {
    stdio: ['ignore', 'ignore', 'pipe'],
  })
  let error = ''
  process.stderr.on('data', (data) => {
    error += data
  })
  const [code] = await once(process, 'close')
  assert.equal(code, 0, error)
}
for (const [profile, fragmented] of [
  ['high', false],
  ['main', true],
]) {
  await run([
    '-f',
    'lavfi',
    '-i',
    'testsrc2=size=640x360:rate=30',
    '-f',
    'lavfi',
    '-i',
    'sine=frequency=880:sample_rate=48000',
    '-t',
    '5',
    '-c:v',
    'libx264',
    '-threads',
    '2',
    '-preset',
    'veryfast',
    '-profile:v',
    profile,
    '-pix_fmt',
    'yuv420p',
    '-g',
    '30',
    '-c:a',
    'aac',
    '-b:a',
    '128k',
    '-movflags',
    fragmented ? 'frag_keyframe+empty_moov+default_base_moof' : '+faststart',
    join(directory, profile + '.mp4'),
  ])
}
const files = {
  '/high.mp4': await readFile(join(directory, 'high.mp4')),
  '/main.mp4': await readFile(join(directory, 'main.mp4')),
}
const server = createServer((request, response) => {
  const file = files[request.url]
  if (file) {
    response.setHeader('Content-Type', 'video/mp4')
    response.setHeader('Accept-Ranges', 'bytes')
    const range = request.headers.range?.match(/^bytes=(\d+)-(\d*)$/)
    if (range) {
      const start = Number(range[1]),
        end = Math.min(file.length - 1, range[2] ? Number(range[2]) : file.length - 1)
      if (start >= file.length || start > end) {
        response.writeHead(416, { 'Content-Range': `bytes */${file.length}` }).end()
        return
      }
      response
        .writeHead(206, {
          'Content-Range': `bytes ${start}-${end}/${file.length}`,
          'Content-Length': end - start + 1,
        })
        .end(file.subarray(start, end + 1))
    } else {
      response.setHeader('Content-Length', file.length)
      response.end(file)
    }
    return
  }
  response.setHeader('Content-Type', 'text/html')
  response.end(`<!doctype html><style>html,body{margin:0;background:#000}video{width:640px;height:360px}</style><video id="video" autoplay loop playsinline></video><script>
  const v=document.querySelector('video');
  if(location.pathname==='/mse') {
    const media=new MediaSource(); v.src=URL.createObjectURL(media);
    media.addEventListener('sourceopen',async()=>{
      try {const data=await (await fetch('/main.mp4')).arrayBuffer(); const buffer=media.addSourceBuffer('video/mp4; codecs="avc1.4D401E, mp4a.40.2"');
      buffer.addEventListener('updateend',()=>media.endOfStream(),{once:true});buffer.appendBuffer(data);await v.play()}
      catch(e){window.playbackError=String(e)}
    },{once:true});
  } else {v.src='/high.mp4';v.play().catch(e=>window.playbackError=String(e))}
  </script>`)
})
await new Promise((ready) => server.listen(0, '127.0.0.1', ready))
let engine, output, unsubscribe
try {
  engine = await launchEngine({
    executablePath: process.env.SUITECUT_NATIVE_EXECUTABLE,
    width: 640,
    height: 360,
    framesPerSecond: 30,
  })
  result.cefVersion = engine.source.cefVersion
  const page = engine.browser.contexts()[0].pages()[0]
  result.capabilities = await page.evaluate(() => {
    const v = document.createElement('video'),
      a = document.createElement('audio')
    return {
      baseline: v.canPlayType('video/mp4; codecs="avc1.42E01E"'),
      main: v.canPlayType('video/mp4; codecs="avc1.4D401E"'),
      high: v.canPlayType('video/mp4; codecs="avc1.64001E"'),
      aac: a.canPlayType('audio/mp4; codecs="mp4a.40.2"'),
      vp9opus: v.canPlayType('video/webm; codecs="vp9, opus"'),
      av1: v.canPlayType('video/mp4; codecs="av01.0.05M.08"'),
      mse: MediaSource.isTypeSupported('video/mp4; codecs="avc1.4D401E, mp4a.40.2"'),
    }
  })
  console.log('Browser playback capabilities', JSON.stringify(result.capabilities))
  for (const key of ['baseline', 'main', 'high', 'aac', 'vp9opus', 'av1', 'mse'])
    assert(
      result.capabilities[key],
      'CEF playback codec missing: ' +
        key +
        '; use the codec-enabled distribution, not the stock SDK',
    )
  const base = `http://127.0.0.1:${server.address().port}`
  for (const route of ['/progressive', '/mse']) {
    let packets = 0,
      audibleSamples = 0
    unsubscribe = engine.source.onAudio((packet) => {
      packets++
      for (let offset = 0; offset < packet.data.length; offset += 2)
        if (Math.abs(packet.data.readInt16LE(offset)) > 8) audibleSamples++
    })
    await page.goto(base + route)
    await page.waitForFunction(
      () => {
        const v = document.querySelector('video')
        return v && !v.paused && v.readyState >= 2 && v.videoWidth === 640 && v.currentTime > 0.3
      },
      undefined,
      { timeout: 15000 },
    )
    const before = await page.evaluate(() => document.querySelector('video').currentTime)
    const name = route.slice(1)
    output = await engine.record(page, {
      destination: join(directory, name + '.mkv'),
      videoCodec: 'ffv1',
      audioCodec: 'pcm_s16le',
    })
    await delay(1800)
    const after = await page.evaluate(() => ({
      time: document.querySelector('video').currentTime,
      width: document.querySelector('video').videoWidth,
      frames: document.querySelector('video').getVideoPlaybackQuality().totalVideoFrames,
      error: window.playbackError ?? null,
    }))
    assert.equal(after.error, null)
    assert(after.time > before + 1)
    assert(after.frames > 30)
    assert(audibleSamples > 20000, 'Decoded AAC did not reach CEF native audio capture')
    const status = await output.stop()
    output = undefined
    assert.equal(status.error, '')
    assert(status.videoFrames > 30)
    assert(status.audioSamples > 48000)
    assert.equal(status.queuedAudioBytes + status.queuedVideoBytes, 0)
    // Verify the finalized file as well as the browser's capture callbacks.
    await run(['-xerror', '-i', join(directory, name + '.mkv'), '-f', 'null', '-'])
    const pcmPath = join(directory, name + '.s16le')
    await run([
      '-i',
      join(directory, name + '.mkv'),
      '-map',
      '0:a:0',
      '-ac',
      '1',
      '-ar',
      '48000',
      '-f',
      's16le',
      pcmPath,
    ])
    const pcm = await readFile(pcmPath)
    let energy = 0
    for (let offset = 0; offset < pcm.length; offset += 2) energy += pcm.readInt16LE(offset) ** 2
    const decodedAudio = {
      samples: pcm.length / 2,
      rms: Math.sqrt(energy / (pcm.length / 2)),
    }
    assert(decodedAudio.samples > 48000)
    assert(decodedAudio.rms > 100, 'Recorded audio lost the decoded AAC tone')
    await page.screenshot({ path: join(directory, name + '.png') })
    unsubscribe()
    unsubscribe = undefined
    const check = {
      mode: name,
      before,
      after,
      packets,
      audibleSamples,
      decodedAudio,
      output: status,
    }
    result.tests.push(check)
    console.log('PASSED', JSON.stringify(check))
  }
  result.status = 'passed'
} catch (error) {
  result.status = 'failed'
  result.error = error.stack
  throw error
} finally {
  unsubscribe?.()
  await output?.stop().catch(() => {})
  await engine?.close()
  server.closeAllConnections()
  await new Promise((ready) => server.close(ready))
  result.finishedAt = new Date().toISOString()
  await writeFile(join(directory, 'playback-result.json'), JSON.stringify(result, null, 2))
  console.log(JSON.stringify(result, null, 2))
}
