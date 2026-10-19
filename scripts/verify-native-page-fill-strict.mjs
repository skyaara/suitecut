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
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-page-fill-strict',
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
const errors = []
/** @type {string[]} */
const checks = []
/** @type {import('../src/untrusted.js').UntrustedInput[]} */
const observations = []
const stateSchema = z.object({
  one: z.string(),
  two: z.string(),
  outside: z.string(),
  events: z.array(z.object({ type: z.string(), id: z.string(), trusted: z.boolean() })),
})
const state = async () =>
  stateSchema.parse(
    await page.evaluate(
      `({one:document.querySelector('#one').value,two:document.querySelector('#two').value,outside:document.querySelector('#outside').value,events:globalThis.edits})`,
    ),
  )
const setup = () =>
  page.setContent(
    `<label for=one>First</label><input id=one value=FIRST><label for=two>Second</label><input id=two value=SECOND><input id=outside value=OUTSIDE><script>globalThis.edits=[];for(const type of ['beforeinput','input','change'])document.addEventListener(type,e=>edits.push({type,id:e.target.id,trusted:e.isTrusted}),true);document.querySelector('#outside').focus()</script>`,
  )
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
async function settledOwnership() {
  const deadline = performance.now() + 4000
  while (performance.now() < deadline) {
    const value = ownership.parse(await source.runAutomation('diagnostics', { timeoutMs: 1000 }))
    if (Object.values(value).every((count) => count === 0)) return value
  }
  throw new Error('Actual native editable ownership did not settle after its real acknowledgments')
}
try {
  for (const options of [{}, { strict: false }]) {
    await setup()
    await page.fill('input:not(#outside)', 'FIRST-NATIVE', options)
    const actual = await state()
    assert.equal(actual.one, 'FIRST-NATIVE')
    assert.equal(actual.two, 'SECOND')
    assert.equal(actual.outside, 'OUTSIDE')
    assert(
      actual.events.some((event) => event.type === 'input' && event.id === 'one' && event.trusted),
    )
    assert(actual.events.every((event) => event.id === 'one' && event.trusted))
    observations.push(actual)
  }
  await setup()
  await page.evaluate("document.querySelector('#one').hidden=true")
  await assert.rejects(
    page.fill('input:not(#outside)', 'WRONG', { timeout: 70 }),
    (error) => error instanceof NativeTimeoutError,
  )
  assert.deepEqual(await state(), { one: 'FIRST', two: 'SECOND', outside: 'OUTSIDE', events: [] })
  checks.push(
    'Page.fill default and explicitfalse edit only the genuine first matching control with trusted events; hidden first controls keep their native deadline instead of selecting a visible alternative',
  )

  await setup()
  assert.equal(await page.locator('input:not(#outside)').count(), 2)
  /** @type {string|undefined} */
  let nativeStrictError
  await assert.rejects(page.fill('input:not(#outside)', 'WRONG', { strict: true }), (error) => {
    assert(error instanceof Error)
    nativeStrictError = error.message
    return (
      /strict|multiple/iu.test(error.message) &&
      !error.message.includes('Unsupported native action option')
    )
  })
  assert.deepEqual(await state(), { one: 'FIRST', two: 'SECOND', outside: 'OUTSIDE', events: [] })
  observations.push({ actualNativeMatchCount: 2, nativeStrictError })
  checks.push(
    'Page.fill stricttrue reaches the genuine native count check and leaves both matched controls and outside focus target unedited',
  )

  await setup()
  await assert.rejects(page.locator('input:not(#outside)').fill('WRONG'), /strict|multiple/iu)
  await assert.rejects(page.locator('input:not(#outside)').clear(), /strict|multiple/iu)
  const second = await page.$('#two')
  assert(second)
  await second.fill('PINNED')
  await second.clear()
  await second.dispose()
  const pinned = await state()
  assert.equal(pinned.one, 'FIRST')
  assert.equal(pinned.two, '')
  assert.equal(pinned.outside, 'OUTSIDE')
  assert(
    pinned.events.length > 0 && pinned.events.every((event) => event.id === 'two' && event.trusted),
  )
  observations.push(pinned)
  checks.push(
    'Locator.fill/clear remain strict and actual fixed-handle fill/clear retain their selected second-node identity',
  )

  await setup()
  await page.fill('label', 'LABEL-NATIVE')
  const label = await state()
  assert.equal(label.one, 'LABEL-NATIVE')
  assert.equal(label.two, 'SECOND')
  assert(
    label.events.some((event) => event.id === 'one' && event.type === 'input' && event.trusted),
  )
  await page.setContent(
    '<input id=outside value=OUTSIDE><iframe srcdoc="<input id=one value=FIRST><input id=two value=SECOND>"></iframe>',
  )
  const frame = page.frameLocator('iframe')
  await frame.locator('#one').waitFor({ state: 'attached' })
  const actualFrame = page.frames().find((candidate) => candidate.parentFrame())
  assert(actualFrame)
  await actualFrame.fill('input', 'FRAME-FIRST')
  assert.equal(await frame.locator('#one').inputValue(), 'FRAME-FIRST')
  assert.equal(await frame.locator('#two').inputValue(), 'SECOND')
  assert.equal(await page.locator('#outside').inputValue(), 'OUTSIDE')
  await assert.rejects(actualFrame.fill('input', 'WRONG', { strict: true }), /strict|multiple/iu)
  checks.push(
    'Native label.control retargeting and inherited Frame.fill preserve the original native control/frame while using Page first-match semantics',
  )

  await setup()
  const controller = new AbortController(),
    reason = new Error('fill: original strict Page cancellation')
  controller.abort(reason)
  await assert.rejects(
    page.fill('input', 'WRONG', { strict: false, signal: controller.signal }),
    (error) => error === reason,
  )
  assert.deepEqual(await state(), { one: 'FIRST', two: 'SECOND', outside: 'OUTSIDE', events: [] })
  page.setDefaultTimeout(70)
  await assert.rejects(
    page.fill('#missing', 'WRONG', { strict: false }),
    (error) => error instanceof NativeTimeoutError,
  )
  const finalOwnership = await settledOwnership()
  checks.push(
    'Strict options do not serialize or replace caller cancellation and inherited native deadlines; every tracked native ownership counter settles tozero',
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: true, checks, observations, finalOwnership }, null, 2) + '\n',
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
