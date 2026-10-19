// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve, join } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_SELECT_TEXT_DIRECTORY ?? '.suitecut/native-select-text-proof',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory),
  errors = z.array(z.unknown()).parse([]),
  checks = []
const source = await launchNativeBrowser({
  executablePath: z.string().min(1).parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 640,
  height: 480,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
const rendered = () =>
  page.evaluate(
    () =>
      new Promise((resolve) =>
        globalThis.requestAnimationFrame(() =>
          globalThis.requestAnimationFrame(() => resolve(null)),
        ),
      ),
  )
try {
  await page.setContent(
    '<!DOCTYPE html><input id=input value="Actual α🙂" readonly><textarea id=textarea>Typed β</textarea><div id=plain contenteditable>Plain <b>node</b> text</div><label id=label for=input><span id=labelchild>Label</span></label><input id=hidden style="display:none" value="Hidden selected value"><div class=duplicate>A</div><div class=duplicate>B</div>',
  )
  await page.evaluate(() => {
    /** @type {{type:string,trusted:boolean}[]} */
    const journal = []
    globalThis.selectionEvents = journal
    for (const type of ['select', 'input', 'change'])
      globalThis.document.addEventListener(
        type,
        (event) => journal.push({ type: event.type, trusted: event.isTrusted }),
        true,
      )
    Reflect.deleteProperty(globalThis, 'Node')
  })
  for (const [selector, text] of [
    ['#input', 'Actual α🙂'],
    ['#textarea', 'Typed β'],
    ['#plain', 'Plain node text'],
  ]) {
    await page.locator(z.string().parse(selector)).selectText({ timeout: 2000 })
    assert.equal(await page.evaluate('getSelection().toString()'), text)
  }
  checks.push('native-control-and-Range-selection-Unicode-readonly-with-author-Node-deleted')
  await page.locator('#labelchild').selectText({ timeout: 1000 })
  assert.equal(await page.evaluate('document.activeElement.id'), 'input')
  assert.equal(await page.evaluate('getSelection().toString()'), 'Actual α🙂')
  checks.push('genuine-label-control-association')
  const handle = await page.$('#textarea')
  assert(handle)
  try {
    await handle.evaluate((node) => {
      if (!(node instanceof globalThis.HTMLElement))
        throw new Error('Actual textarea fixture is not HTML')
      node.id = 'renamed'
      const decoy = globalThis.document.createElement('textarea')
      decoy.id = 'textarea'
      decoy.value = 'Decoy'
      node.after(decoy)
    })
    await handle.selectText({ timeout: 1000 })
    assert.equal(await page.evaluate('getSelection().toString()'), 'Typed β')
    await handle.evaluate((node) => {
      if (!(node instanceof globalThis.Element))
        throw new Error('Actual node fixture is not an element')
      node.remove()
    })
    await assert.rejects(
      handle.selectText({ timeout: 1000 }),
      /Element is not attached to the DOM/u,
    )
  } finally {
    await handle.dispose()
  }
  checks.push('pinned-node-rename-and-detachment-do-not-select-replacement')
  let completed = false
  const waiting = page
    .locator('#hidden')
    .selectText({ timeout: 5000 })
    .then(() => {
      completed = true
    })
  await rendered()
  assert.equal(completed, false)
  await page.locator('#hidden').evaluate((node) => (node.style.display = 'block'))
  await waiting
  assert.equal(await page.evaluate('getSelection().toString()'), 'Hidden selected value')
  await page.locator('#hidden').evaluate((node) => (node.style.display = 'none'))
  page.setDefaultTimeout(200)
  await assert.rejects(page.locator('#hidden').selectText(), /element is not visible/u)
  page.setDefaultTimeout(5000)
  await page.locator('#hidden').evaluate((node) => {
    if (!(node instanceof globalThis.HTMLInputElement))
      throw new Error('Actual force fixture is not an input')
    node.setSelectionRange(3, 4)
  })
  await page.locator('#hidden').selectText({ force: true, timeout: 1000 })
  assert.deepEqual(
    await page.locator('#hidden').evaluate((node) => {
      if (!(node instanceof globalThis.HTMLInputElement))
        throw new Error('Actual force fixture is not an input')
      return { start: node.selectionStart, end: node.selectionEnd, length: node.value.length }
    }),
    { start: 0, end: 21, length: 21 },
  )
  checks.push('original-visibility-wait-default-timeout-and-forced-real-input-selection-range')
  await assert.rejects(page.locator('.duplicate').selectText({ timeout: 1000 }), /[Ss]trict/u)
  const controller = new AbortController(),
    reason = new Error('native-selection-caller-cancel')
  const canceled = page.locator('#hidden').selectText({ timeout: 0, signal: controller.signal })
  await rendered()
  await rendered()
  const during = z
    .object({ evaluationGroups: z.number(), activeHandles: z.number() })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(during.evaluationGroups, 1)
  assert.equal(during.activeHandles, 0)
  controller.abort(reason)
  await assert.rejects(canceled, (error) => {
    assert(error instanceof Error)
    assert.match(error.message, /native-selection-caller-cancel/u)
    return true
  })
  checks.push('strict-selection-and-caller-cancel-with-one-bounded-command-group')
  const ready = await page.waitForFunction(
    'selectionEvents.some(event=>event.type==="select"&&event.trusted)',
    undefined,
    { timeout: 2000 },
  )
  await ready.dispose()
  const events = z
    .array(z.object({ type: z.string(), trusted: z.boolean() }))
    .parse(await page.evaluate('selectionEvents'))
  assert(events.some((event) => event.type === 'select' && event.trusted))
  assert(events.every((event) => event.type !== 'input' && event.type !== 'change'))
  checks.push('actual-trusted-select-events-without-synthetic-input-change')
  await page.setContent(
    '<!DOCTYPE html><iframe id=child srcdoc="<!DOCTYPE html><textarea id=target>Actual frame selection</textarea>"></iframe>',
  )
  await page.frameLocator('#child').locator('#target').selectText({ timeout: 5000 })
  const iframe = await page.$('#child')
  assert(iframe)
  const frame = await iframe.contentFrame()
  await iframe.dispose()
  assert(frame)
  const target = await frame.$('#target')
  assert(target)
  try {
    await target.selectText({ timeout: 1000 })
    assert.equal(await frame.evaluate('getSelection().toString()'), 'Actual frame selection')
  } finally {
    await target.dispose()
  }
  checks.push('lazy-and-pinned-child-frame-real-document-selection')
  const stats = z
    .object({
      activeHandles: z.number(),
      pendingHandles: z.number(),
      evaluationGroups: z.number(),
      inputWork: z.number(),
      queuedInput: z.number(),
      pressedKeys: z.number(),
      keyModifiers: z.number(),
      mouseButtons: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(stats)) assert.equal(value, 0, `Actual final ${key}`)
  await writeFile(
    join(directory, 'report.json'),
    JSON.stringify({ completed: true, checks, events, stats }, null, 2) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    join(directory, 'failure.json'),
    JSON.stringify(
      { completed: false, checks, error: error instanceof Error ? error.stack : String(error) },
      null,
      2,
    ) + '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, diagnostics, directory, errors)
}
