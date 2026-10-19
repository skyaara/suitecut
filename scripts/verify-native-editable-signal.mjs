// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeTimeoutError } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-editable-signal',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 420,
  height: 320,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(2000)
/** @type {unknown[]} */
const behaviorErrors = []
/** @type {string[]} */
const checks = []
const ownership = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
  inputWork: z.number(),
  pressedKeys: z.number(),
})
/**
 * Observe real physical command retirement under a bounded proof deadline.
 * @param {(value:z.output<typeof ownership>)=>boolean} predicate
 */
async function waitState(predicate) {
  const until = performance.now() + 3000
  while (true) {
    const value = ownership.parse(await source.runAutomation('diagnostics'))
    if (predicate(value)) return value
    assert(performance.now() < until, 'Native editable cancellation ownership did not settle')
  }
}
const setup = async () => {
  await page.setContent('<input id=actual value=ORIGINAL><input id=outside value=OUTSIDE>')
  await page.locator('#outside').focus()
  await page.evaluate(
    "globalThis.actualEditEvents=[];for(const type of ['input','change','keydown','keyup'])document.addEventListener(type,e=>globalThis.actualEditEvents.push({type:e.type,id:e.target.id,trusted:e.isTrusted}),true)",
  )
}
const state = async () =>
  z
    .object({ actual: z.string(), outside: z.string(), events: z.array(z.json()) })
    .parse(
      await page.evaluate(
        "({actual:document.querySelector('#actual').value,outside:document.querySelector('#outside').value,events:globalThis.actualEditEvents})",
      ),
    )
try {
  await setup()
  const handle = await page.$('#actual')
  assert(handle)
  const controller = new AbortController(),
    reason = new Error('fill: original pre-aborted editable operation')
  controller.abort(reason)
  const options = { signal: controller.signal, timeout: 0 }
  await assert.rejects(page.fill('#actual', 'NEW', options), (error) => error === reason)
  await assert.rejects(page.locator('#actual').fill('NEW', options), (error) => error === reason)
  await assert.rejects(page.locator('#actual').clear(options), (error) => error === reason)
  await assert.rejects(handle.fill('NEW', options), (error) => error === reason)
  await assert.rejects(handle.clear(options), (error) => error === reason)
  await handle.dispose()
  assert.deepEqual(await state(), { actual: 'ORIGINAL', outside: 'OUTSIDE', events: [] })
  checks.push(
    'Page/locator/fixed-handle fill and clear preserve the exact original pre-aborted reason without native editing or synthetic events',
  )
  page.setDefaultTimeout(70)
  const live = new AbortController()
  await assert.rejects(
    page.fill('#missing', 'NEW', { signal: live.signal }),
    (error) => error instanceof NativeTimeoutError,
  )
  await assert.rejects(
    page.locator('#missing').clear({ signal: live.signal }),
    (error) => error instanceof NativeTimeoutError,
  )
  page.setDefaultTimeout(10)
  const zero = new AbortController(),
    zeroReason = new Error('Original explicit-zero editable cancellation')
  let settled = false
  const pendingZero = page
    .locator('#missing')
    .fill('NEW', { signal: zero.signal, timeout: 0 })
    .finally(() => {
      settled = true
    })
  const rejectedZero = assert.rejects(pendingZero, (error) => error === zeroReason),
    began = performance.now()
  await source.runAutomation('evaluate', {
    expression:
      '(async()=>{for(let index=0;index<4;index++)await new Promise(requestAnimationFrame);return 42})()',
    timeoutMs: 2000,
  })
  assert(performance.now() - began > 10)
  assert.equal(settled, false)
  zero.abort(zeroReason)
  await rejectedZero
  page.setDefaultTimeout(2000)
  await waitState((value) => Object.values(value).every((count) => count === 0))
  checks.push(
    'Caller signals do not disable native inherited deadlines; explicit timeout0 survives actual rendering opportunities and remains cancelable by its original reason',
  )
  await setup()
  /**
   * Receive the actual renderer start notification.
   * @type {()=>void}
   */
  let mark = () => {
    throw Error('Renderer hold callback missing')
  }
  const started = new Promise((resolve) => {
    mark = () => resolve(undefined)
  })
  const bridge = await page.exposeFunction('actualEditableHoldStarted', () => mark())
  const busy = page.evaluate(
    '(()=>{globalThis.actualEditableHoldStarted();const until=performance.now()+650;while(performance.now()<until){};return 42})()',
  )
  await started
  const active = new AbortController(),
    activeReason = new Error('fill: exact abort while actual preparation ACK is pending')
  const pending = page.fill('#actual', 'NEW', { signal: active.signal, timeout: 0 }),
    rejected = assert.rejects(pending, (error) => error === activeReason)
  const during = await waitState((value) => value.nativeSelectorWaitPhysical > 0)
  active.abort(activeReason)
  await rejected
  const afterAbort = ownership.parse(await source.runAutomation('diagnostics'))
  assert(afterAbort.nativeSelectorWaits > 0 && afterAbort.nativeSelectorWaitPhysical > 0)
  assert.equal(await busy, 42)
  await bridge.dispose()
  assert.deepEqual(await state(), { actual: 'ORIGINAL', outside: 'OUTSIDE', events: [] })
  const finalOwnership = await waitState((value) =>
    Object.values(value).every((count) => count === 0),
  )
  checks.push(
    'Public fill cancellation returns its exact reason while C++ retains real preparation/group ACKs; after renderer release no late text is dispatched and all ownership settles',
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: true, checks, during, afterAbort, finalOwnership }, null, 2) + '\n',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, checks, error: String(error) }, null, 2) + '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, diagnostics, directory, behaviorErrors)
}
