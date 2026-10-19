/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Exercises a selectable emitted SDK and validates actual native output. */
/* global console, document, window */
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import { once } from 'node:events'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'
import { promisify } from 'node:util'

const run = promisify(execFile),
  output = resolve(process.env.SUITECUT_CAPTURE_PROOF ?? '.suitecut/capture-retirement-proof')
const sdk = resolve(process.env.SUITECUT_CAPTURE_SDK ?? 'dist/browser.js')
const executablePath = resolve(
  process.env.SUITECUT_NATIVE_EXECUTABLE ?? '.suitecut/native-build/suitecut-browser',
)
const { launchBrowser } = await import(pathToFileURL(sdk))
await mkdir(output, { recursive: true })
const server = createServer((q, r) => {
  r.setHeader('Content-Type', 'text/html')
  r.end(
    `<!doctype html><style>body{margin:0;background:#315f88;height:1400px}#marker{position:absolute;left:70px;top:70px;width:100px;height:100px;background:#cf4318}</style><div id="marker"></div><script>window.animate=true;let n=0;function tick(){if(window.animate)document.querySelector('#marker').style.backgroundColor=(++n%2)?'#cf4318':'#50a054';requestAnimationFrame(tick)}tick()</script>`,
  )
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const url = `http://127.0.0.1:${server.address().port}`
let browser, off
const checks = [],
  recordings = []
try {
  browser = await launchBrowser({
    executablePath,
    headless: true,
    width: 320,
    height: 240,
    framesPerSecond: 60,
  })
  const page = await browser.newPage()
  await page.goto(url)
  let latest
  off = page.source.onFrame((frame) => {
    latest = frame
  })
  await page.evaluate(() => {
    window.animate = false
  })
  const png = await page.screenshot({ fullPage: true })
  assert(png.length > 100)
  await writeFile(output + '/full-page.png', png)
  await page.evaluate(() => {
    document.querySelector('#marker').style.backgroundColor = '#cf4318'
  })
  const deadline = Date.now() + 10000
  const pixel = (x, y) =>
    latest?.data.subarray((y * 320 + x) * 4, (y * 320 + x) * 4 + 3).toString('hex')
  while (
    (pixel(100, 100) !== '1843cf' ||
      pixel(4, 4) !== '885f31' ||
      latest?.viewport?.width !== 320 ||
      latest?.viewport?.height !== 240) &&
    Date.now() < deadline
  )
    await delay(30)
  assert.equal(
    pixel(100, 100),
    '1843cf',
    'Dirty marker paint did not restore after full-page capture',
  )
  assert.equal(pixel(4, 4), '885f31', 'Unchanged pixels lost after screenshot view restoration')
  assert.deepEqual(latest.viewport, { width: 320, height: 240 })
  checks.push('full-page-screenshot-followed-by-dirty-paint-restores-complete-original-viewport')
  // A full session stop must release source-cache ownership and allow another
  // session to capture the same paths. Check real exported ZIPs across reuse.
  for (let iteration = 0; iteration < 3; iteration++) {
    await page.context().tracing.start({ sources: true, screenshots: false, snapshots: false })
    await page.title()
    const path = output + `/trace-${iteration}.zip`
    await page.context().tracing.stop({ path })
    assert((await readFile(path)).length > 100)
  }
  checks.push('three-source-tracing-sessions-stop-and-restart-on-live-context')
  off()
  off = undefined
  await browser.close()
  assert.equal(browser.source.processExit().code, 0)
  browser = undefined
  for (const fps of [60, 120]) {
    browser = await launchBrowser({
      executablePath,
      headless: true,
      width: 320,
      height: 240,
      pixelFormat: 'i420',
      framesPerSecond: fps,
    })
    const tab = await browser.newPage()
    await tab.goto(url)
    const outputPath = output + `/native-i420-${fps}.webm`
    const started = await tab.source.runRecording({
      action: 'start',
      ffmpegPath: '/usr/bin/ffmpeg',
      outputPath,
      width: 320,
      height: 240,
      framesPerSecond: fps,
      audio: false,
      timeoutMs: 30000,
    })
    assert.equal(started.phase, 'recording')
    await delay(700)
    await tab.setViewportSize({ width: 317, height: 239 })
    await delay(700)
    const stopped = await tab.source.runRecording({
      action: 'stop',
      recordingId: started.id,
      timeoutMs: 30000,
    })
    assert.equal(stopped.phase, 'complete', stopped.failure)
    assert.equal(stopped.failure, '')
    const probe = JSON.parse(
      (
        await run('/usr/bin/ffprobe', [
          '-v',
          'error',
          '-count_frames',
          '-select_streams',
          'v:0',
          '-show_entries',
          'stream=codec_name,width,height,r_frame_rate,nb_read_frames',
          '-show_entries',
          'format=duration',
          '-of',
          'json',
          outputPath,
        ])
      ).stdout,
    )
    const stream = probe.streams[0]
    assert.deepEqual([stream.width, stream.height, stream.r_frame_rate], [320, 240, `${fps}/1`])
    assert(Number(stream.nb_read_frames) > fps)
    assert.equal(Number(stream.nb_read_frames), stopped.outputFrames)
    await run('/usr/bin/ffmpeg', ['-v', 'error', '-i', outputPath, '-f', 'null', '-'])
    recordings.push({ fps, outputPath, probe, native: stopped })
    await browser.close()
    assert.equal(browser.source.processExit().code, 0)
    browser = undefined
  }
  checks.push('real-60-and-120-FPS-I420-recordings-decode-after-odd-viewport-resize')
} catch (error) {
  process.exitCode = 1
  checks.push({ error: String(error), stack: error.stack })
} finally {
  off?.()
  await browser?.close()
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  const result = {
    status: process.exitCode ? 'FAIL' : 'PASS',
    sdk,
    executablePath,
    checks,
    recordings,
    scope:
      'Actual screenshot/live-paint restoration, trace-session reuse, and two short native I420 encodes. CFR output rate verified; distinct images at every requested tick are not claimed.',
  }
  await writeFile(output + '/result.json', JSON.stringify(result, null, 2) + '\n')
  console.log(JSON.stringify(result, null, 2))
}
