import assert from 'node:assert/strict'
import { execFileSync, spawn } from 'node:child_process'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setInterval, clearInterval } from 'node:timers'
import { setTimeout as delay } from 'node:timers/promises'

import { z } from 'zod'

import { record } from '../dist/playwright.js'
import { resolveFfmpeg, resolveFfprobe, runProcess } from '../dist/process.js'

const ffmpeg = await resolveFfmpeg()
const ffprobe = await resolveFfprobe(ffmpeg)

const directory = resolve('.suitecut/tab-audio-verification', String(Date.now()))
await mkdir(directory, { recursive: true })
const seconds = Number(process.env.SUITECUT_AUDIO_SOAK_SECONDS ?? 5)
assert(Number.isFinite(seconds) && seconds >= 3 && seconds <= 7200)
const timeout = AbortSignal.timeout((seconds + 90) * 1000)
const stress = process.env.SUITECUT_AUDIO_STRESS === '1'
// The flash and gain change happen in the same animation frame.
const html = `<body style="margin:0;background:black"><script>
const audio = new AudioContext(); const tone = audio.createOscillator(); const gain = audio.createGain();
tone.frequency.value=880; tone.connect(gain); gain.connect(audio.destination); gain.gain.value=0; tone.start();
let previous=false;
const canvas = document.createElement('canvas'); canvas.width=1280; canvas.height=720;
canvas.style.cssText='position:fixed;inset:0;width:100%;height:100%';
if (${stress}) document.body.appendChild(canvas);
const paint = canvas.getContext('2d'); let tick=0;
function frame() {
 const on = performance.now()%1000 < 180;
 if(on!==previous) { document.body.style.background=on?'white':'black'; gain.gain.setValueAtTime(on?0.3:0,audio.currentTime); previous=on; }
 if (${stress}) {
   canvas.style.visibility=on?'hidden':'visible'; tick++;
   for(let y=0;y<720;y+=20) for(let x=0;x<1280;x+=20) {
     const shade=(x*17+y*31+tick*13)%65;
     paint.fillStyle='rgb('+shade+','+shade+','+shade+')'; paint.fillRect(x,y,20,20);
   }
 }
 requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
</script>`
const web = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(request.url === '/silent' ? '<body style="background:black">' : html)
})
web.listen(0, '127.0.0.1')
await once(web, 'listening')
const address = web.address()
assert(address && typeof address !== 'string')
const base = `http://127.0.0.1:${address.port}`
const port = Number(process.env.SUITECUT_AUDIO_RTMP_PORT ?? 19363)
assert(Number.isInteger(port) && port > 1024 && port < 65536)
const url = `rtmp://127.0.0.1:${port}/live/tab-audio-verification`
let part = 0
function receive() {
  const path = `${directory}/received-${++part}.flv`
  const child = spawn(
    ffmpeg,
    [
      '-hide_banner',
      '-loglevel',
      'error',
      '-listen',
      '1',
      '-i',
      url,
      '-c',
      'copy',
      '-flush_packets',
      '1',
      '-y',
      path,
    ],
    { stdio: ['ignore', 'ignore', 'ignore'] },
  )
  return { child, path, closed: once(child, 'close') }
}
const memory = []
function sampleMemory() {
  const sample = { at: Date.now(), rss: process.memoryUsage().rss }
  if (stress && process.platform !== 'win32') {
    const rows = execFileSync('ps', ['-axo', 'pid=,ppid=,rss=,comm='], { encoding: 'utf8' })
      .trim()
      .split('\n')
      .map((line) => {
        const match = /^(\d+)\s+(\d+)\s+(\d+)\s+(.+)$/.exec(line.trim())
        return match
          ? { pid: +match[1], parent: +match[2], rss: +match[3] * 1024, command: match[4] }
          : null
      })
      .filter(Boolean)
    const descendants = new Set([process.pid])
    let previousSize = 0
    while (previousSize !== descendants.size) {
      previousSize = descendants.size
      for (const row of rows) if (descendants.has(row.parent)) descendants.add(row.pid)
    }
    sample.processes = rows.filter((row) => descendants.has(row.pid))
  }
  memory.push(sample)
}
const memoryTimer = setInterval(sampleMemory, 1000)
let receiver = receive()
const paths = [receiver.path]
const pluginPath = `${directory}/pause-plugin.mjs`
await writeFile(
  pluginPath,
  `import {writeFile} from 'node:fs/promises';
import {encodePcm16Wav} from ${JSON.stringify(new URL('../dist/audio-plugin.js', import.meta.url).href)};
export default {async synthesize({outputPath}) {await new Promise(r=>setTimeout(r,1500)); await writeFile(outputPath,encodePcm16Wav(new Float32Array(2400),24000));}};`,
)
try {
  await delay(400)
  const result = await record(
    'tab audio sync and lifecycle',
    async ({ page, context, suitecut }) => {
      await page.goto(`${base}/first`)
      if (stress) {
        const cdp = await context.browser().newBrowserCDPSession()
        const { targetInfos } = await cdp.send('Target.getTargets')
        const offscreen = targetInfos.find((target) => target.url.endsWith('/offscreen.html'))
        assert(offscreen, 'Missing audio offscreen target')
        const { sessionId } = await cdp.send('Target.attachToTarget', {
          targetId: offscreen.targetId,
        })
        const end = Date.now() + seconds * 1000
        let cycle = 0
        while (Date.now() < end) {
          await suitecut.hold(Math.min(10000, end - Date.now()))
          if (Date.now() >= end) break
          // Block audio delivery, or sever the socket, while tab video keeps running.
          const expression =
            cycle++ % 2 === 0
              ? '(() => { const end = performance.now() + 350; while (performance.now() < end) {} })()'
              : 'socket.close()'
          await cdp.send('Target.sendMessageToTarget', {
            sessionId,
            message: JSON.stringify({
              id: cycle,
              method: 'Runtime.evaluate',
              params: { expression },
            }),
          })
        }
        await cdp.detach()
      } else await suitecut.hold(seconds * 1000)
      await page.goto(`${base}/navigation`)
      await suitecut.hold(3100)
      // Silent tab selection must exclude the still-audible original tab.
      const popup = await context.newPage()
      await popup.goto(`${base}/silent`)
      suitecut.selectPage(popup)
      await suitecut.hold(2200)
      await popup.close()
      await suitecut.hold(3100)
      const [windowPopup] = await Promise.all([
        context.waitForEvent('page'),
        page.evaluate((url) => {
          globalThis.open(url, 'audio-popup')
        }, `${base}/silent`),
      ])
      suitecut.selectPage(windowPopup)
      await suitecut.hold(1800)
      await windowPopup.close()
      await suitecut.hold(2100)
      await suitecut.narrate('Pause capture', { provider: 'pause-test', caption: '' })
      await suitecut.hold(2100)
      receiver.child.kill('SIGKILL')
      await receiver.closed
      await delay(1000)
      receiver = receive()
      paths.push(receiver.path)
      await suitecut.hold(6500)
    },
    {
      launch: { headless: process.env.SUITECUT_AUDIO_HEADED !== '1' },
      capture: {
        viewport: stress ? { width: 1280, height: 720 } : { width: 320, height: 180 },
        stream: {
          url,
          audio: 'tab',
          bitrateKbps: stress ? 2500 : 400,
          reconnect: { initialDelayMs: 300, maxDelayMs: 1000, maxAttempts: 15 },
        },
      },
      audioPlugins: [{ provider: 'pause-test', module: pluginPath }],
      output: { directory: `${directory}/source`, manifestPath: `${directory}/manifest.json` },
      signal: timeout,
    },
  )
  assert.equal(result.manifest.status, 'passed')
  await receiver.closed
  clearInterval(memoryTimer)
  const reports = []
  for (const [partIndex, path] of paths.entries()) {
    const pcmPath = `${path}.pcm`
    const pixelsPath = `${path}.gray`
    await runProcess(ffmpeg, [
      '-v',
      'error',
      '-i',
      path,
      '-map',
      '0:a:0',
      '-ac',
      '1',
      '-ar',
      '48000',
      '-f',
      's16le',
      '-y',
      pcmPath,
    ])
    await runProcess(ffmpeg, [
      '-v',
      'error',
      '-i',
      path,
      '-map',
      '0:v:0',
      '-vf',
      'scale=1:1:flags=area,format=gray',
      '-fps_mode',
      'passthrough',
      '-f',
      'rawvideo',
      '-y',
      pixelsPath,
    ])
    const probe = await runProcess(ffprobe, [
      '-v',
      'error',
      '-select_streams',
      'v:0',
      '-show_entries',
      'frame=best_effort_timestamp_time',
      '-of',
      'json',
      path,
    ])
    const timestamps = z
      .object({ frames: z.array(z.object({ best_effort_timestamp_time: z.string() })) })
      .parse(JSON.parse(probe.stdout))
      .frames.map((frame) => Number(frame.best_effort_timestamp_time))
    const pcm = await readFile(pcmPath)
    const pixels = await readFile(pixelsPath)
    // eslint-disable-next-line jsdoc/check-tag-names -- This JavaScript verifier needs numeric array types.
    /** @type {number[]} */
    const audioStarts = []
    const quietRuns = []
    let audible = false
    let quietAt = 0
    for (let sample = 0; sample + 480 <= pcm.length / 2; sample += 480) {
      let sum = 0
      for (let i = 0; i < 480; i++) sum += (pcm.readInt16LE((sample + i) * 2) / 32768) ** 2
      const on = Math.sqrt(sum / 480) > 0.04
      if (on && !audible) {
        audioStarts.push(sample / 48000)
        quietRuns.push(sample / 48000 - quietAt)
      }
      if (!on && audible) quietAt = sample / 48000
      audible = on
    }
    // eslint-disable-next-line jsdoc/check-tag-names -- This JavaScript verifier needs numeric array types.
    /** @type {number[]} */
    const flashes = []
    for (let i = 1; i < pixels.length; i++)
      if (pixels[i] > 180 && pixels[i - 1] < 80) flashes.push(timestamps[i])
    const offsets = flashes.map((time) =>
      audioStarts.reduce(
        (best, audio) => (Math.abs(audio - time) < Math.abs(best) ? audio - time : best),
        Infinity,
      ),
    )
    // Exclude deliberate capture transitions; they produce a partial flash at startup/resume.
    const matched = offsets.filter((offset) => Math.abs(offset) < 0.3)
    assert(matched.length >= 3, `Insufficient matched flash/beep pairs in part ${partIndex + 1}`)
    const sorted = matched.map(Math.abs).sort((a, b) => a - b)
    const p95 = sorted[Math.floor((sorted.length - 1) * 0.95)]
    assert(p95 < 0.12, `Audio/video sync exceeded 120ms: ${p95}s`)
    assert(
      matched.length >= flashes.length - (stress ? Math.ceil(seconds / 10) + 3 : 3),
      'Too many flashes lacked matching audio',
    )
    if (partIndex === 0) {
      assert(
        quietRuns.some((duration) => duration > 1.7),
        'Selecting a silent page did not silence the stream',
      )
      assert(
        quietRuns.filter((duration) => duration > 1.2).length >= 3,
        'Silent tabs and capture pause must each create a silent interval',
      )
    }
    reports.push({
      part: partIndex + 1,
      beeps: audioStarts.length,
      flashes: flashes.length,
      matched: matched.length,
      unmatched: flashes.length - matched.length,
      p95SyncMs: Math.round(p95 * 1000),
      maxQuietSeconds: Math.max(...quietRuns),
    })
  }
  await writeFile(
    `${directory}/results.json`,
    JSON.stringify(
      { platform: process.platform, seconds, stress, reports, nodeMemory: memory },
      null,
      2,
    ),
  )
  console.log(JSON.stringify({ directory, reports }, null, 2))
} finally {
  clearInterval(memoryTimer)
  receiver.child.kill('SIGKILL')
  web.closeAllConnections()
  web.close()
}
