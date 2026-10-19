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
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-edit-focus',
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
/** @type {unknown[]} */
const behaviorErrors = []
/** @type {string[]} */
const checks = []
const stateSchema = z.object({
  active: z.string(),
  actual: z.string(),
  outside: z.string(),
  events: z.array(z.object({ type: z.string(), id: z.string(), trusted: z.boolean() })),
})
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
 * Prepare actual native focus and real editing event observations.
 * @param {string} attribute
 * @param {boolean} redirect
 */
async function setup(attribute = '', redirect = false) {
  await page.setContent(
    `<input id=actual value=ORIGINAL ${attribute}><input id=outside value=OUTSIDE>`,
  )
  await page.locator('#outside').focus()
  await page.evaluate(
    "globalThis.editEvents=[];for(const type of ['beforeinput','input','change','keydown','keyup'])document.addEventListener(type,e=>globalThis.editEvents.push({type:e.type,id:e.target.id,trusted:e.isTrusted}),true);document.querySelector('#outside').setSelectionRange(0,7)",
  )
  if (redirect)
    await page.evaluate(
      "document.querySelector('#actual').addEventListener('focus',()=>document.querySelector('#outside').focus())",
    )
}
const state = async () =>
  stateSchema.parse(
    await page.evaluate(
      "({active:document.activeElement.id,actual:document.querySelector('#actual').value,outside:document.querySelector('#outside').value,events:globalThis.editEvents})",
    ),
  )
/**
 * Dispatch an actual native edit intent; no host editing/value fallback.
 * @param {string} action
 * @param {boolean} force
 * @param {number} timeoutMs
 * @param {AbortSignal} [signal]
 */
function edit(action, force, timeoutMs = 1000, signal) {
  return page.runAutomation(
    'locator',
    {
      selector: '#actual',
      query: [{ engine: 'css', selector: '#actual' }],
      action,
      value: action === 'fill' ? 'FORCED' : '',
      force,
      strict: true,
      timeoutMs,
    },
    signal,
  )
}
/**
 * Observe genuine native settlement under an authored proof deadline.
 * @param {(value:z.output<typeof ownership>)=>boolean} predicate
 */
async function waitState(predicate) {
  const until = performance.now() + 3000
  while (true) {
    const value = ownership.parse(await source.runAutomation('diagnostics'))
    if (predicate(value)) return value
    assert(performance.now() < until, 'Native edit ownership failed to settle')
  }
}
try {
  for (const action of ['fill', 'clear']) {
    await setup('disabled')
    await assert.rejects(edit(action, true), /Element cannot receive native text editing focus/u)
    assert.deepEqual(await state(), {
      active: 'outside',
      actual: 'ORIGINAL',
      outside: 'OUTSIDE',
      events: [],
    })
  }
  checks.push(
    'Forced disabled fill/clear never dispatch text packets to an unrelated focused control; actual target and outside values/events stay untouched',
  )
  for (const action of ['fill', 'clear'])
    for (const force of [false, true]) {
      await setup('', true)
      await assert.rejects(
        edit(action, force, 70),
        force
          ? /Element cannot receive native text editing focus/u
          : (error) => error instanceof NativeTimeoutError,
      )
      const value = await state()
      assert.equal(value.active, 'outside')
      assert.equal(value.actual, 'ORIGINAL')
      assert.equal(value.outside, 'OUTSIDE')
      assert.deepEqual(value.events, [])
    }
  checks.push(
    'Synchronous author focus redirection cannot send forced or ordinary fill/clear editing events to the redirected control; ordinary retries keep their native original deadline',
  )
  await setup()
  await page.locator('#actual').fill('NORMAL')
  assert.equal((await state()).actual, 'NORMAL')
  await page.locator('#actual').clear()
  const ordinary = await state()
  assert.equal(ordinary.actual, '')
  assert.equal(ordinary.outside, 'OUTSIDE')
  assert(
    ordinary.events.some(
      (event) => event.type === 'input' && event.id === 'actual' && event.trusted,
    ),
  )
  await page.setContent(
    '<div id=outer contenteditable><div id=nested>OLD</div></div><div id=host></div><iframe id=child srcdoc="<input id=actual value=CHILD>"></iframe>',
  )
  await page.locator('#nested').fill('NESTED')
  assert.equal(await page.locator('#nested').textContent(), 'NESTED')
  await page.evaluate(
    "document.querySelector('#host').attachShadow({mode:'open'}).innerHTML='<input id=shadow value=SHADOW>'",
  )
  await page.locator('#shadow').fill('ACTUAL')
  assert.equal(await page.locator('#shadow').inputValue(), 'ACTUAL')
  await page.locator('#shadow').clear()
  assert.equal(await page.locator('#shadow').inputValue(), '')
  const frame = page.frameLocator('#child')
  await frame.locator('#actual').waitFor({ state: 'attached' })
  await frame.locator('#actual').fill('FRAME')
  assert.equal(await frame.locator('#actual').inputValue(), 'FRAME')
  await frame.locator('#actual').clear()
  assert.equal(await frame.locator('#actual').inputValue(), '')
  checks.push(
    'Genuine trusted normal text editing, nested editing-host selection, ShadowRoot activeElement and frame focus remain functional',
  )
  for (const action of ['fill', 'clear']) {
    await setup('hidden')
    await assert.rejects(edit(action, true), /Element is not visible/u)
    assert.equal((await state()).outside, 'OUTSIDE')
    await setup('readonly')
    await assert.rejects(edit(action, true, 70), (error) => error instanceof NativeTimeoutError)
    assert.equal((await state()).outside, 'OUTSIDE')
  }
  checks.push(
    'Existing hidden/readonly force limitations stay explicit native errors/deadlines; this repair does not forge editing capability or mutate read-only state',
  )
  await waitState((value) => Object.values(value).every((count) => count === 0))
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
  const bridge = await page.exposeFunction('actualEditHoldStarted', () => mark())
  const busy = page.evaluate(
    '(()=>{globalThis.actualEditHoldStarted();const until=performance.now()+650;while(performance.now()<until){};return 42})()',
  )
  await started
  const controller = new AbortController(),
    reason = new Error('Original native edit cancellation while real preparation ACK is pending')
  const pending = edit('fill', true, 0, controller.signal),
    rejected = assert.rejects(pending, (error) => error === reason)
  const during = await waitState((value) => value.nativeSelectorWaitPhysical > 0)
  controller.abort(reason)
  await rejected
  const afterAbort = ownership.parse(await source.runAutomation('diagnostics'))
  assert(afterAbort.nativeSelectorWaits > 0 && afterAbort.nativeSelectorWaitPhysical > 0)
  assert.equal(await busy, 42)
  await bridge.dispose()
  const actual = await state()
  assert.equal(actual.actual, 'ORIGINAL')
  assert.equal(actual.outside, 'OUTSIDE')
  assert.deepEqual(actual.events, [])
  const finalOwnership = await waitState((value) =>
    Object.values(value).every((count) => count === 0),
  )
  checks.push(
    'Canceled native edits preserve original reason and real preparation/group ACK ownership, then settle all counters without late native text dispatch',
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      { completed: true, checks, ordinary, during, afterAbort, finalOwnership },
      null,
      2,
    ) + '\n',
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
