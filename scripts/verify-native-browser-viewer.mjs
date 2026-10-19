/* eslint-disable @typescript-eslint/no-unsafe-assignment, @typescript-eslint/no-unsafe-call, @typescript-eslint/no-unsafe-member-access, @typescript-eslint/no-unsafe-return, @typescript-eslint/no-unsafe-argument -- Loads a selectable emitted SDK artifact; actual behavior and response shapes are verified by assertions. */
/* global document, fetch, location, window, setTimeout */
// Real native viewer verification; uses SuiteCut itself as the human-facing client.
import assert from 'node:assert/strict'
import { once } from 'node:events'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer, request as httpRequest } from 'node:http'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'
import { pathToFileURL } from 'node:url'

const output = resolve(process.env.SUITECUT_VIEWER_PROOF ?? '.suitecut/viewer-proof')
const sdk = resolve(process.env.SUITECUT_VIEWER_SDK ?? 'dist/browser.js')
const executablePath = resolve(
  process.env.SUITECUT_NATIVE_EXECUTABLE ?? '.suitecut/native-build/suitecut-browser',
)
const { launchBrowser, createBrowserViewer } = await import(pathToFileURL(sdk))
await mkdir(output, { recursive: true })
const checks = [],
  events = []
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
  response.end(`<!doctype html><meta charset="utf-8"><title>SuiteCut live handover</title>
  <style>body{font:18px system-ui;margin:0;background:#f4f6fb;color:#172441}header{padding:28px 36px;background:#e4ebfb}h1{margin:0 0 8px;font-size:27px}p{margin:8px 0}main{padding:30px 36px;height:1900px}label{display:block;margin-bottom:12px}input{display:block;width:330px;padding:12px;font:18px system-ui;border:1px solid #8495b2;border-radius:8px;margin-top:8px}button{background:#294bb5;color:white;border:0;border-radius:8px;padding:13px 22px;font:16px system-ui;margin-top:14px}.card{padding:24px;background:white;border:1px solid #d3dced;border-radius:12px;max-width:560px}#drag{margin-top:20px;background:#c3d8ef;padding:18px;width:130px;user-select:none}</style>
  <header><h1>Same browser. Your turn.</h1><p>Human and AI share this native CEF session.</p></header><main><div class="card"><label>Your message<input id="message" placeholder="Type through the live viewer"></label><button id="save">Save message</button><p id="result">Waiting for input</p><div id="drag">Drag this area</div></div></main>
  <script>window.events=[];for(const type of ['click','keydown','keyup','input','mousedown','mouseup','mousemove','wheel'])document.addEventListener(type,e=>{const value={type,trusted:e.isTrusted,target:e.target.id,shift:e.shiftKey,buttons:e.buttons,text:document.querySelector('#message').value};window.events.push(value);fetch('/event',{method:'POST',body:JSON.stringify(value)})});document.querySelector('#save').onclick=()=>document.querySelector('#result').textContent='Saved: '+document.querySelector('#message').value;</script>`)
})
server.listen(0, '127.0.0.1')
await once(server, 'listening')
const app = `http://127.0.0.1:${server.address().port}`
let browser, clientBrowser, viewer, ui
const summary = { startedAt: new Date().toISOString(), checks, sdk, executablePath }
const until = async (predicate, message, timeout = 12000) => {
  const deadline = Date.now() + timeout
  while (Date.now() < deadline) {
    if (await predicate()) return
    await delay(40)
  }
  throw new Error(message)
}
try {
  browser = await launchBrowser({
    executablePath,
    headless: true,
    width: 960,
    height: 640,
    deviceScaleFactor: 2,
    framesPerSecond: 60,
  })
  const page = await browser.newPage()
  await page.goto(app)
  await page.setViewportSize({ width: 900, height: 600 })
  const observation = await page.ariaSnapshotJSON({ mode: 'ai', boxes: true })
  assert(observation.length)
  const inputBox = await page.locator('#message').boundingBox()
  const saveBox = await page.locator('#save').boundingBox()
  const dragBox = await page.locator('#drag').boundingBox()
  assert(inputBox && saveBox && dragBox)
  viewer = await createBrowserViewer({
    browser,
    page,
    framesPerSecond: 30,
    takeoverTimeoutMs: 2500,
  })
  const link = new URL(viewer.url),
    authorization = `Bearer ${link.hash.slice(1)}`,
    origin = link.origin
  const request = (path, options = {}) =>
    fetch(origin + path, {
      ...options,
      headers: {
        authorization,
        ...(options.body ? { 'content-type': 'application/json' } : {}),
        ...options.headers,
      },
    })
  assert.equal((await fetch(origin + '/state')).status, 401)
  assert.equal(
    (await request('/state', { headers: { origin: 'https://unrelated.invalid' } })).status,
    403,
  )
  const invalidHostStatus = await new Promise((resolve, reject) => {
    const req = httpRequest(
      origin + '/state',
      { headers: { host: 'unrelated.invalid', authorization } },
      (response) => {
        response.resume()
        response.once('end', () => resolve(response.statusCode))
      },
    )
    req.once('error', reject)
    req.end()
  })
  assert.equal(invalidHostStatus, 403)
  const clientId = (await (await request('/session', { method: 'POST' })).json()).clientId
  const secondId = (await (await request('/session', { method: 'POST' })).json()).clientId
  const state = () => request('/state').then((r) => r.json())
  const action = async (type, details = {}, id = clientId, expected = 200, revision) => {
    const current = await state()
    const response = await request('/action', {
      method: 'POST',
      body: JSON.stringify({
        clientId: id,
        revision: revision ?? current.revision,
        type,
        ...details,
      }),
    })
    const result = await response.json()
    assert.equal(response.status, expected, JSON.stringify(result))
    return result
  }
  assert.equal((await state()).width, 900)
  assert.equal((await state()).height, 600)
  checks.push('authentication-origin-host-and-resized-scale2-CSS-viewport')
  const outdated = (await state()).revision
  await action('takeover')
  await action('selectPage', { pageId: 1 })
  await action('navigate', { url: app + '/another-tab' })
  await action('reload')
  assert((await state()).pages.some((item) => item.id === 1 && item.url === app + '/another-tab'))
  await action('selectPage', { pageId: page.source.pageId })
  checks.push('same-helper-tab-selection-and-navigation')
  await action('text', { text: 'denied' }, secondId, 409)
  await action('text', { text: 'stale' }, clientId, 409, outdated)
  await assert.rejects(
    page.evaluate(() => document.title),
    /paused for human control/,
  )
  await assert.rejects(browser.newPage(), /paused for human control/)
  let resumed = false
  const ai = viewer.runAgent(() => {
    resumed = true
    return page.title()
  })
  await delay(80)
  assert.equal(resumed, false)
  await action('pointer', {
    action: 'down',
    x: inputBox.x + 10,
    y: inputBox.y + 10,
    button: 'left',
  })
  await action('pointer', { action: 'up', x: inputBox.x + 10, y: inputBox.y + 10, button: 'left' })
  await action('text', { text: 'Native human ✓' })
  await action('pointer', { action: 'down', x: saveBox.x + 10, y: saveBox.y + 10, button: 'left' })
  await action('pointer', { action: 'up', x: saveBox.x + 10, y: saveBox.y + 10, button: 'left' })
  await action('key', { action: 'down', key: 'Shift' })
  await action('resetInput')
  await action('key', { action: 'down', key: 'Unidentified' }, clientId, 400)
  await action('release')
  assert.equal(await ai, 'SuiteCut live handover')
  assert.equal(resumed, true)
  assert.equal(await page.locator('#message').inputValue(), 'Native human ✓')
  assert.equal(await page.locator('#result').innerText(), 'Saved: Native human ✓')
  assert.equal((await page.source.runAutomation('diagnostics')).pressedKeys, 0)
  checks.push('native-input-stale-owner-rejection-AI-pause-resume-and-key-release')
  // An AI-held modifier must never transfer into an untracked human lease.
  await page.keyboard.down('Shift')
  await action('takeover', {}, clientId, 409)
  assert.equal((await state()).mode, 'agent')
  await page.keyboard.up('Shift')
  checks.push('held-agent-input-refuses-takeover')
  // Native timeouts retire logically first: the physical probe must remain busy.
  const timeoutStarted = performance.now()
  page.setDefaultTimeout(25)
  await assert.rejects(
    page.evaluate(() => new Promise((resolve) => setTimeout(() => resolve(7), 500))),
    /timed out|timeout|expired/i,
  )
  page.setDefaultTimeout(30000)
  await action('takeover')
  summary.timeoutThroughHandoverMs = performance.now() - timeoutStarted
  assert(
    summary.timeoutThroughHandoverMs >= 250,
    'Takeover preceded the outstanding renderer callback',
  )
  await action('release')
  checks.push('timeout-followed-by-native-idle-handover')
  let finish
  const step = viewer.runAgent(
    () =>
      new Promise((resolve) => {
        finish = resolve
      }),
  )
  await until(() => !!finish, 'Agent step did not enter')
  const transfer = action('takeover')
  await until(async () => (await state()).mode === 'taking-over', 'Takeover did not wait')
  assert.equal((await state()).mode, 'taking-over')
  finish()
  await step
  await transfer
  await action('release')
  checks.push('takeover-waits-existing-agent-step')
  // Drive the actual web UI from another native helper, with genuine input.
  clientBrowser = await launchBrowser({
    executablePath,
    headless: true,
    width: 1440,
    height: 1000,
    framesPerSecond: 60,
  })
  ui = await clientBrowser.newPage()
  await ui.goto(viewer.url)
  await until(
    async () =>
      await ui.evaluate(() => document.querySelector('#screen').classList.contains('has-frame')),
    'Viewer did not render a JPEG frame',
  )
  assert.equal(await ui.evaluate(() => location.hash), '')
  await writeFile(output + '/viewer-watching.png', await ui.screenshot())
  await ui.locator('#control').click()
  await until(
    async () => (await ui.locator('#status').innerText()) === 'You have control',
    'Web UI takeover failed',
  )
  await ui.locator('#screen.has-frame.interactive').waitFor({ state: 'visible', timeout: 12000 })
  const canvas = await ui.locator('#screen').boundingBox()
  assert(canvas)
  const click = async (box) => {
    const x = canvas.x + ((box.x + box.width / 2) * canvas.width) / 900
    const y = canvas.y + ((box.y + box.height / 2) * canvas.height) / 600
    await ui.mouse.click(x, y)
  }
  await click(inputBox)
  await ui.keyboard.press('Control+a')
  await ui.keyboard.type('Viewer works')
  await ui.keyboard.insertText(' ✓')
  await click(saveBox)
  await until(
    () =>
      events.some((e) => e.type === 'click' && e.target === 'save' && e.text === 'Viewer works ✓'),
    'Web UI input did not reach original native page',
  )
  await writeFile(output + '/viewer-human.png', await ui.screenshot())
  const dragX = canvas.x + ((dragBox.x + 20) * canvas.width) / 900
  const dragY = canvas.y + ((dragBox.y + 20) * canvas.height) / 600
  await ui.mouse.move(dragX, dragY)
  await ui.mouse.down()
  await ui.mouse.move(dragX + 50, dragY + 15, { steps: 6 })
  await ui.mouse.up()
  await until(
    () => events.some((e) => e.type === 'mousemove' && e.target === 'drag' && e.buttons === 1),
    'Viewer drag did not reach native page',
  )
  await ui.mouse.move(canvas.x + canvas.width / 2, canvas.y + canvas.height / 2)
  await ui.mouse.wheel(0, 300)
  await until(
    () => events.some((e) => e.type === 'wheel'),
    'Viewer wheel did not reach native page',
  )
  await ui.locator('#control').click()
  await until(async () => (await state()).mode === 'agent', 'Web UI did not resume agent')
  assert.equal(await page.locator('#message').inputValue(), 'Viewer works ✓')
  const targetEvents = await page.evaluate(() => window.events)
  assert(
    targetEvents
      .filter((e) =>
        ['click', 'keydown', 'keyup', 'input', 'mousedown', 'mouseup', 'wheel'].includes(e.type),
      )
      .every((e) => e.trusted),
  )
  checks.push('real-viewer-UI-frames-pointer-drag-Unicode-keyboard-shortcut-wheel-and-resume')
  summary.targetEventCount = targetEvents.length
  summary.finalState = viewer.state()
  await action('takeover')
  await action('key', { action: 'down', key: 'Shift' })
  await action('pointer', { action: 'down', x: 20, y: 20, button: 'left' })
  const waitingStep = viewer.runAgent(() => page.title())
  const waitingRejected = assert.rejects(waitingStep, /viewer is closed/i)
  await viewer.close()
  await waitingRejected
  viewer = undefined
  assert.equal(await page.title(), 'SuiteCut live handover')
  const finalInput = await page.source.runAutomation('diagnostics')
  assert.equal(finalInput.pressedKeys, 0)
  assert.equal(finalInput.mouseButtons, 0)
  checks.push('viewer-close-preserves-browser-and-removes-gate')
  summary.status = 'PASS'
} catch (error) {
  summary.status = 'FAIL'
  summary.error = String(error)
  summary.stack = error.stack
  summary.recentTargetEvents = events.slice(-60)
  if (ui) {
    try {
      summary.viewerUI = await ui.evaluate(() => ({
        body: document.body.innerText,
        notice: document.querySelector('#notice').textContent,
        focus: document.activeElement?.id,
      }))
      await writeFile(output + '/viewer-failure.png', await ui.screenshot())
    } catch (captureError) {
      summary.uiCaptureError = String(captureError)
    }
  }
  process.exitCode = 1
} finally {
  try {
    await viewer?.close()
  } catch (error) {
    summary.closeError = String(error)
  }
  await clientBrowser?.close()
  await browser?.close()
  server.close()
  await once(server, 'close')
  summary.completedAt = new Date().toISOString()
  await writeFile(output + '/result.json', JSON.stringify(summary, null, 2) + '\n')
  process.stdout.write(JSON.stringify(summary, null, 2) + '\n')
}
