// SPDX-License-Identifier: MIT
/* global document, Element, window, setTimeout */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeSelectors } from '../dist/native.js'

const directory = '.suitecut/native-action-isolation'
await mkdir(directory, { recursive: true })
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (message) => {
    stderr += message
    assert(stderr.length < 1048576)
  },
})
const schema = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
})
const checks = []
try {
  const page = createNativePage(source)
  await page.setContent(
    '<button id="target" onclick="window.actualActionClicks=(window.actualActionClicks||0)+1;window.actualActionClickTrusted=event.isTrusted" onmouseover="window.actualActionHoverTrusted=event.isTrusted">Actual action</button>',
  )
  await page.evaluate(() => {
    const rectangle = Object.getOwnPropertyDescriptor(Element.prototype, 'getBoundingClientRect'),
      style = Object.getOwnPropertyDescriptor(window, 'getComputedStyle'),
      raf = Object.getOwnPropertyDescriptor(window, 'requestAnimationFrame')
    if (!rectangle || !style || !raf) throw new Error('Actual browser DOM descriptors unavailable')
    Reflect.set(globalThis, 'actualActionAuthorMarker', true)
    Reflect.set(globalThis, 'actualAuthorDOMCalls', 0)
    Reflect.set(globalThis, 'restoreActualActionDOM', () => {
      Object.defineProperty(Element.prototype, 'getBoundingClientRect', rectangle)
      Object.defineProperty(window, 'getComputedStyle', style)
      Object.defineProperty(window, 'requestAnimationFrame', raf)
    })
    const reject = () => {
      Reflect.set(
        globalThis,
        'actualAuthorDOMCalls',
        Number(Reflect.get(globalThis, 'actualAuthorDOMCalls')) + 1,
      )
      throw new Error('Author DOM geometry must not implement native readiness')
    }
    Element.prototype.getBoundingClientRect = reject
    window.getComputedStyle = reject
    window.requestAnimationFrame = reject
  })
  const selectors = new NativeSelectors(source)
  await selectors.register('actionActualMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'actualActionAuthorMarker') !== true)
        throw new Error('Factory did not execute in actual main world')
      return root.querySelectorAll(selector)
    },
  }))
  await selectors.register(
    'actionActualUtility',
    () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (Reflect.get(globalThis, 'actualActionAuthorMarker') !== undefined)
          throw new Error('Factory did not execute in actual utility world')
        return root.querySelectorAll(selector)
      },
    }),
    { contentScript: true },
  )
  await page.locator('actionActualMain=#target').click({ timeout: 2000 })
  assert.equal(await page.evaluate(() => Number(Reflect.get(globalThis, 'actualActionClicks'))), 1)
  assert.equal(
    await page.evaluate(() => Reflect.get(globalThis, 'actualActionClickTrusted') === true),
    true,
  )
  checks.push(
    'single main-world factory retains its actual author environment while native geometry and real trusted click use the actual adopted utility wrapper',
  )
  await page.mouse.move(300, 160)
  await page.evaluate(() => Reflect.set(globalThis, 'actualActionHoverTrusted', false))
  await page.locator('actionActualMain=#target').hover({ timeout: 2000 })
  assert.equal(
    await page.evaluate(() => Reflect.get(globalThis, 'actualActionHoverTrusted') === true),
    true,
  )
  checks.push(
    'main-world custom-selector hover uses genuine utility readiness and trusted native pointer events',
  )
  await page.locator('#target').click({ timeout: 2000 })
  await page.locator('actionActualUtility=#target').click({ timeout: 2000 })
  assert.equal(await page.evaluate(() => Number(Reflect.get(globalThis, 'actualActionClicks'))), 3)
  checks.push(
    'builtin and declared utility selectors preserve their genuine native action paths under author geometry replacement',
  )
  await page
    .locator('actionActualMain=body >> actionActualUtility=#target')
    .click({ timeout: 2000 })
  assert.equal(await page.evaluate(() => Number(Reflect.get(globalThis, 'actualActionClicks'))), 4)
  assert.equal(
    await page.evaluate(() => Number(Reflect.get(globalThis, 'actualAuthorDOMCalls'))),
    0,
  )
  checks.push(
    'mixed-world factory composition preserves both declared worlds and author DOM geometry is never used by native readiness',
  )
  await page.evaluate(() =>
    setTimeout(() => {
      const button = document.createElement('button')
      button.id = 'later'
      button.textContent = 'Actually later'
      button.onclick = (event) => Reflect.set(globalThis, 'actualLaterTrusted', event.isTrusted)
      document.body.appendChild(button)
    }, 80),
  )
  const waiting = page.locator('actionActualMain=#later').click({ timeout: 2000 })
  await waiting
  assert.equal(
    await page.evaluate(() => Reflect.get(globalThis, 'actualLaterTrusted') === true),
    true,
  )
  checks.push(
    'missing custom selections retry through native group retirement and deliver real trusted input only after a genuine descendant appears',
  )
  await assert.rejects(
    page.locator('actionActualMain=#never').click({ timeout: 45 }),
    /Timeout|timed out/,
  )
  assert.equal(await page.evaluate(() => Number(Reflect.get(globalThis, 'actualActionClicks'))), 4)
  checks.push(
    'original native action deadline rejects missing matches without dispatching input or entering author geometry',
  )
  const abort = new AbortController(),
    projection = createNativePage(source, abort.signal)
  const started = new AbortController(),
    start = new Promise((resolve) =>
      started.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
  const bridge = await page.exposeFunction('actionIsolationHoldStarted', () => started.abort())
  const busy = page.evaluate(
    '(()=>{globalThis.actionIsolationHoldStarted();const end=Date.now()+500;while(Date.now()<end){};})()',
  )
  await start
  const pending = assert.rejects(
    projection.locator('actionActualMain=#never').click({ timeout: 3000 }),
    /Actual action cancellation/,
  )
  let held
  for (let attempt = 0; attempt < 100; attempt++) {
    held = schema.parse(await source.runAutomation('diagnostics'))
    if (held.nativeSelectorWaitPhysical > 0) break
    await delay(1)
  }
  assert(held && held.nativeSelectorWaitPhysical > 0)
  abort.abort(new Error('Actual action cancellation'))
  await pending
  const afterAbort = schema.parse(await source.runAutomation('diagnostics'))
  assert(afterAbort.nativeSelectorWaits > 0)
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  await busy
  await bridge.dispose()
  checks.push(
    'logical caller cancellation retains actual selected-node command ownership until renderer-held ACKs settle',
  )
  await page.evaluate('globalThis.restoreActualActionDOM()')
  for (let attempt = 0; attempt < 300; attempt++) {
    const state = schema.parse(await source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) break
    await delay(5)
  }
  const state = schema.parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(state)) assert.equal(value, 0, name)
  checks.push(
    'real action successes, retries, deadlines and cancellation retire all tracked native preparation groups and physical requests',
  )
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ checks, held, afterAbort, state }, null, 2) + '\n',
  )
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '', 'Native action diagnostics must remain empty')
