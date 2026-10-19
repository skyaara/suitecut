// SPDX-License-Identifier: MIT
/* global HTMLInputElement */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import {
  createNativePage,
  expectNative,
  launchNativeBrowser,
  NativeSelectors,
} from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-query-diagnostics'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory),
  source = await launchNativeBrowser({
    executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
    headless: true,
    onDiagnostic: diagnostics.observe,
  }),
  page = createNativePage(source)
/** @type {unknown[]} */
const errors = []
/** @type {string[]} */
const checks = []
/** @type {string[]} */
const observations = []
/**
 * Captures an actual native failure, preserving every diagnostic line.
 * @param {() => Promise<unknown>} operation
 */
async function failure(operation) {
  let message = ''
  await assert.rejects(operation(), (error) => {
    assert(error instanceof Error)
    message = error.message
    return message.includes('strict mode violation')
  })
  observations.push(message)
  return message
}
const ownership = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeDOMGetters: z.number(),
  nativeVisibilitySnapshots: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
  bindingCalls: z.number(),
  pendingBindingCommands: z.number(),
})
/** Waits for genuine acknowledgments within the bounded checker cleanup budget. */
async function retired() {
  let state = ownership.parse(await source.runAutomation('diagnostics'))
  const deadline = performance.now() + 1000
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(1)
    state = ownership.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  return state
}
try {
  await page.setContent('<span>span1</span><span>target</span>')
  const scalar = await failure(() => page.textContent('span', { strict: true }))
  assert(scalar.includes("1) <span>span1</span> aka getByText('span1')"))
  assert(scalar.includes("2) <span>target</span> aka getByText('target')"))
  assert.equal(await page.textContent('span'), 'span1')
  checks.push(
    'native scalar strict getters report genuine matched-node previews/unique text aliases, while Page default first-match remains native',
  )
  await page.setContent('<input><div><input></div>')
  const fill = await failure(() => page.fill('input', 'WRONG', { strict: true }))
  assert(fill.includes("1) <input/> aka getByRole('textbox').first()"))
  assert(fill.includes("2) <input/> aka locator('div').getByRole('textbox')"))
  assert.deepEqual(
    await page.locator('input').evaluateAll((elements) =>
      elements.map((element) => {
        if (!(element instanceof HTMLInputElement)) throw new Error('Actual native input missing')
        return element.value
      }),
    ),
    ['', ''],
  )
  checks.push(
    'action diagnostics preserve real original input identities and role aliases before native editing or retargeting',
  )
  await page.setContent('<span></span><div><span></span></div>')
  const event = await failure(() => page.dispatchEvent('span', 'click', {}, { strict: true }))
  assert(event.includes("1) <span></span> aka locator('span').first()"))
  assert(event.includes("2) <span></span> aka locator('div span')"))
  checks.push(
    'dispatch strict diagnostics preserve multiline native metadata separately from the retained selected-Node result',
  )
  await page.setContent(
    '<span>special test description</span><q:template><span>special test description</span></q:template>',
  )
  const matcher = await failure(() =>
    expectNative(page.getByText('special test description')).toBeVisible(),
  )
  assert(matcher.includes("getByText('special test description').first()"))
  assert(
    matcher.includes(
      "locator('q\\\\:template').filter({ hasText: 'special test description' }).getByText('special test description')",
    ),
  )
  checks.push(
    'scalar assertion strictness consumes the common native utility diagnostic without inventing a parent-node alias',
  )
  await page.setContent('<div>..loading</div><div>..loading</div>')
  const described = await failure(() =>
    page.locator('text=..loading').describe("getByRole('forged')").hover(),
  )
  assert(described.includes("locator('text=..loading')"))
  assert(!described.includes("getByRole('forged')"))
  checks.push(
    'original raw text-engine construction is data and user descriptions never become executable-looking fallback aliases',
  )
  const selectors = new NativeSelectors(source)
  await page.evaluate('globalThis.actualStrictEngineCalls=0')
  await selectors.register(
    'actualStrictMain',
    '()=>({query(root,selector){return root.querySelector(selector)},queryAll(root,selector){globalThis.actualStrictEngineCalls++;return root.querySelectorAll(selector)}})',
  )
  await failure(() => page.locator('actualStrictMain=div').textContent())
  assert.equal(await page.evaluate('globalThis.actualStrictEngineCalls'), 1)
  checks.push(
    'main-world custom selection executes once and its actual backend nodes are diagnosed in utility without reentering registered callbacks',
  )
  await page.setContent('<span></span><span></span>')
  await page.evaluate(
    "document.querySelectorAll('span').forEach(node=>node.textContent='Actual\\ud800')",
  )
  const units = await failure(() => page.locator('span').textContent())
  assert(units.includes('Actual\\ud800'))
  assert(!units.includes('\ud800') && !units.includes('\ufffd'))
  checks.push(
    'real isolated UTF16 DOM units remain explicit diagnostic escapes without substitution or ambiguous raw-scalar metadata',
  )
  await page.setContent('<span>one</span><span>two</span>')
  const hugeSelector = 'span/*' + 'x'.repeat(20000) + '*/'
  const unavailable = await failure(() => page.locator(hugeSelector).textContent())
  assert(unavailable.includes('strict mode violation: native locator resolved to 2 elements'))
  assert(!unavailable.includes('native locator.first()') && !unavailable.includes('x'.repeat(1000)))
  assert(unavailable.includes("getByText('one')") && unavailable.includes("getByText('two')"))
  checks.push(
    'oversized original expressions keep the actual selector and genuine aliases while omitting neutral-placeholder executable fallbacks',
  )
  await page.setContent('<span>actual</span>'.repeat(2000))
  const began = performance.now()
  const bounded = await failure(() => page.locator('span').textContent({ timeout: 1000 }))
  assert(bounded.length < 20000)
  assert(bounded.includes('resolved to 2000 elements'))
  assert(bounded.includes('\n    …'))
  assert.equal(bounded.split('\n').filter((line) => /^ {4}\d\)/u.test(line)).length, 5)
  const state = await retired()
  checks.push(
    'large actual match sets retain bounded preview/query/output work and retire every tracked native command/group/handle after real acknowledgment',
  )
  await writeFile(
    directory + '/report.json',
    JSON.stringify(
      {
        completed: true,
        checks,
        observations,
        boundedMilliseconds: performance.now() - began,
        state,
      },
      null,
      2,
    ) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    directory + '/failure.json',
    JSON.stringify(
      {
        completed: false,
        checks,
        observations,
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
