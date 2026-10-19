// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeSelectors } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-frame-visibility-snapshot'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory),
  source = await launchNativeBrowser({
    executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
    headless: true,
    onDiagnostic: diagnostics.observe,
  }),
  page = createNativePage(source),
  checks = [],
  rows = []
const schema = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeDOMGetters: z.number(),
  nativeVisibilitySnapshots: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  bindingCalls: z.number(),
  pendingBindingCommands: z.number(),
})
/** Waits only for actual native retirement within the bounded checker budget. */
async function settled() {
  const end = performance.now() + 1000
  let state = schema.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < end) {
    await delay(1)
    state = schema.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  return state
}
const errors = []
try {
  await page.setContent(
    '<div id="wrong"></div><iframe id="parent" srcdoc="<button>Actual button</button><button hidden>Hidden button</button>"></iframe>',
  )
  await page
    .frameLocator('#parent')
    .getByText('Actual button')
    .waitFor({ state: 'attached', timeout: 1000 })
  const began = performance.now()
  assert.equal(
    await page.frameLocator('#absent').locator('button').isVisible({ timeout: 1000 }),
    false,
  )
  assert.equal(
    await page.frameLocator('#absent').locator('button').isHidden({ timeout: 1000 }),
    true,
  )
  const elapsed = performance.now() - began
  assert(elapsed < 800)
  rows.push({ missingFrameMs: elapsed })
  checks.push(
    'actual missing iframe scopes return immediate false visibility and true hidden state without waiting for a future frame',
  )
  assert.equal(
    await page
      .frameLocator('#parent')
      .frameLocator('#absent')
      .locator('button')
      .isVisible({ timeout: 1000 }),
    false,
  )
  checks.push(
    'nested lazy frame paths stop at the actual absent child frame rather than retrying attachment',
  )
  const parent = page.frameLocator('#parent')
  assert.equal(await parent.getByText('Actual button').isVisible(), true)
  assert.equal(await parent.getByText('Hidden button').isHidden(), true)
  assert.equal(await parent.locator('#absent').isVisible(), false)
  checks.push(
    'existing real child documents still use native visibility for visible, hidden and absent matching nodes',
  )
  for (const operation of [
    () => page.frameLocator('#wrong').locator('button').isVisible(),
    () => parent.locator('button').isVisible(),
  ])
    await assert.rejects(operation(), /frame locator resolution failed|strict mode violation/u)
  await assert.rejects(
    page.frameLocator('##').locator('button').isVisible(),
    /Unexpected token "#" while parsing css selector "##"/u,
  )
  checks.push(
    'noniframe targets, malformed selectors and genuine child ambiguity stay errors instead of becoming empty-scope false',
  )
  const absent = page.frameLocator('#absent')
  for (const locator of [
    absent.locator('##'),
    absent.locator('section:has(##)'),
    absent.locator('xpath=//['),
    absent.locator('unknownActualVisibilityEngine=x'),
    absent.locator('section').and(absent.locator('##')),
  ])
    await assert.rejects(
      locator.isVisible({ timeout: 1000 }),
      /Unexpected token|valid XPath|Unknown engine|Invalid/u,
    )
  checks.push(
    'all leaf and nested grammar is compiled before an absent frame can produce empty-scope success',
  )
  await new NativeSelectors(source).register('actualAbsentLeafMustNotRun', () => {
    throw new Error('Actual leaf factory executed in an unrelated document')
  })
  assert.equal(await absent.locator('actualAbsentLeafMustNotRun=not CSS [').isVisible(), false)
  checks.push(
    'grammar-only preflight accepts opaque custom selector bodies without invoking their registered factory or callback in the wrong document',
  )

  const actual = page.frames().find((frame) => frame.parentFrame())
  assert(actual)
  await page.locator('#parent').evaluate((node) => node.remove())
  assert.equal(await actual.isVisible('button'), false)
  assert.equal(await actual.isHidden('button'), true)
  assert.equal(await parent.locator('button').isVisible(), false)
  checks.push(
    'a genuinely detached retained Frame facade and its now-missing lazy frame path both observe an empty current scope',
  )
  await new NativeSelectors(source).register('actualVisibilityThrow', () => ({
    query() {
      throw new Error('Actual visibility selector failure')
    },
    queryAll() {
      throw new Error('Actual visibility selector failure')
    },
  }))
  await assert.rejects(
    page.locator('actualVisibilityThrow=button').isVisible(),
    /Actual visibility selector failure/u,
  )
  checks.push(
    'actual declared main-world selector exceptions retain their real errors rather than being hidden by visibility snapshot mode',
  )
  const query = [{ engine: 'css', selector: 'button' }],
    mode = {
      query,
      dom: true,
      domGetter: true,
      visibility: true,
      immediate: true,
      visibilitySnapshot: true,
      function: 'function(){return true;}',
    }
  for (const fields of [
    { visibilitySnapshot: 'bad' },
    { domGetter: false },
    { dom: false },
    { visibility: false },
    { immediate: false },
    { all: true },
    { count: true },
    { expectation: true },
    { elementState: true },
    { stableState: true },
    { accessibilitySnapshot: true },
    { handles: [] },
  ])
    await assert.rejects(
      page.runAutomation('queryEvaluate', { ...mode, ...fields }),
      /Invalid native visibility snapshot request/u,
    )
  checks.push(
    'the native snapshot contract rejects malformed flags and incompatible authored/all-match/assertion metadata combinations before scope admission',
  )
  const empty = z
    .object({ result: z.object({ type: z.literal('boolean'), value: z.boolean() }) })
    .parse(await page.runAutomation('queryEvaluate', mode))
  assert.equal(empty.result.value, false)
  checks.push(
    'the dedicated native boolean mode does not execute an arbitrary caller function in a missing or replacement document',
  )
  const abort = new AbortController(),
    projection = createNativePage(page.source, abort.signal),
    started = new AbortController(),
    ready = new Promise((resolve) =>
      started.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    ),
    bridge = await page.exposeFunction('actualVisibilitySnapshotHoldStarted', () =>
      started.abort(),
    ),
    busy = page.evaluate(
      '(()=>{globalThis.actualVisibilitySnapshotHoldStarted();const end=Date.now()+500;while(Date.now()<end){};})()',
    )
  await ready
  const pending = assert.rejects(
    projection.frameLocator('#absent').locator('button').isVisible({ timeout: 1000 }),
    /Actual visibility snapshot cancellation/u,
  )
  let held
  for (let attempt = 0; attempt < 100; attempt++) {
    held = schema.parse(await source.runAutomation('diagnostics'))
    if (held.nativeSelectorWaitPhysical > 0) break
    await delay(1)
  }
  assert(held?.nativeVisibilitySnapshots === 1 && held.nativeSelectorWaitPhysical > 0)
  abort.abort(new Error('Actual visibility snapshot cancellation'))
  await pending
  const afterAbort = schema.parse(await source.runAutomation('diagnostics'))
  assert.equal(afterAbort.nativeVisibilitySnapshots, 0)
  assert.equal(afterAbort.nativeDOMGetters, 0)
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  const deadlineBegan = performance.now()
  await assert.rejects(
    page.frameLocator('#absent').locator('button').isVisible({ timeout: 35 }),
    /Timeout 35ms exceeded/u,
  )
  const deadlineMs = performance.now() - deadlineBegan
  assert(deadlineMs < 400)
  const afterDeadline = schema.parse(await source.runAutomation('diagnostics'))
  assert.equal(afterDeadline.nativeVisibilitySnapshots, 0)
  assert(afterDeadline.nativeSelectorWaitPhysical > 0)
  rows.push({ held, afterAbort, afterDeadline, deadlineMs })
  await busy
  await bridge.dispose()
  checks.push(
    'logical cancellation retires snapshot scope while actual renderer commands and private groups remain physically owned until real acknowledgment',
  )
  checks.push(
    'the original native deadline rejects held missing-frame observation before late renderer acknowledgment can publish empty-scope success',
  )
  const state = await settled()
  checks.push(
    'all actual snapshot, DOM getter, selector command/group, retained handle and binding ownership retires to zero',
  )
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ completed: true, checks, rows, state }, null, 2) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    directory + '/failure.json',
    JSON.stringify(
      {
        completed: false,
        checks,
        rows,
        error:
          error instanceof Error
            ? { name: error.name, message: error.message, stack: error.stack }
            : String(error),
      },
      null,
      2,
    ) + '\n',
  )
} finally {
  await finishNativeProof(source, diagnostics, directory, errors)
}
if (errors.length) throw errors[0]
