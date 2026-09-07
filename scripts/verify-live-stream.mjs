import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, readdir, stat, writeFile, readFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { setTimeout as delay } from 'node:timers/promises'

import { record } from '../dist/playwright.js'
import { runProcess } from '../dist/process.js'

const directory = resolve('.suitecut/live-verification', String(Date.now()))
await mkdir(directory, { recursive: true })
const pluginPath = `${directory}/audio-plugin.mjs`
await writeFile(
  pluginPath,
  `import {writeFile} from 'node:fs/promises';
import {encodePcm16Wav} from ${JSON.stringify(new URL('../dist/audio-plugin.js', import.meta.url).href)};
export default {async synthesize({outputPath}) {await writeFile(outputPath, encodePcm16Wav(new Float32Array(48000), 24000))}};`,
)
const url = 'rtmp://127.0.0.1:19362/live/local-verification'
let part = 0
function receive() {
  const path = `${directory}/received-${++part}.flv`
  const child = spawn(
    'ffmpeg',
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
  const closed = once(child, 'close').then(() => undefined)
  return { child, closed, path }
}
let receiver = receive()
const timeout = AbortSignal.timeout(90_000)
try {
  await delay(400)
  const result = await record(
    'live effects and reconnect',
    async ({ page, context, suitecut }) => {
      await page.setContent(
        `<style>body{margin:0;background:#183044;color:white;font:24px sans-serif}button{position:absolute;left:260px;top:140px;width:120px;height:70px;background:#ffc600;font:22px sans-serif}input{position:absolute;top:270px;left:100px}#bottom{position:absolute;top:1200px}</style><h1>Live effects</h1><button onclick="this.textContent='Moved'">Move</button><input aria-label="Comment"><div id="bottom">Bottom</div>`,
      )
      const target = page.getByRole('button')
      await suitecut.hover(target)
      await suitecut.click(target)
      assert.equal(await target.textContent(), 'Moved')
      await suitecut.type(page.getByRole('textbox'), 'Live game', { delayMs: 40 })
      assert.equal(await page.getByRole('textbox').inputValue(), 'Live game')
      const before = await target.boundingBox()
      assert(before)
      const zoom = suitecut.zoom(target, {
        scale: 1.25,
        enter: { durationMs: 300 },
        holdMs: 1500,
        exit: { durationMs: 300 },
      })
      await delay(800)
      assert.deepEqual(await target.boundingBox(), before, 'Live camera changed website geometry')
      await page.screenshot({ path: `${directory}/zoom.png` })
      await zoom
      const after = await target.boundingBox()
      assert(after && Math.abs(after.width - before.width) < 1, 'Zoom did not restore geometry')
      const highlight = suitecut.highlight(target, { durationMs: 1600 })
      await delay(400)
      assert.equal(await page.locator('[data-suitecut-highlight]').count(), 1)
      await page.screenshot({ path: `${directory}/highlight.png` })
      await highlight
      const caption = suitecut.narrate('Live captions', { provider: 'live-test' })
      await page.locator('[data-suitecut-caption]').waitFor({ state: 'visible' })
      assert.equal(await page.locator('[data-suitecut-caption]').textContent(), 'Live captions')
      await page.screenshot({ path: `${directory}/caption.png` })
      await caption
      assert.deepEqual(
        await readdir(`${directory}/source`),
        [],
        'Narration temporary files were retained',
      )
      await suitecut.scrollTo(page.locator('#bottom'))
      assert(await page.evaluate(() => globalThis.scrollY > 0))
      await page.locator('#bottom').evaluate((element) => {
        element.style.width = '120px'
      })
      const scrolledZoom = suitecut.zoom(page.locator('#bottom'), { scale: 1.25, holdMs: 1500 })
      await delay(700)
      const scrolledTarget = await page.locator('#bottom').boundingBox()
      assert(
        scrolledTarget && scrolledTarget.y >= 0 && scrolledTarget.y + scrolledTarget.height <= 361,
        'Scrolled zoom changed website geometry',
      )
      await page.screenshot({ path: `${directory}/scrolled-zoom.png` })
      await scrolledZoom
      await suitecut.scrollTop()
      await suitecut.checkpoint('Live hold', { durationMs: 500 })
      const size = (await stat(receiver.path)).size
      await suitecut.hold(1000)
      assert((await stat(receiver.path)).size > size, 'Idle broadcast stopped')

      receiver.child.kill('SIGKILL')
      await receiver.closed
      await delay(1800)
      receiver = receive()
      await page.setContent(
        '<body style="background:#15723d;color:white;font:48px sans-serif">RECONNECTED</body>',
      )
      for (let tries = 0; tries < 60; tries++) {
        if ((await stat(receiver.path).catch(() => ({ size: 0 }))).size > 1000) break
        await delay(200, undefined, { signal: timeout })
      }
      assert((await stat(receiver.path)).size > 1000, 'No stream after restarting the receiver')
      const resumedBytes = (await stat(receiver.path)).size
      await delay(2500)
      assert(
        (await stat(receiver.path)).size > resumedBytes,
        'Reconnected output did not keep delivering frames',
      )
      const popup = await context.newPage()
      await popup.setContent(
        '<body style="background:#b32843;color:white;font:48px sans-serif">SECOND PAGE</body>',
      )
      suitecut.selectPage(popup)
      await suitecut.hold(1600)
      await popup.close()
      await suitecut.hold(1600)
      // Repeated page and event lifecycles must not produce a recording archive.
      for (let i = 0; i < 8; i++) {
        const tab = await context.newPage()
        await tab.setContent('<body>Transient page</body>')
        await tab.close()
      }
    },
    {
      audioPlugins: [{ provider: 'live-test', module: pluginPath }],
      capture: {
        viewport: { width: 640, height: 360 },
        stream: {
          url,
          bitrateKbps: 800,
          reconnect: { initialDelayMs: 300, maxDelayMs: 1000, maxAttempts: 20 },
        },
      },
      output: { directory: `${directory}/source`, manifestPath: `${directory}/manifest.json` },
      signal: timeout,
    },
  )
  await receiver.closed
  const probe = await runProcess(
    'ffprobe',
    [
      '-v',
      'error',
      '-count_frames',
      '-select_streams',
      'v:0',
      '-show_entries',
      'stream=nb_read_frames',
      '-of',
      'csv=p=0',
      receiver.path,
    ],
    { timeoutMs: 10000 },
  )
  assert.equal(probe.exitCode, 0)
  assert(Number(probe.stdout.trim()) >= 120, 'Reconnected video did not contain sustained frames')
  assert.deepEqual(
    await readdir(`${directory}/source`),
    [],
    'Streaming-only mode wrote source artifacts',
  )
  const rawPath = `${directory}/decoded.rgb`
  const decoded = await runProcess(
    'ffmpeg',
    [
      '-hide_banner',
      '-loglevel',
      'error',
      '-i',
      `${directory}/received-1.flv`,
      '-vf',
      'fps=4,scale=160:90',
      '-pix_fmt',
      'rgb24',
      '-f',
      'rawvideo',
      '-y',
      rawPath,
    ],
    { timeoutMs: 20000 },
  )
  assert.equal(decoded.exitCode, 0)
  const pixels = await readFile(rawPath)
  let maximumWidth = 0
  let normalFrames = 0
  for (let offset = 0; offset + 160 * 90 * 3 <= pixels.length; offset += 160 * 90 * 3) {
    let left = 160,
      right = -1
    for (let y = 0; y < 90; y++)
      for (let x = 0; x < 160; x++) {
        const index = offset + (y * 160 + x) * 3
        if (pixels[index] > 180 && pixels[index + 1] > 130 && pixels[index + 2] < 80) {
          left = Math.min(left, x)
          right = Math.max(right, x)
        }
      }
    const width = right - left + 1
    if (width >= 28 && width <= 32) normalFrames++
    maximumWidth = Math.max(maximumWidth, width)
  }
  assert(
    normalFrames > 0 && maximumWidth >= 36,
    'Encoded stream did not contain both normal and zoomed target pixels',
  )
  const attempt = result.manifest.tests[0]?.attempts[0]
  assert(attempt)
  assert.deepEqual(attempt.events, [])
  assert.deepEqual(attempt.artifacts, [])
  console.log(
    `Live capture, visual effects, idle pacing, receiver restart, page switching, and no source artifacts passed: ${directory}`,
  )
} finally {
  receiver.child.kill('SIGKILL')
}
