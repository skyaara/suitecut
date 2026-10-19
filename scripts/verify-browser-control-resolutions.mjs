/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Verifies a selectable emitted SDK against the real native engine. */
/* global document, window, getComputedStyle, fetch, Buffer, console */
// Native target + native viewer client. No external automation framework.
import assert from 'node:assert/strict'
import { once } from 'node:events'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { resolve } from 'node:path'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

import imageCodec from 'sharp'

const output = resolve(process.env.SUITECUT_VIEWER_PROOF ?? '.suitecut/browser-control-proof')
const sdk = resolve(process.env.SUITECUT_VIEWER_SDK ?? 'packages/browser-control/dist/browser.js')
const executablePath = resolve(
  process.env.SUITECUT_NATIVE_EXECUTABLE ?? '.suitecut/native-build/suitecut-browser',
)
const { launchBrowser, createBrowserViewer } = await import(pathToFileURL(sdk))
await mkdir(output, { recursive: true })
const events = [],
  checks = [],
  sizes = []
const server = createServer((request, response) => {
  if (request.url === '/event') {
    let data = ''
    request.on('data', (chunk) => {
      data += chunk
    })
    request.on('end', () => {
      events.push(JSON.parse(data))
      response.end('ok')
    })
    return
  }
  response.setHeader('content-type', 'text/html')
  response.end(`<!doctype html><meta charset="utf-8"><title>Native resolution proof</title>
<style>*{box-sizing:border-box}html,body{margin:0;width:100%;height:100%;overflow:hidden;background:rgb(39,143,111);color:white;font:20px system-ui}h1,p{margin:30px}#edge{position:fixed;bottom:14px;right:14px;width:100px;height:60px;border:0;background:#f4cc67;color:#142341;font:16px system-ui}</style>
<h1>Native browser control</h1><p id="size"></p><button id="edge">Click edge</button>
<script>function report(type,e){const value={type,width:innerWidth,height:innerHeight,trusted:e?e.isTrusted:null,shift:e?e.shiftKey:false,buttons:e?e.buttons:0};document.querySelector('#size').textContent=innerWidth+' × '+innerHeight;fetch('/event',{method:'POST',body:JSON.stringify(value)})}addEventListener('resize',()=>report('resize'));document.querySelector('#edge').addEventListener('click',e=>report('click',e));report('ready');</script>`)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const app = `http://127.0.0.1:${server.address().port}`
const until = async (predicate, message, timeout = 15000) => {
  const deadline = Date.now() + timeout
  while (Date.now() < deadline) {
    if (await predicate()) return
    await delay(50)
  }
  throw new Error(message)
}
let browser, clientBrowser, viewer, ui, off
const summary = { sdk, executablePath, startedAt: new Date().toISOString(), checks, sizes }
try {
  browser = await launchBrowser({
    executablePath,
    headless: true,
    width: 1280,
    height: 720,
    framesPerSecond: 30,
    pixelFormat: 'bgra',
  })
  const page = await browser.newPage()
  await page.goto(app)
  let frame
  off = page.source.onFrame((value) => {
    frame = {
      width: value.width,
      height: value.height,
      viewport: value.viewport,
      contentRect: value.contentRect,
    }
  })
  viewer = await createBrowserViewer({ browser, page })
  const link = new URL(viewer.url),
    origin = link.origin,
    authorization = `Bearer ${link.hash.slice(1)}`
  const request = (path, options = {}) =>
    fetch(origin + path, {
      ...options,
      headers: {
        authorization,
        ...(options.body ? { 'content-type': 'application/json' } : {}),
        ...options.headers,
      },
    })
  const state = () => request('/state').then((response) => response.json())
  const id = (await (await request('/session', { method: 'POST' })).json()).clientId
  const action = async (type, details = {}, expected = 200, revision) => {
    const response = await request('/action', {
      method: 'POST',
      body: JSON.stringify({
        clientId: id,
        revision: revision ?? (await state()).revision,
        type,
        ...details,
      }),
    })
    const result = await response.json()
    assert.equal(response.status, expected, JSON.stringify(result))
    return result
  }
  await until(async () => (await state()).canResize, 'No native geometry support')
  const screenshotFrame = async (width, height, name) => {
    await until(
      () => frame?.viewport?.width === width && frame?.viewport?.height === height,
      'Native paint geometry did not settle',
    )
    const expected = frame.contentRect
    let jpeg
    await until(async () => {
      const current = await state()
      const response = await request(`/frame?after=-1&revision=${current.revision}`)
      if (response.status !== 200) return false
      jpeg = Buffer.from(await response.arrayBuffer())
      return true
    }, 'Viewer did not publish resized JPEG')
    const { data, info } = await imageCodec(jpeg)
      .removeAlpha()
      .raw()
      .toBuffer({ resolveWithObject: true })
    assert.equal(info.width, expected.width)
    assert.equal(info.height, expected.height)
    // A near-corner pixel remains green: letterbox pixels must have been removed.
    for (const [x, y] of [
      [2, 2],
      [info.width - 3, 2],
      [2, info.height - 3],
    ]) {
      const offset = (y * info.width + x) * info.channels
      assert(
        data[offset + 1] > 90 && data[offset + 2] > 65,
        `Unexpected letterbox pixel at ${x},${y}`,
      )
    }
    assert(Math.abs(info.width / info.height - width / height) < 0.01)
    await writeFile(`${output}/${name}.jpg`, jpeg)
    sizes.push({
      name,
      width,
      height,
      canvas: { width: frame.width, height: frame.height },
      contentRect: expected,
      jpeg: { width: info.width, height: info.height },
    })
  }
  await action('resize', { width: 390, height: 844, mode: 'fixed' }, 409)
  await action('takeover')
  const oldRevision = (await state()).revision
  await action('key', { action: 'down', key: 'Shift' })
  await action('resize', { width: 390, height: 844, mode: 'fixed' })
  await action('resize', { width: 777, height: 555, mode: 'fixed' }, 409, oldRevision)
  await screenshotFrame(390, 844, 'portrait-api')
  const clickCount = events.filter((event) => event.type === 'click').length
  for (const command of ['down', 'up'])
    await action('pointer', { action: command, x: 326, y: 800, button: 'left' })
  await until(
    () => events.filter((event) => event.type === 'click').length > clickCount,
    'Portrait edge click missed',
  )
  assert.equal(events.filter((event) => event.type === 'click').at(-1).shift, false)
  await action('release')
  assert.deepEqual(
    await page.evaluate(() => ({ width: window.innerWidth, height: window.innerHeight })),
    { width: 390, height: 844 },
  )
  checks.push('native-portrait-crop-input-mapping-held-key-release-and-stale-revision')

  clientBrowser = await launchBrowser({
    executablePath,
    headless: true,
    width: 1440,
    height: 1000,
    framesPerSecond: 30,
  })
  ui = await clientBrowser.newPage()
  await ui.goto(viewer.url)
  const drawn = async (width, height) => {
    await until(async () => {
      const current = await state()
      return (
        current.width === width &&
        current.height === height &&
        (await ui.evaluate(
          ({ width, height, revision }) => {
            const canvas = document.querySelector('#screen')
            return (
              canvas.classList.contains('has-frame') &&
              canvas.classList.contains('interactive') &&
              canvas.width === width &&
              canvas.height === height &&
              Number(canvas.dataset.revision) === revision
            )
          },
          { width, height, revision: current.revision },
        ))
      )
    }, `Current ${width}x${height} viewer frame was not drawn`)
  }
  await ui.locator('#control').click()
  await drawn(390, 844)
  const edgeClick = async (width, height) => {
    await drawn(width, height)
    const canvas = await ui.locator('#screen').boundingBox()
    const count = events.filter((event) => event.type === 'click').length
    await ui.mouse.click(
      canvas.x + ((width - 64) * canvas.width) / width,
      canvas.y + ((height - 44) * canvas.height) / height,
    )
    await until(
      () => events.filter((event) => event.type === 'click').length > count,
      'Viewer edge click missed after resize',
    )
    const event = events.filter((event) => event.type === 'click').at(-1)
    assert.deepEqual([event.width, event.height, event.trusted], [width, height, true])
  }
  await edgeClick(390, 844)
  await ui.locator('#resolution').selectOption('1920x1080')
  await drawn(1920, 1080)
  await screenshotFrame(1920, 1080, 'desktop-preset')
  await edgeClick(1920, 1080)
  for (const [width, height] of [
    [2560, 1440],
    [3840, 2160],
  ]) {
    await ui.locator('#resolution').selectOption(`${width}x${height}`)
    await drawn(width, height)
    await edgeClick(width, height)
  }
  await ui.locator('#resolution-width').fill('777')
  await ui.locator('#resolution-height').fill('555')
  await ui.locator('#resolution-apply').click()
  await drawn(777, 555)
  await screenshotFrame(777, 555, 'custom-odd')
  await edgeClick(777, 555)
  checks.push('real-viewer-preset-custom-odd-size-and-trusted-edge-clicks')
  await ui.setViewportSize({ width: 1100, height: 800 })
  await delay(600)
  assert.deepEqual([(await state()).width, (await state()).height], [777, 555])
  checks.push('fixed-resolution-survives-viewer-window-resize')
  await ui.locator('#resolution').selectOption('auto')
  const autoSize = () =>
    ui.evaluate(() => {
      const stage = document.querySelector('#stage'),
        style = getComputedStyle(stage)
      return {
        width: Math.floor(
          stage.clientWidth - parseFloat(style.paddingLeft) - parseFloat(style.paddingRight),
        ),
        height: Math.floor(
          stage.clientHeight - parseFloat(style.paddingTop) - parseFloat(style.paddingBottom),
        ),
      }
    })
  for (const size of [
    { width: 1100, height: 800 },
    { width: 800, height: 600 },
    { width: 1320, height: 900 },
  ]) {
    await ui.setViewportSize(size)
    await delay(250)
    const expected = await autoSize()
    await drawn(expected.width, expected.height)
    await edgeClick(expected.width, expected.height)
  }
  const settled = await state()
  await delay(800)
  assert.equal((await state()).revision, settled.revision, 'Auto fit oscillated')
  await writeFile(output + '/viewer-auto.png', await ui.screenshot())
  checks.push('auto-fit-three-window-sizes-stable-no-resize-feedback-loop')
  await action('resize', { width: 1280, height: 720, mode: 'fixed' }, 409)
  await ui.locator('#control').click()
  await until(async () => (await state()).mode === 'agent', 'AI did not resume')
  await page.setViewportSize({ width: 990, height: 660 })
  await until(async () => (await state()).width === 990, 'SDK viewport did not synchronize')
  await screenshotFrame(990, 660, 'agent-resize')
  checks.push('spectator-cannot-resize-and-agent-viewport-changes-refresh-frame-revision')
  await viewer.close()
  viewer = undefined
  off()
  off = undefined
  await browser.close()
  browser = undefined

  browser = await launchBrowser({
    executablePath,
    headless: true,
    width: 640,
    height: 360,
    deviceScaleFactor: 2,
    framesPerSecond: 30,
  })
  const scaled = await browser.newPage()
  await scaled.goto(app)
  viewer = await createBrowserViewer({ browser, page: scaled })
  const scaledLink = new URL(viewer.url),
    scaledHeaders = {
      authorization: `Bearer ${scaledLink.hash.slice(1)}`,
      'content-type': 'application/json',
    }
  const scaledRequest = (path, options = {}) =>
    fetch(scaledLink.origin + path, { ...options, headers: scaledHeaders })
  const scaledId = (await (await scaledRequest('/session', { method: 'POST' })).json()).clientId
  const scaledAction = async (type, details = {}, status = 200) => {
    const current = await (await scaledRequest('/state')).json()
    const response = await scaledRequest('/action', {
      method: 'POST',
      body: JSON.stringify({ clientId: scaledId, revision: current.revision, type, ...details }),
    })
    const result = await response.json()
    assert.equal(response.status, status, JSON.stringify(result))
    return result
  }
  await until(() => viewer.state().canResize, 'Scaled native geometry missing')
  assert.deepEqual([viewer.state().maxWidth, viewer.state().maxHeight], [1920, 1080])
  await scaledAction('takeover')
  await scaledAction('resize', { width: 3840, height: 2160, mode: 'fixed' }, 400)
  const clamped = await scaledAction('resize', { width: 3840, height: 2160, mode: 'auto' })
  assert.deepEqual([clamped.width, clamped.height], [1920, 1080])
  await scaledAction('release')
  assert.deepEqual(
    await scaled.evaluate(() => [window.innerWidth, window.innerHeight, window.devicePixelRatio]),
    [1920, 1080, 2],
  )
  checks.push('DPR2-physical-limits-fixed-rejection-and-auto-clamp')
  await viewer.close()
  viewer = undefined
  const fractional = await browser.newPage({
    viewport: { width: 641, height: 479 },
    deviceScaleFactor: 1.5,
  })
  await fractional.goto(app)
  viewer = await createBrowserViewer({ browser, page: fractional })
  await until(() => viewer.state().canResize, 'Fractional native geometry missing')
  assert.deepEqual([viewer.state().maxWidth, viewer.state().maxHeight], [2560, 1440])
  checks.push('fractional-DPR-exact-limits-despite-rounded-capture-dimensions')
  summary.status = 'PASS'
} catch (error) {
  summary.status = 'FAIL'
  summary.error = String(error)
  summary.stack = error.stack
  if (ui) {
    try {
      summary.viewerUI = await ui.evaluate(() => document.body.innerText)
      await writeFile(output + '/failure.png', await ui.screenshot())
    } catch {
      // Preserve the original assertion if the viewer already closed.
    }
  }
  process.exitCode = 1
} finally {
  off?.()
  await viewer?.close().catch((error) => {
    summary.closeError = String(error)
    process.exitCode = 1
  })
  await clientBrowser?.close().catch((error) => {
    summary.clientCloseError = String(error)
    process.exitCode = 1
  })
  await browser?.close().catch((error) => {
    summary.browserCloseError = String(error)
    process.exitCode = 1
  })
  server.closeAllConnections()
  await new Promise((resolve) => server.close(resolve))
  summary.finishedAt = new Date().toISOString()
  await writeFile(output + '/result.json', JSON.stringify(summary, null, 2) + '\n')
  console.log(JSON.stringify(summary, null, 2))
}
