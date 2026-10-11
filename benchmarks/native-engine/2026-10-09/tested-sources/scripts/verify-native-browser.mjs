import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { once } from 'node:events'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { clearTimeout, setTimeout } from 'node:timers'
import { setTimeout as delay } from 'node:timers/promises'

import imageProcessor from 'sharp'
import * as z from 'zod'

import { launchNativeBrowser } from '../dist/native.js'

const executablePath = process.env.SUITECUT_NATIVE_EXECUTABLE
if (!executablePath) throw new Error('Set SUITECUT_NATIVE_EXECUTABLE to the built CEF executable')
const directory = resolve('.suitecut/native-verification')
await mkdir(directory, { recursive: true })
const server = createServer((request, response) => {
  if (request.url === '/missing') {
    response.writeHead(404).end('missing')
    return
  }
  response.setHeader('Content-Type', 'text/html')
  response.end(`<!doctype html><meta charset="utf-8"><style>
  body { margin:0; background:#183044; color:white; font:32px system-ui }
  h1,p,button { margin:32px } canvas { position:fixed; right:0; bottom:0 }
  </style><h1>SuiteCut native browser</h1><p>CEF frames + native PCM audio</p>
  <button onclick="this.textContent='Native click passed'">Test input</button>
  <canvas width="300" height="180"></canvas><script>
  const canvas=document.querySelector('canvas'), ctx=canvas.getContext('2d');
  let frame=0; function draw(){frame++;ctx.fillStyle=frame%60<30?'#ffc600':'#0099ff';ctx.fillRect(0,0,300,180);ctx.fillStyle='#000';ctx.fillText(String(frame),10,30);requestAnimationFrame(draw)}draw();
  const audio=new AudioContext({sampleRate:48000}), oscillator=audio.createOscillator(),gain=audio.createGain();
  gain.gain.value=.08; oscillator.frequency.value=440;oscillator.connect(gain).connect(audio.destination);oscillator.start();
  </script>`)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const address = server.address()
assert(address && typeof address !== 'string')
const url = `http://127.0.0.1:${address.port}`
const controller = new AbortController()
const timeout = setTimeout(() => controller.abort(), 90_000)
const sources = []
const evaluation = z.object({ result: z.object({ value: z.json() }) })
try {
  const source = await launchNativeBrowser({
    executablePath,
    width: 1280,
    height: 720,
    framesPerSecond: 60,
    signal: controller.signal,
  })
  sources.push(source)
  let frameCount = 0
  let audioCount = 0
  let audibleSamples = 0
  let latest
  const timestamps = []
  source.onFrame((frame) => {
    frameCount++
    latest = frame
    timestamps.push(frame.timestampMs)
  })
  source.onAudio((packet) => {
    audioCount++
    for (let i = 0; i < packet.data.length; i += 2)
      if (Math.abs(packet.data.readInt16LE(i)) > 100) audibleSamples++
  })
  await source.navigate(url)
  await source.sendDevToolsCommand('Runtime.evaluate', {
    expression: 'document.cookie="suitecut_isolation=first"',
  })
  const result = await source.sendDevToolsCommand('Runtime.evaluate', {
    expression: '[innerWidth,innerHeight,devicePixelRatio]',
    returnByValue: true,
  })
  assert.deepEqual(evaluation.parse(result).result.value, [1280, 720, 1])
  const bounds = await source.sendDevToolsCommand('Runtime.evaluate', {
    expression:
      '(()=>{const r=document.querySelector("button").getBoundingClientRect();return [r.x+r.width/2,r.y+r.height/2]})()',
    returnByValue: true,
  })
  const [x, y] = z.tuple([z.number(), z.number()]).parse(evaluation.parse(bounds).result.value)
  await source.sendDevToolsCommand('Input.dispatchMouseEvent', {
    type: 'mousePressed',
    button: 'left',
    clickCount: 1,
    x,
    y,
  })
  await source.sendDevToolsCommand('Input.dispatchMouseEvent', {
    type: 'mouseReleased',
    button: 'left',
    clickCount: 1,
    x,
    y,
  })
  const clicked = await source.sendDevToolsCommand('Runtime.evaluate', {
    expression: 'document.querySelector("button").textContent',
    returnByValue: true,
  })
  assert.equal(evaluation.parse(clicked).result.value, 'Native click passed')
  const dialog = await source.sendDevToolsCommand('Runtime.evaluate', {
    expression: 'alert("verification");42',
    returnByValue: true,
  })
  assert.equal(
    evaluation.parse(dialog).result.value,
    42,
    'JavaScript dialog blocked the native source',
  )
  await Promise.race([delay(5000, undefined, { signal: controller.signal }), source.failure])
  assert(frameCount > 30, `Only ${frameCount} native frames arrived`)
  assert(audioCount > 20 && audibleSamples > 1000, 'No audible native PCM captured')
  const captured = z
    .object({
      data: z.instanceof(Buffer),
      width: z.number(),
      height: z.number(),
      droppedFrames: z.number(),
    })
    .parse(latest)
  assert.equal(captured.width, 1280)
  assert.equal(captured.height, 720)
  assert(timestamps.every((value, index) => index === 0 || value >= timestamps[index - 1]))
  const rgba = Buffer.from(captured.data)
  for (let i = 0; i < rgba.length; i += 4) {
    const blue = rgba[i]
    rgba[i] = rgba[i + 2]
    rgba[i + 2] = blue
  }
  await imageProcessor(rgba, {
    raw: { width: captured.width, height: captured.height, channels: 4 },
  })
    .png()
    .toFile(`${directory}/native-frame.png`)
  await assert.rejects(source.navigate(`${url}/missing`), /navigation failed/)
  const before = frameCount
  await source.navigate(url)
  await delay(500)
  assert(frameCount > before, 'Capture did not survive navigation')
  const scaled = await launchNativeBrowser({
    executablePath,
    width: 640,
    height: 360,
    deviceScaleFactor: 2,
    signal: controller.signal,
  })
  sources.push(scaled)
  await scaled.navigate(url)
  const cookies = await scaled.sendDevToolsCommand('Runtime.evaluate', {
    expression: 'document.cookie',
    returnByValue: true,
  })
  assert.equal(evaluation.parse(cookies).result.value, '', 'Native sources shared browser storage')
  const dimensions = await scaled.sendDevToolsCommand('Runtime.evaluate', {
    expression: '[innerWidth,innerHeight,devicePixelRatio]',
    returnByValue: true,
  })
  assert.deepEqual(evaluation.parse(dimensions).result.value, [640, 360, 2])
  const failure = assert.rejects(scaled.failure, /Native browser/)
  await scaled.sendDevToolsCommand('Page.crash').catch(() => undefined)
  await failure
  await scaled.closed
  await source.close()
  await source.close()
  const highResolution = await launchNativeBrowser({
    executablePath,
    width: 1920,
    height: 1080,
    deviceScaleFactor: 2,
    signal: controller.signal,
  })
  sources.push(highResolution)
  const highResolutionFrame = new Promise((resolve) => {
    highResolution.onFrame((frame) => {
      if (
        frame.width === 3840 &&
        frame.height === 2160 &&
        frame.data[0] === 68 &&
        frame.data[1] === 48 &&
        frame.data[2] === 24
      )
        resolve(frame.data.length)
    })
  })
  await highResolution.navigate(url)
  assert.equal(await Promise.race([highResolutionFrame, highResolution.failure]), 3840 * 2160 * 4)
  await highResolution.close()
  const report = {
    cefVersion: source.cefVersion,
    frameCount,
    audioCount,
    audibleSamples,
    droppedFrames: captured.droppedFrames,
    navigation: 'passed',
    nativeMouseInput: 'passed',
    dialogSuppression: 'passed',
    deviceScale: 'passed',
    highResolution: '3840x2160 passed',
    profileIsolation: 'passed',
    rendererCrash: 'passed',
    shutdown: 'passed',
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  clearTimeout(timeout)
  await Promise.all(sources.map((source) => source.close()))
  server.closeAllConnections()
  await new Promise((done) => server.close(done))
}
