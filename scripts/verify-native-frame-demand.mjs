/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Selectable built SDK and passive native IPC observation; runtime outcomes are asserted. */
/* global fetch, document */
// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import childProcess from 'node:child_process'
import console from 'node:console'
import { mkdir, stat, writeFile } from 'node:fs/promises'
import { syncBuiltinESMExports } from 'node:module'
import { join, resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const executablePath = resolve(
  process.env.SUITECUT_NATIVE_EXECUTABLE ?? '.suitecut/memory-deep/runtime/suitecut-browser',
)
const sdkPath = resolve(
  process.env.SUITECUT_STRESS_SDK ?? 'packages/browser-control/dist/browser.js',
)
const output = resolve(
  process.env.SUITECUT_FRAME_DEMAND_PROOF ?? '.suitecut/external-native-perf/fixes/frame-demand',
)
await mkdir(output, { recursive: true })

// This observer forwards spawn unchanged. It reads only bounded JSON headers,
// skips frame payloads without retaining them, and never changes SDK callbacks.
const originalSpawn = childProcess.spawn
const wire = new Map(),
  commands = [],
  children = []
let processExit
childProcess.spawn = (...args) => {
  const child = originalSpawn(...args)
  if (resolve(String(args[0])) !== executablePath) return child
  children.push({ pid: child.pid })
  child.once('close', (code, signal) => {
    processExit = { code, signal }
  })
  const prefix = Buffer.alloc(4)
  let input = '',
    prefixAt = 0,
    header,
    headerAt = 0,
    payloadRemaining = 0,
    packet
  // Observe authored protocol writes rather than consuming the writable stream.
  const write = child.stdin.write.bind(child.stdin)
  child.stdin.write = (...values) => {
    input += String(values[0])
    for (let end = input.indexOf('\n'); end !== -1; end = input.indexOf('\n')) {
      const command = JSON.parse(input.slice(0, end))
      if (command.command === 'frameSubscription')
        commands.push({ ...command, acknowledged: false })
      input = input.slice(end + 1)
    }
    return write(...values)
  }
  const complete = () => {
    if (packet.type === 'frame') {
      const value = wire.get(packet.pageId ?? 1) ?? { frames: 0, payloadBytes: 0 }
      ++value.frames
      value.payloadBytes += packet.bytes
      wire.set(packet.pageId ?? 1, value)
    } else if (packet.type === 'response') {
      const command = commands.find((item) => item.id === packet.id)
      if (command) {
        command.acknowledged = true
        command.ok = packet.ok
      }
    }
    packet = undefined
  }
  child.stdout.on('data', (chunk) => {
    let at = 0
    while (at < chunk.length) {
      if (payloadRemaining) {
        const skip = Math.min(payloadRemaining, chunk.length - at)
        payloadRemaining -= skip
        at += skip
        if (!payloadRemaining) complete()
      } else if (header) {
        const take = Math.min(header.length - headerAt, chunk.length - at)
        chunk.copy(header, headerAt, at, at + take)
        headerAt += take
        at += take
        if (headerAt === header.length) {
          packet = JSON.parse(header.toString())
          header = undefined
          payloadRemaining = packet.bytes
          if (!payloadRemaining) complete()
        }
      } else {
        const take = Math.min(4 - prefixAt, chunk.length - at)
        chunk.copy(prefix, prefixAt, at, at + take)
        prefixAt += take
        at += take
        if (prefixAt === 4) {
          const size = prefix.readUInt32LE()
          assert(size > 0 && size <= 65536)
          header = Buffer.alloc(size)
          headerAt = 0
          prefixAt = 0
        }
      }
    }
  })
  return child
}
syncBuiltinESMExports()
const { launchBrowser, createBrowserViewer } = await import(pathToFileURL(sdkPath))
const checks = [],
  quietWindows = [],
  result = { status: 'FAIL', sdkPath, executablePath, checks, quietWindows, children, commands }
let browser, viewer
const counts = (page) => ({ ...(wire.get(page.source.pageId) ?? { frames: 0, payloadBytes: 0 }) })
const until = async (predicate, message, timeoutMs = 5000) => {
  const end = performance.now() + timeoutMs
  do {
    if (await predicate()) return
    await delay(15)
  } while (performance.now() < end)
  throw new Error(message)
}
const disabled = (page) =>
  commands.some(
    (command) =>
      command.pageId === page.source.pageId &&
      !command.enabled &&
      command.acknowledged &&
      command.ok,
  )
const assertQuiet = async (page, name) => {
  const beforeDrain = counts(page)
  // Disabling future production cannot retract a packet already queued or
  // being written. Allow a bounded drain, then require an entirely quiet window.
  await delay(80)
  const before = counts(page)
  await delay(180)
  assert.deepEqual(counts(page), before, `${name}: unsubscribed transport emitted a frame`)
  quietWindows.push({
    name,
    drainMs: 80,
    drainedFrames: before.frames - beforeDrain.frames,
    quietMs: 180,
    quietFrames: 0,
  })
  checks.push(name)
}
try {
  browser = await launchBrowser({
    executablePath,
    headless: true,
    width: 320,
    height: 180,
    framesPerSecond: 60,
  })
  assert.equal(
    browser.capabilities().frameSubscription,
    true,
    'Rebuild native helper for subscription support',
  )
  result.version = browser.version()
  const context = await browser.newContext(),
    page = await context.newPage()
  await page.setContent(
    '<!doctype html><body style="margin:0;background:rgb(240,30,60)"><h1>Static current paint</h1></body>',
  )
  await assertQuiet(page, 'initial-no-subscriber-has-zero-transport-frames')
  assert.equal(counts(page).frames, 0)
  const first = [],
    stop = page.source.onFrame((frame) =>
      first.push({ timestampMs: frame.timestampMs, firstPixel: [...frame.data.subarray(0, 4)] }),
    )
  await until(() => first.length > 0, 'Static page did not deliver enabled cached/fresh paint')
  stop()
  await until(() => disabled(page), 'Native unsubscribe was not acknowledged')
  await assertQuiet(page, 'unsubscribe-stops-wire-frames')
  const screenshot = await page.screenshot({ type: 'png' })
  assert.deepEqual([...screenshot.subarray(0, 8)], [137, 80, 78, 71, 13, 10, 26, 10])
  await writeFile(join(output, 'unsubscribed.png'), screenshot)
  await page.evaluate(() => {
    document.body.style.background = 'rgb(30,60,240)'
  })
  await assertQuiet(page, 'screenshot-and-DOM-updates-do-not-enable-transport')
  const resumed = [],
    stopResumed = page.source.onFrame((frame) =>
      resumed.push({ timestampMs: frame.timestampMs, firstPixel: [...frame.data.subarray(0, 4)] }),
    )
  assert.equal(resumed.length, 0, 'Resubscription replayed stale JS cache synchronously')
  await until(
    () => resumed.some((frame) => frame.firstPixel[0] > 200 && frame.firstPixel[2] < 80),
    'Resubscription did not deliver current blue native view',
  )
  stopResumed()
  await until(
    () =>
      commands.filter(
        (command) =>
          command.pageId === page.source.pageId && !command.enabled && command.acknowledged,
      ).length === 2,
    'Second unsubscribe not acknowledged',
  )
  checks.push('resubscribe-static-current-paint-without-stale-JS-replay')
  const second = await context.newPage()
  await second.setContent('<!doctype html><body>Independent page</body>')
  const secondFrames = [],
    stopSecond = second.source.onFrame((frame) => secondFrames.push(frame.timestampMs))
  await until(() => secondFrames.length > 0, 'Independent page did not enable transport')
  await assertQuiet(page, 'another-page-subscription-does-not-enable-first-page')
  stopSecond()
  await until(() => disabled(second), 'Independent page disable not acknowledged')
  viewer = await createBrowserViewer({ browser, page, framesPerSecond: 30 })
  const link = new URL(viewer.url)
  const response = await fetch(
    `${link.origin}/frame?revision=${viewer.state().revision}&after=0&wait=1`,
    {
      headers: { authorization: `Bearer ${link.hash.slice(1)}` },
      signal: AbortSignal.timeout(7000),
    },
  )
  assert.equal(response.status, 200)
  const jpeg = Buffer.from(await response.arrayBuffer())
  assert.deepEqual([...jpeg.subarray(0, 2)], [255, 216])
  result.viewerJPEGBytes = jpeg.length
  await viewer.close()
  viewer = undefined
  checks.push('viewer-attachment-enables-current-static-frame-and-returns-JPEG')
  const recordedContext = await browser.newContext({
    recordVideo: { dir: join(output, 'automatic-video'), size: { width: 64, height: 64 } },
  })
  const recorded = await recordedContext.newPage(),
    video = recorded.video()
  assert(video)
  await recorded.setContent(
    '<!doctype html><body style="background:#29e"><h1>Native-owned recording</h1></body>',
  )
  await delay(450)
  await assertQuiet(
    recorded,
    'native-automatic-recording-with-zero-JS-subscribers-has-zero-wire-frames',
  )
  assert.equal(counts(recorded).frames, 0)
  await recordedContext.close()
  const videoPath = await video.path()
  assert((await stat(videoPath)).size > 0)
  const probe = childProcess.spawnSync(
    '/usr/bin/ffprobe',
    [
      '-v',
      'error',
      '-count_frames',
      '-show_entries',
      'stream=codec_name,width,height,nb_read_frames',
      '-of',
      'json',
      videoPath,
    ],
    { encoding: 'utf8' },
  )
  assert.equal(probe.status, 0, probe.stderr)
  result.recording = { path: videoPath, ...JSON.parse(probe.stdout) }
  assert(
    result.recording.streams.some(
      (stream) =>
        stream.codec_name === 'vp9' &&
        stream.width === 64 &&
        stream.height === 64 &&
        Number(stream.nb_read_frames) > 0,
    ),
  )
  checks.push('automatic-native-recording-ffprobe-decodes-real-frames-without-raw-transport')
  await context.close()
  result.initialFrames = first
  result.resumedFrames = resumed
  result.wireByPage = Object.fromEntries(wire)
  result.status = 'PASS'
} catch (error) {
  result.error = { message: error.message, stack: error.stack }
  process.exitCode = 1
} finally {
  await viewer?.close()
  await browser?.close()
  result.processExit = processExit
  childProcess.spawn = originalSpawn
  syncBuiltinESMExports()
  await writeFile(join(output, 'result.json'), JSON.stringify(result, null, 2) + '\n')
  console.log(
    JSON.stringify({ status: result.status, checks, processExit, output, error: result.error }),
  )
}
