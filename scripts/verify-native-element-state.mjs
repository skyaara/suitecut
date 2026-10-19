// SPDX-License-Identifier: MIT
/* global document, HTMLElement, requestAnimationFrame, setTimeout */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  createNativePage,
  launchNativeBrowser,
  expectNative,
  NativeElementHandle,
} from '../dist/native.js'

const directory = resolve('.suitecut/native-element-state')
await mkdir(directory, { recursive: true })
await rm(directory + '/report.json', { force: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
const State = z.object({
  nativeElementStates: z.number(),
  nativeElementStatePhysical: z.number(),
  nativeElementStateReleases: z.number(),
  evaluationGroups: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  otherOperations: z.number(),
})
let stderr = ''
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic: (message) => {
    if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  await page.setContent(
    `<div id="child" style="display:contents"><span>Actual child</span></div><span id="text" style="display:contents">Actual text</span><span id="empty" style="display:contents"></span><span id="space" style="display:contents"> </span><span id="transparent" style="opacity:0">Actual transparent</span><details><summary>Summary</summary><span id="closed">Not rendered</span></details><div style="content-visibility:hidden"><span id="skipped">Not rendered</span></div><div id="shadow" style="display:contents">Slotted text</div>`,
  )
  await page.evaluate(
    () =>
      (document.querySelector('#shadow').attachShadow({ mode: 'open' }).innerHTML =
        '<slot style="display:contents"></slot>'),
  )
  for (const [selector, expected] of [
    ['#child', true],
    ['#text', true],
    ['#empty', false],
    ['#space', true],
    ['#transparent', true],
    ['#closed', false],
    ['#skipped', false],
    ['#shadow', true],
  ]) {
    assert.equal(await page.locator(selector).isVisible(), expected, selector)
    assert.equal(
      await page.locator(selector).filter({ visible: true }).count(),
      Number(expected),
      selector + ' filter',
    )
    await page.locator(selector).waitFor({ state: expected ? 'visible' : 'hidden', timeout: 1000 })
    await expectNative(page.locator(selector))[expected ? 'toBeVisible' : 'toBeHidden']({
      timeout: 1000,
    })
  }
  checks.push(
    'eight real composed visibility cases agree across immediate lookup, selector filtering, native locator waits and native matcher',
  )
  await page.setContent(
    '<input id="input" disabled readonly><div id="edit" contenteditable>Editable</div><div id="plain">Plain</div><button id="replace">Original</button>',
  )
  const handle = await page.locator('#input').elementHandle()
  assert(handle)
  await handle.waitForElementState('disabled', { timeout: 1000 })
  await page.evaluate(() =>
    setTimeout(() => (document.querySelector('input').disabled = false), 30),
  )
  await handle.waitForElementState('enabled', { timeout: 1000 })
  await page.evaluate(() =>
    setTimeout(() => (document.querySelector('input').readOnly = false), 30),
  )
  await handle.waitForElementState('editable', { timeout: 1000 })
  const edit = await page.locator('#edit').elementHandle()
  assert(edit)
  await edit.waitForElementState('editable', { timeout: 1000 })
  const plain = await page.locator('#plain').elementHandle()
  assert(plain)
  await assert.rejects(plain.waitForElementState('editable', { timeout: 1000 }), /not an input/)
  checks.push(
    'exact native handle waits observe actual disabled, enabled, readonly and contenteditable transitions and reject unsupported editable targets',
  )
  await page.evaluate(() => (document.querySelector('input').style.visibility = 'hidden'))
  await handle.waitForElementState('hidden', { timeout: 1000 })
  await page.evaluate(() =>
    setTimeout(() => (document.querySelector('input').style.visibility = 'visible'), 30),
  )
  await handle.waitForElementState('visible', { timeout: 1000 })
  const original = await page.locator('#replace').elementHandle()
  assert(original)
  await page.evaluate(() => {
    const e = document.querySelector('#replace')
    e.remove()
    document.body.insertAdjacentHTML('beforeend', '<button id="replace">New</button>')
  })
  await original.waitForElementState('hidden', { timeout: 1000 })
  await assert.rejects(original.waitForElementState('visible', { timeout: 1000 }), /not attached/)
  assert.equal(await page.locator('#replace').isVisible(), true)
  checks.push(
    'fixed handles keep original node identity across replacement and detached hidden succeeds while detached visible fails',
  )
  await page.setContent(
    '<div id="moving" style="width:40px;height:20px">Moving</div><div id="contents" style="display:contents"><span>Child</span></div>',
  )
  const moving = await page.locator('#moving').elementHandle()
  assert(moving)
  await page.evaluate(() => {
    const e = document.querySelector('#moving')
    if (!(e instanceof HTMLElement)) throw new Error('Actual moving node missing')
    let n = 0
    const loop = () => {
      e.style.transform = `translateX(${++n}px)`
      if (n < 12) requestAnimationFrame(loop)
      else Reflect.set(globalThis, 'movementFinished', true)
    }
    requestAnimationFrame(loop)
  })
  await moving.waitForElementState('stable', { timeout: 2000 })
  assert.equal(
    await page.evaluate(() => Reflect.get(globalThis, 'movementFinished') === true),
    true,
  )
  const contents = await page.locator('#contents').elementHandle()
  assert(contents)
  await contents.waitForElementState('stable', { timeout: 2000 })
  checks.push(
    'stability requires consecutive genuine renderer RAF geometry samples, including composed display:contents geometry',
  )
  await page.evaluate(() => {
    const e = document.querySelector('#moving')
    if (!(e instanceof HTMLElement)) throw new Error('Actual moving node missing')
    let n = 0
    Reflect.set(globalThis, 'stateMoving', true)
    const loop = () => {
      if (!Reflect.get(globalThis, 'stateMoving')) return
      e.style.transform = `translateX(${++n}px)`
      requestAnimationFrame(loop)
    }
    requestAnimationFrame(loop)
  })
  await assert.rejects(moving.waitForElementState('stable', { timeout: 80 }), /Timeout|timed out/)
  await page.evaluate(() => Reflect.set(globalThis, 'stateMoving', false))
  await moving.waitForElementState('stable', { timeout: 2000 })
  checks.push(
    'native stable timeout cleans sampler ownership and subsequent real stable wait recovers',
  )
  await page.evaluate(() => {
    const e = document.querySelector('#moving')
    if (!(e instanceof HTMLElement)) throw new Error('Actual moving node missing')
    let n = 0
    Reflect.set(globalThis, 'stateMoving', true)
    const loop = () => {
      if (!Reflect.get(globalThis, 'stateMoving')) return
      e.style.transform = `translateX(${++n}px)`
      requestAnimationFrame(loop)
    }
    requestAnimationFrame(loop)
  })
  const saturated = await Promise.allSettled(
    Array.from({ length: 64 }, () => moving.waitForElementState('stable', { timeout: 1500 })),
  )
  assert.equal(saturated.length, 64)
  for (const entry of saturated) {
    assert.equal(entry.status, 'rejected')
    assert.match(String(entry.reason), /Timeout|timed out/)
  }
  await page.evaluate(() => Reflect.set(globalThis, 'stateMoving', false))
  await moving.waitForElementState('stable', { timeout: 2000 })
  checks.push(
    '64 concurrent real moving-node waits all time out and release admission before a fresh genuine stable wait succeeds',
  )
  await page.evaluate(() => {
    const e = document.querySelector('#moving')
    if (!(e instanceof HTMLElement)) throw new Error('Actual moving node missing')
    let n = 0
    Reflect.set(globalThis, 'stateMoving', true)
    const loop = () => {
      if (!Reflect.get(globalThis, 'stateMoving')) return
      e.style.transform = `translateX(${++n}px)`
      requestAnimationFrame(loop)
    }
    requestAnimationFrame(loop)
  })
  const abort = new AbortController()
  const projected = createNativePage(source, abort.signal)
  const projection = new NativeElementHandle(projected, moving.id, '<div>')
  const canceled = projection.waitForElementState('stable', { timeout: 0 })
  const rejected = assert.rejects(canceled, /actual wait cancellation/)
  await new Promise((r) => setTimeout(r, 40))
  abort.abort(new Error('actual wait cancellation'))
  await rejected
  await page.evaluate(() => Reflect.set(globalThis, 'stateMoving', false))
  await moving.waitForElementState('stable', { timeout: 2000 })
  checks.push(
    'real AbortSignal cancellation cleans native sampler without poisoning canonical retained node or subsequent wait',
  )
  for (const h of [handle, edit, plain, original, moving, contents]) await h.dispose()
  await page.setContent(
    '<iframe srcdoc="<div id=frame-target style=width:20px;height:20px>Child</div>"></iframe>',
  )
  const child = await page
    .frameLocator('iframe')
    .locator('#frame-target')
    .elementHandle({ timeout: 2000 })
  assert(child)
  await child.waitForElementState('stable', { timeout: 2000 })
  const frame = page.frames().find((f) => f.parentFrame())
  assert(frame)
  await frame.evaluate(() => {
    const e = document.querySelector('div')
    if (!(e instanceof HTMLElement)) throw new Error('Actual child node missing')
    let n = 0
    const loop = () => {
      e.style.transform = `translateX(${++n}px)`
      requestAnimationFrame(loop)
    }
    requestAnimationFrame(loop)
  })
  const detached = child.waitForElementState('stable', { timeout: 2000 })
  const detachRejected = assert.rejects(detached, /detached|context|closed|unavailable/i)
  await new Promise((r) => setTimeout(r, 40))
  await page.evaluate(() => document.querySelector('iframe').remove())
  await detachRejected
  checks.push(
    'exact child-frame stable wait owns real utility context and releases native work when that actual frame is detached',
  )
  let state = State.parse(await source.runAutomation('diagnostics'))
  for (let i = 0; i < 100; i++) {
    state = State.parse(await source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) break
    await new Promise((r) => setTimeout(r, 10))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  const report = {
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks,
    state,
  }
  await writeFile(directory + '/report.json', JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
