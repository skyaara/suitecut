// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-drag-sequence',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  width: 640,
  height: 420,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(2000)
const fixture = `<div id=source draggable=true style="position:fixed;left:10px;top:20px;width:100px;height:80px;background:blue">Source</div><div id=target style="position:fixed;left:240px;top:180px;width:100px;height:80px;background:gray">Target</div><script>
globalThis.realEvents=[];globalThis.realDrop=null;globalThis.allowedDrop=true;
for(const type of ['mousemove','mousedown','mouseup','dragstart','dragenter','dragover','drop','dragend'])document.addEventListener(type,event=>realEvents.push({type:event.type,trusted:event.isTrusted}),true);
document.querySelector('#source').addEventListener('dragstart',event=>{event.dataTransfer.effectAllowed='copy';event.dataTransfer.setData('text/plain','Actual native drag bytes')});
document.querySelector('#target').addEventListener('dragover',event=>{event.dataTransfer.dropEffect=allowedDrop?'copy':'none';event.preventDefault()});
document.querySelector('#target').addEventListener('drop',event=>{event.preventDefault();realDrop=event.dataTransfer.getData('text/plain');event.currentTarget.appendChild(document.querySelector('#source'))});
</script>`
const eventSchema = z.array(z.object({ type: z.string(), trusted: z.boolean() }))
const expected = [
  'mousemove',
  'mousedown',
  'mousemove',
  'dragstart',
  'dragenter',
  'dragover',
  'drop',
  'dragend',
]
const ownership = z.object({
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  inputWork: z.number(),
  formClicks: z.number(),
  mouseButtons: z.number(),
  pressedKeys: z.number(),
  nativeDragOperations: z.number(),
  nativeManualDragActive: z.boolean(),
  pendingDevTools: z.number(),
})
/** @type {unknown[]} */
const errors = []
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const observations = []
/**
 * Reads actual document observations without creating or dispatching events.
 * @param {import('../dist/native-page.js').NativePage} owner - Actual owning Page or Frame.
 */
async function actual(owner) {
  const events = eventSchema.parse(await owner.evaluate('realEvents'))
  assert(events.every((event) => event.trusted))
  observations.push(events)
  return events.map((event) => event.type)
}
try {
  await page.setContent(fixture)
  await page.hover('#source')
  await page.mouse.down()
  await page.hover('#target')
  await page.mouse.up()
  assert.deepEqual(await actual(page), expected)
  assert.equal(await page.evaluate('realDrop'), 'Actual native drag bytes')
  checks.push(
    'Actual manual drag emits exactly one native dragover negotiation before drop, with genuine bytes and trusted events',
  )

  await page.setContent(fixture)
  await page.hover('#source')
  await page.mouse.down()
  await page.hover('#target')
  assert.deepEqual(await actual(page), expected.slice(0, 5))
  await page.keyboard.press('Escape')
  await page.mouse.up()
  assert.deepEqual(await actual(page), [...expected.slice(0, 5), 'dragend', 'mouseup'])
  assert.equal(await page.evaluate('realDrop'), null)
  checks.push(
    'Native intercepted DragCancel emits actual dragend; the original key and subsequent physical mouse release still execute',
  )

  await page.setContent('<iframe name=child style="width:600px;height:400px;border:0"></iframe>')
  await page.frameLocator('iframe').locator('body').waitFor({ state: 'attached' })
  const frame = page.frame('child')
  assert(frame)
  await frame.setContent(fixture)
  await frame.hover('#source')
  await page.mouse.down()
  await frame.hover('#target')
  await page.mouse.up()
  assert.deepEqual(await actual(frame), expected)
  assert.equal(await frame.evaluate('realDrop'), 'Actual native drag bytes')
  checks.push(
    'Same-frame child drag preserves real target ownership and exact trusted native event sequence',
  )

  await page.setContent(fixture)
  await page.locator('#source').dragTo(page.locator('#target'))
  const helper = await actual(page)
  assert.deepEqual(
    helper.filter((type) => ['dragenter', 'dragover', 'drop', 'dragend'].includes(type)),
    ['dragenter', 'dragover', 'drop', 'dragend'],
  )
  assert.equal(await page.evaluate('realDrop'), 'Actual native drag bytes')
  checks.push(
    'Native locator drag helper delegates exactly one drop negotiation without a duplicate authored or protocol dragover',
  )

  await page.setContent(fixture)
  await page.evaluate('globalThis.allowedDrop=false;void 0')
  await page.hover('#source')
  await page.mouse.down()
  await page.hover('#target')
  await page.mouse.up()
  const denied = await actual(page)
  assert(!denied.includes('drop'))
  assert.equal(await page.evaluate('realDrop'), null)
  const deadline = performance.now() + 4000
  let state
  for (;;) {
    state = ownership.parse(await page.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0 || value === false)) break
    assert(
      performance.now() < deadline,
      'Actual drag ownership did not settle after native receipts',
    )
    await setImmediate()
  }
  checks.push(
    'Real target dropEffect remains authoritative and native drag/input ownership settles after actual completion',
  )
  assert.equal(checks.length, 5)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: true, checks, observations, state }, null, 2) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, checks, observations, error: String(error) }, null, 2) +
      '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, diagnostics, directory, errors)
}
console.info('Native trusted drag sequence5 passed')
