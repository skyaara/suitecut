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
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-filechooser-lifetime',
)
await mkdir(directory, { recursive: true })
const file = resolve(directory, 'genuine-chooser.txt')
await writeFile(file, 'Actual chooser file bytes')
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 640,
  height: 360,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(1800)
/** @type {unknown[]} */
const errors = []
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const observations = []
/** @type {Set<import('../dist/native.js').NativeElementHandle>} */
const inputs = new Set()
/**
 * Retains each delivered actual input until its acknowledged disposal.
 * @param {import('../dist/native.js').NativeFileChooser} chooser
 */
const listener = (chooser) => {
  inputs.add(chooser.element())
}
const ownership = z.object({
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  otherOperations: z.number(),
  formClicks: z.number(),
  inputWork: z.number(),
  pendingDevTools: z.number(),
  pendingUploadGroups: z.number(),
})
async function settle() {
  const deadline = performance.now() + 4000
  for (;;) {
    const actual = ownership.parse(await page.runAutomation('diagnostics'))
    if (Object.values(actual).every((value) => value === 0)) return actual
    assert(performance.now() < deadline, 'Actual chooser ownership did not settle by real ACK')
    await setImmediate()
  }
}
try {
  await page.setContent(
    `<input id=root type=file><iframe name=child></iframe><script>globalThis.fileEvents=[];for(const type of ['input','change'])document.addEventListener(type,event=>fileEvents.push({type,id:event.target.id,trusted:event.isTrusted}),true)</script>`,
  )
  const child = page.frame('child')
  assert(child)
  await child.setContent(
    `<div>Click me</div><script>document.querySelector('div').addEventListener('click',()=>{const input=document.createElement('input');input.type='file';input.click();window.parent.__done=true;window.parent.document.querySelector('iframe').remove()})</script>`,
  )
  page.on('filechooser', listener)
  await child.click('div')
  const done = await page.waitForFunction(() => Reflect.get(globalThis, '__done') === true)
  await done.dispose()
  assert.equal(child.isDetached(), true)
  assert.equal(await page.locator('iframe').count(), 0)
  assert.deepEqual(
    await page.evaluate(`({files:document.querySelector('#root').files.length,events:fileEvents})`),
    { files: 0, events: [] },
  )
  observations.push({ phase: 'after detached child', delivered: inputs.size })
  checks.push(
    'Original detached-child chooser fixture completes its real native click and frame removal without mutating the root file input',
  )

  const pending = page.waitForEvent('filechooser', { timeout: 1200 })
  const [chooser] = await Promise.all([pending, page.click('#root')])
  inputs.add(chooser.element())
  assert.equal(chooser.page(), page)
  assert.equal(await chooser.element().getAttribute('id'), 'root')
  assert.equal(chooser.isMultiple(), false)
  await chooser.setFiles(file)
  assert.deepEqual(
    await page.evaluate(
      `Promise.all(Array.from(document.querySelector('#root').files,async file=>({name:file.name,text:await file.text()})))`,
    ),
    [{ name: 'genuine-chooser.txt', text: 'Actual chooser file bytes' }],
  )
  const events = z
    .array(z.object({ type: z.string(), id: z.string(), trusted: z.boolean() }))
    .parse(await page.evaluate('fileEvents'))
  assert.deepEqual(events, [
    { type: 'input', id: 'root', trusted: true },
    { type: 'change', id: 'root', trusted: true },
  ])
  observations.push({ phase: 'healthy root chooser', events })
  checks.push(
    'A later healthy root chooser retains actual input identity, genuine File bytes and trusted native upload events',
  )

  const next = page.waitForEvent('filechooser', { timeout: 1200 })
  const [again] = await Promise.all([next, page.click('#root')])
  inputs.add(again.element())
  assert.equal(await again.element().getAttribute('id'), 'root')
  page.off('filechooser', listener)
  for (const input of inputs) {
    await input.dispose()
    inputs.delete(input)
  }
  const state = await settle()
  checks.push(
    'Repeated healthy chooser delivery remains usable and all selected handles/groups/physical commands settle before page close',
  )
  assert.equal(checks.length, 3)
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
console.info('Native file chooser lifetime3 passed')
