// SPDX-License-Identifier: MIT
/* global console, document, setTimeout */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-locator-wait-strict'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
    executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
    headless: true,
    onDiagnostic: diagnostics.observe,
  }),
  page = createNativePage(source)
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const observations = []
/** @type {unknown[]} */
const errors = []
/** @type {Promise<unknown> | undefined} */
let busy
const ownership = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
})
try {
  await page.setContent('<span id="first">First</span><span id="second">Second</span>')
  for (const state of ['attached', 'detached', 'visible', 'hidden']) {
    await assert.rejects(
      page.locator('span').waitFor({
        state: z.enum(['attached', 'detached', 'visible', 'hidden']).parse(state),
        timeout: 1000,
      }),
      (error) => {
        assert(error instanceof Error)
        assert.match(error.message, /strict mode violation/u)
        assert.match(error.message, /id="first"/u)
        assert.match(error.message, /id="second"/u)
        return true
      },
    )
  }
  checks.push(
    'every wait state reports genuine native matched nodes and identity-validated strict diagnostics',
  )

  await page.setContent('<button id="first">First</button><button id="second">Second</button>')
  const first = await page.waitForSelector('button', { state: 'attached', timeout: 1000 })
  assert(first)
  assert.equal(await first.getAttribute('id'), 'first')
  await first.dispose()
  await assert.rejects(
    page.waitForSelector('button', { strict: true, timeout: 1000 }),
    /strict mode violation/u,
  )
  checks.push(
    'Page first-match and explicit strict selector waits preserve their native atomic selection contracts',
  )

  await page.setContent(
    '<button id="outside">Outside</button><iframe id="child" srcdoc="<button id=childone>One</button><button id=childtwo>Two</button>"></iframe>',
  )
  await page.frameLocator('#child').locator('body').waitFor({ state: 'attached' })
  await assert.rejects(
    page
      .frameLocator('#child')
      .getByRole('button')
      .describe('Unrelated label')
      .waitFor({ timeout: 1000 }),
    (error) => {
      assert(error instanceof Error)
      assert.match(error.message, /strict mode violation/u)
      assert.match(error.message, /getByRole/u)
      assert.match(error.message, /id="childone"/u)
      assert.match(error.message, /id="childtwo"/u)
      assert.equal(error.message.includes('Outside'), false)
      assert.equal(error.message.includes('Unrelated label'), false)
      return true
    },
  )
  checks.push(
    'frame waits retain genuine child identity and native AST diagnostics rather than user description labels',
  )

  await page.setContent('<span id="target" hidden>Target</span>')
  await page.locator('#target').waitFor({ state: 'attached', timeout: 1000 })
  await page.evaluate(() => {
    setTimeout(() => document.querySelector('#target')?.removeAttribute('hidden'), 30)
  })
  await page.locator('#target').waitFor({ state: 'visible', timeout: 1000 })
  await page.evaluate(() => {
    setTimeout(() => document.querySelector('#target')?.setAttribute('hidden', ''), 30)
  })
  await page.locator('#target').waitFor({ state: 'hidden', timeout: 1000 })
  await page.evaluate(() => {
    setTimeout(() => document.querySelector('#target')?.remove(), 30)
  })
  await page.locator('#target').waitFor({ state: 'detached', timeout: 1000 })
  checks.push(
    'all four wait-state observations continue to use native retries over actual DOM transitions',
  )

  await page.setContent('<button>Actual held target</button>')
  let startedResolve = () => undefined
  const started = new Promise((resolve) => {
    startedResolve = () => resolve(undefined)
  })
  const binding = await page.exposeFunction('actualStrictWaitStarted', () => startedResolve())
  busy = page.evaluate(
    '(()=>{globalThis.actualStrictWaitStarted();const until=performance.now()+500;while(performance.now()<until){};})()',
  )
  await started
  const abort = new AbortController(),
    cause = new Error('Actual strict wait cancellation')
  const pending = createNativePage(source, abort.signal)
      .locator('button')
      .waitFor({ timeout: 5000 }),
    rejection = assert.rejects(pending, (error) => error === cause)
  const until = performance.now() + 1500
  let before = ownership.parse(await source.runAutomation('diagnostics'))
  while (!before.nativeSelectorWaitPhysical && performance.now() < until) {
    await delay(2)
    before = ownership.parse(await source.runAutomation('diagnostics'))
  }
  assert(before.nativeSelectorWaitPhysical)
  abort.abort(cause)
  await rejection
  const after = ownership.parse(await source.runAutomation('diagnostics'))
  assert(after.nativeSelectorWaitPhysical > 0)
  await busy
  busy = undefined
  await binding.dispose()
  observations.push({ before, after })
  await page.goto('about:blank')
  const deadline = performance.now() + 3000
  let state = ownership.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(2)
    state = ownership.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  checks.push(
    'caller cancellation rejects promptly while actual renderer-held selection and cleanup remain owned until real receipts',
  )
  assert.equal(checks.length, 5)
  await writeFile(
    `${directory}/report.json`,
    JSON.stringify({ completed: true, checks, observations, state }, null, 2),
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    `${directory}/failure.json`,
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
    ),
  )
} finally {
  if (busy) {
    const result = await Promise.allSettled([busy])
    for (const item of result) if (item.status === 'rejected') errors.push(item.reason)
  }
  await finishNativeProof(source, diagnostics, directory, errors)
}
if (errors.length) throw errors[0]
console.log(`Native strict locator wait ${checks.length} groups PASS`)
