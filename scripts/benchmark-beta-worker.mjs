/* global document */
/* eslint jsdoc/check-tag-names: ["error", {"typed": false}] -- JavaScript worker needs one imported type. */
/* eslint @typescript-eslint/no-unsafe-assignment: off, @typescript-eslint/no-unsafe-call: off, @typescript-eslint/no-unsafe-member-access: off, @typescript-eslint/no-unsafe-return: off */
import assert from 'node:assert/strict'
import { execFile } from 'node:child_process'
import { once } from 'node:events'
import { mkdir, readFile, stat, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { promisify } from 'node:util'

import * as z from 'zod'

import { record } from '../dist/beta.js'
import { launchNativeBrowser } from '../dist/native.js'
import { resolveFfmpeg, resolveFfprobe } from '../dist/process.js'

const config = z
  .object({
    backend: z.enum(['native', 'playwright']),
    workload: z.enum(['static', 'canvas', 'dom', 'multi-page']),
    durationMs: z.number(),
    width: z.number(),
    height: z.number(),
    fps: z.union([z.literal(30), z.literal(60)]),
    directory: z.string(),
  })
  .parse(JSON.parse(process.argv[2] ?? '{}'))
await mkdir(config.directory, { recursive: true })
const controller = new AbortController()
process.once('SIGINT', () => controller.abort(new Error('Benchmark interrupted')))
process.once('SIGTERM', () => controller.abort(new Error('Benchmark interrupted')))

function fixture(variant) {
  const marker = '<canvas id="marker" width="320" height="32"></canvas>'
  const common = `<style>
    *{box-sizing:border-box}html,body{margin:0;min-height:100%;overflow:hidden;background:#142738;color:white;font-family:system-ui}
    #marker{position:fixed;z-index:10;inset:0 auto auto 0;width:320px;height:32px;image-rendering:pixelated}
    #visual{position:fixed;inset:0;width:100%;height:100%}
    #dom-stage{position:absolute;inset:0;padding:80px 5vw 120px;background:${variant === 'secondary' ? '#263d6b' : '#142738'}}
    .card-grid{display:grid;grid-template-columns:repeat(5,minmax(0,1fr));gap:14px}.card{height:110px;padding:18px;border:1px solid #ffffff33;background:#ffffff0b}
  </style>`
  const canvas = `<canvas id="visual" width="${config.width}" height="${config.height}"></canvas>`
  const cards = Array.from(
    { length: 60 },
    (_, index) =>
      `<div class="card">Card ${index + 1}<br><small>SuiteCut DOM workload</small></div>`,
  ).join('')
  const body =
    config.workload === 'dom'
      ? `<main id="dom-stage"><h1>DOM and scrolling workload</h1><div class="card-grid">${cards}</div></main>`
      : canvas
  const animate = config.workload !== 'static'
  return `<!doctype html><meta charset="utf-8">${common}${marker}${body}<script>
    const marker=document.querySelector('#marker'),markerContext=marker.getContext('2d');
    const visual=document.querySelector('#visual'),context=visual?.getContext('2d');
    const stage=document.querySelector('#dom-stage');let count=0;
    function mark(id){for(let bit=0;bit<20;bit++){markerContext.fillStyle=id&(1<<bit)?'white':'black';markerContext.fillRect(bit*16,0,16,32)}}
    function frame(now){
      const active=document.documentElement.dataset.benchmarkActive==='true';
      if(active){count++;document.documentElement.dataset.benchmarkCount=String(count);mark(count)}else mark(0);
      if(context){context.fillStyle='${variant === 'secondary' ? '#263d6b' : '#142738'}';context.fillRect(0,0,visual.width,visual.height);context.fillStyle='#ffd34d';context.fillRect((now*.35)%(visual.width-140),visual.height*.62,140,140);context.fillStyle='white';context.font='48px sans-serif';context.fillText('${variant === 'secondary' ? 'Secondary' : 'Primary'} SuiteCut benchmark',50,150)}
      if(stage){const offset=(now*.12)%Math.max(1,stage.scrollHeight-innerHeight);stage.style.transform='translateY('+(-offset)+'px)'}
      requestAnimationFrame(frame)
    }
    ${animate ? 'requestAnimationFrame(frame)' : "markerContext.fillStyle='black';markerContext.fillRect(0,0,320,32)"};
  </script>`
}

const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(fixture(request.url?.includes('secondary') ? 'secondary' : 'primary'))
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const url = `http://127.0.0.1:${address.port}`
let startupMs = 0
let callbackEndMs = 0
let animationFrames = 0
let browserVersion = ''
/** @type {import('../dist/native.js').NativeBrowser | undefined} */
let secondaryNative
const started = performance.now()
const output = {
  directory: config.directory,
  manifestPath: resolve(config.directory, 'manifest.json'),
}

function activate(page, active) {
  return page.evaluate((value) => {
    document.documentElement.dataset.benchmarkActive = String(value)
    if (!value) return Number(document.documentElement.dataset.benchmarkCount ?? '0')
    return 0
  }, active)
}

/**
 * Runs the same authored workload on either backend.
 * @param {import('../dist/beta.js').NativeRecordingContext | import('../dist/playwright.js').SuiteCutRecordingContext} context
 */
async function flow(context) {
  startupMs = performance.now() - started
  if ('source' in context) browserVersion = context.source.cefVersion
  else browserVersion = context.browser.version()
  await context.page.goto(`${url}/primary`)
  await context.page.waitForTimeout(1000)
  await activate(context.page, true)
  if (config.workload !== 'multi-page') {
    await context.suitecut.hold(config.durationMs)
  } else {
    let next
    if ('source' in context) {
      const executablePath = process.env.SUITECUT_NATIVE_EXECUTABLE
      if (!executablePath) throw new Error('Missing native executable for multi-page benchmark')
      secondaryNative = await launchNativeBrowser({
        executablePath,
        width: config.width,
        height: config.height,
        framesPerSecond: config.fps,
        pixelFormat: 'i420',
        signal: controller.signal,
      })
      await secondaryNative.navigate(`${url}/secondary`)
      next = await context.addSource(secondaryNative)
    } else {
      next = await context.context.newPage()
      await next.goto(`${url}/secondary`)
      await next.waitForTimeout(250)
    }
    await activate(next, true)
    const section = Math.floor(config.durationMs / 3)
    await context.suitecut.hold(section)
    context.suitecut.selectPage(next)
    await context.suitecut.hold(section)
    context.suitecut.selectPage(context.page)
    await context.suitecut.hold(config.durationMs - section * 2)
    await activate(next, false)
  }
  animationFrames = z.number().parse(await activate(context.page, false))
  await context.page.waitForTimeout(200)
  callbackEndMs = performance.now()
}

try {
  const result =
    config.backend === 'native'
      ? await record('Native beta benchmark', flow, {
          native: { width: config.width, height: config.height, framesPerSecond: config.fps },
          signal: controller.signal,
          output,
        })
      : await record('Playwright baseline benchmark', flow, {
          backend: 'playwright',
          signal: controller.signal,
          context: { viewport: { width: config.width, height: config.height } },
          capture: {
            framesPerSecond: config.fps,
            size: { width: config.width, height: config.height },
          },
          output,
        })
  const finished = performance.now()
  const artifacts =
    result.manifest.tests[0]?.attempts[0]?.artifacts.filter(
      (item) => item.role === 'source-video',
    ) ?? []
  const artifact = artifacts[0]
  assert(artifact)
  const ffmpeg = await resolveFfmpeg()
  const ffprobe = await resolveFfprobe(ffmpeg)
  const execute = promisify(execFile)
  const probe = await execute(ffprobe, [
    '-v',
    'error',
    '-select_streams',
    'v:0',
    '-show_entries',
    'stream=width,height,avg_frame_rate,codec_name',
    '-of',
    'json',
    artifact.path,
  ])
  const media = z
    .object({
      streams: z.array(
        z.object({
          width: z.number(),
          height: z.number(),
          avg_frame_rate: z.string(),
          codec_name: z.string(),
        }),
      ),
    })
    .parse(JSON.parse(probe.stdout)).streams[0]
  assert(
    media?.width === config.width &&
      media.height === config.height &&
      media.avg_frame_rate === `${config.fps}/1`,
  )
  let decodedActiveFrames = null
  let uniqueFrames = null
  let uniqueFps = null
  let repeatedFramePercent = null
  let longestHeldFrameMs = null
  if (config.workload === 'canvas' || config.workload === 'dom') {
    const pixelsPath = resolve(config.directory, 'frame-ids.gray')
    await execute(ffmpeg, [
      '-v',
      'error',
      '-i',
      artifact.path,
      '-vf',
      'crop=320:1:0:16:exact=1,format=gray',
      '-f',
      'rawvideo',
      '-y',
      pixelsPath,
    ])
    const pixels = await readFile(pixelsPath)
    const ids = []
    for (let offset = 0; offset + 320 <= pixels.length; offset += 320) {
      let id = 0
      for (let bit = 0; bit < 20; bit++) if (pixels[offset + bit * 16 + 8] > 128) id |= 1 << bit
      ids.push(id)
    }
    const first = ids.findIndex((id) => id > 0 && id <= animationFrames)
    const last = ids.findLastIndex((id) => id > 0 && id <= animationFrames)
    assert(first >= 0 && last > first, 'No animated frame IDs decoded')
    const active = ids.slice(first, last + 1)
    assert(
      active.every(
        (id, index) => id > 0 && id <= animationFrames && (index === 0 || id >= active[index - 1]),
      ),
      'Frame IDs must be valid and monotonic',
    )
    const unique = new Set(active).size
    let longest = 1
    let current = 1
    for (let index = 1; index < active.length; index++) {
      current = active[index] === active[index - 1] ? current + 1 : 1
      longest = Math.max(longest, current)
    }
    decodedActiveFrames = active.length
    uniqueFrames = unique
    uniqueFps = (unique * config.fps) / active.length
    repeatedFramePercent = (1 - unique / active.length) * 100
    longestHeldFrameMs = (longest * 1000) / config.fps
  }
  const sizes = await Promise.all(artifacts.map((item) => stat(item.path)))
  const report = {
    backend: result.backend,
    browserVersion,
    workload: config.workload,
    config,
    media,
    startupMs: Math.round(startupMs),
    totalWallMs: Math.round(finished - started),
    finalizationMs: Math.round(finished - callbackEndMs),
    fileBytes: sizes.reduce((sum, item) => sum + item.size, 0),
    sourceCount: artifacts.length,
    animationFrames,
    decodedActiveFrames,
    uniqueFrames,
    uniqueFps,
    repeatedFramePercent,
    longestHeldFrameMs,
  }
  await writeFile(resolve(config.directory, 'result.json'), JSON.stringify(report, null, 2))
} finally {
  await secondaryNative?.close().catch(() => undefined)
  server.closeAllConnections()
  server.close()
}
