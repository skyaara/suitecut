// SPDX-License-Identifier: MIT
/* global document, navigator, location */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const executable = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const hash = async () =>
  createHash('sha256')
    .update(await readFile(executable))
    .digest('hex')
const binarySHA256 = await hash()
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: executable,
  headless: true,
  width: 320,
  height: 180,
  onDiagnostic: (message) => {
    stderr += message
  },
})
const eventSchema = z.object({
  type: z.string(),
  trusted: z.boolean(),
  target: z.string(),
  touches: z.number().optional(),
  changed: z.number().optional(),
  x: z.number().optional(),
  y: z.number().optional(),
  pointerType: z.string().optional(),
})
const page = createNativePage(source)
const checks = []
const fixture = `<button id="target" style="position:absolute;left:20px;top:20px;width:100px;height:80px">tap</button>
  <pre id="trace"></pre><script>
  const events=[];let blocked=false;
  for(const type of ['touchstart','touchend','touchcancel','pointerdown','pointerup','mousedown','mouseup','click'])
    document.addEventListener(type,event=>{
      const touch=event.changedTouches?.[0];
      events.push({type,trusted:event.isTrusted,target:event.target.id,
        ...(event.touches?{touches:event.touches.length,changed:event.changedTouches.length}:{}),
        ...(touch?{x:touch.clientX,y:touch.clientY}:{}),
        ...(event.pointerType?{pointerType:event.pointerType}:{})});
      document.querySelector('#trace').textContent=JSON.stringify(events);
      if(type==='touchstart'&&location.search==='?block'&&!blocked){
        blocked=true;const xhr=new XMLHttpRequest();xhr.open('GET','/held-touch',false);xhr.send();
      }
    });
  </script>`
let releaseTouch = () => undefined
let observeHeldTouch = () => undefined
const heldTouch = new Promise((resolve) => {
  observeHeldTouch = () => resolve(undefined)
})
const server = createServer((request, response) => {
  if (request.url === '/held-touch') {
    releaseTouch = () => {
      releaseTouch = () => undefined
      if (response.destroyed || response.writableEnded) return
      response.writeHead(200)
      response.end('released')
    }
    observeHeldTouch()
    return
  }
  response.writeHead(200, { 'content-type': 'text/html' })
  if (request.url === '/destination') {
    response.end('<title>Touch destination</title>')
    return
  }
  response.end(fixture)
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const port = z.object({ port: z.number() }).parse(server.address()).port
const origin = `http://localhost:${port}`
try {
  await page.setContent(fixture)
  await assert.rejects(page.touchscreen.tap(50, 50), /hasTouch/u)
  assert.equal(await page.evaluate(() => navigator.maxTouchPoints), 0)
  checks.push('disabled native touch rejects before input dispatch')
  await page.touchscreen.setEnabled(true)
  assert.equal(await page.evaluate(() => navigator.maxTouchPoints), 1)
  await page.touchscreen.tap(50, 50)
  const text = z
    .string()
    .parse(await page.evaluate(() => document.querySelector('#trace')?.textContent))
  const events = z.array(eventSchema).parse(JSON.parse(text))
  const touches = events.filter((event) => event.type.startsWith('touch'))
  assert.deepEqual(
    touches.map((event) => event.type),
    ['touchstart', 'touchend'],
  )
  assert(
    touches.every(
      (event) => event.trusted && event.target === 'target' && event.x === 50 && event.y === 50,
    ),
  )
  assert.equal(touches[0].touches, 1)
  assert.equal(touches[1].touches, 0)
  assert.equal(touches[0].changed, 1)
  assert.equal(touches[1].changed, 1)
  assert(
    events.some(
      (event) => event.type === 'pointerdown' && event.pointerType === 'touch' && event.trusted,
    ),
  )
  assert.equal(events.filter((event) => event.type === 'click' && event.trusted).length, 1)
  checks.push('actual trusted touch, pointer and compatibility click target original coordinates')
  await page.locator('#target').tap({ trial: true })
  assert.equal(await page.locator('#trace').textContent(), text)
  await page.locator('#target').tap({ position: { x: 10, y: 12 } })
  await assert.rejects(page.locator('button').or(page.locator('pre')).tap(), /Strict locator/u)
  await page.evaluate(() => document.querySelector('#target')?.setAttribute('disabled', ''))
  await assert.rejects(page.locator('#target').tap({ timeout: 100 }), /Timeout/u)
  await page.evaluate(() => document.querySelector('#target')?.removeAttribute('disabled'))
  const handle = await page.$('#target')
  assert(handle)
  await handle.tap()
  await handle.dispose()
  checks.push('locator and handle taps use native strictness, actionability, trial and positions')
  await page.goto(origin)
  await page.evaluate(() => {
    const target = document.querySelector('#target')
    target?.addEventListener('click', () => {
      location.href = '/destination'
    })
  })
  await page.locator('#target').tap()
  assert.equal(page.url(), `${origin}/destination`)
  await page.waitForLoadState('load')
  assert.equal(await page.title(), 'Touch destination')
  checks.push('touch-triggered navigation uses the genuine native intent barrier')
  await page.goto(`${origin}/?block`)
  const canceled = source.runAutomation('input', {
    kind: 'touch',
    action: 'tap',
    x: 50,
    y: 50,
    timeoutMs: 500,
  })
  const cancellation = assert.rejects(canceled, /Timeout/u)
  await heldTouch
  await cancellation
  const retained = z
    .object({ pendingTouchPhysicalCommands: z.literal(1), otherOperations: z.literal(0) })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  const nextTap = page.touchscreen.tap(65, 65)
  const queued = z
    .object({ pendingTouchPhysicalCommands: z.literal(1), queuedTouchCommands: z.literal(1) })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  releaseTouch()
  await nextTap
  const after = z
    .object({
      pendingTouchPhysicalCommands: z.literal(0),
      queuedTouchCommands: z.literal(0),
      touchContactHeld: z.literal(false),
      otherOperations: z.literal(0),
    })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  checks.push(
    'actual held renderer dispatch survives logical timeout and retires before the next contact',
  )
  await page.touchscreen.setEnabled(false)
  assert.equal(await page.evaluate(() => navigator.maxTouchPoints), 0)
  await assert.rejects(page.touchscreen.tap(50, 50), /hasTouch/u)
  const diagnostics = z
    .object({
      pendingTouchPhysicalCommands: z.literal(0),
      queuedTouchCommands: z.literal(0),
      touchContactHeld: z.literal(false),
      touchEnabled: z.literal(false),
      otherOperations: z.literal(0),
    })
    .passthrough()
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(await hash(), binarySHA256)
  assert.equal(stderr, '')
  await mkdir('.suitecut/native-touch', { recursive: true })
  await writeFile(
    '.suitecut/native-touch/report.json',
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        cefVersion: source.cefVersion,
        checks,
        events,
        diagnostics,
        retained,
        queued,
        after,
        stderr,
      },
      null,
      2,
    ) + '\n',
  )
  console.log('Native touch verified: .suitecut/native-touch/report.json')
} finally {
  releaseTouch()
  await source.close()
  server.closeAllConnections()
  await new Promise((resolve, reject) =>
    server.close((error) => (error ? reject(error) : resolve(undefined))),
  )
}
