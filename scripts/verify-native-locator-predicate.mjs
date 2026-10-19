// SPDX-License-Identifier: MIT
/* global console, document, window */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-locator-predicate'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 480,
  height: 360,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
const stateSchema = z.object({
  nativeLocatorPredicates: z.number(),
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
})
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const observations = []
/** @type {unknown[]} */
const errors = []
async function retired() {
  const deadline = performance.now() + 2000
  let state = stateSchema.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(2)
    state = stateSchema.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  observations.push(state)
  return state
}
const server = createServer((_request, response) => {
  response.setHeader('content-type', 'text/html; charset=utf-8')
  response.end('<div id="target">actual-child</div>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
try {
  await page.setContent('<div id="target">yes</div>')
  assert.equal(
    await page.locator('#target').waitForFunction((element) => element.textContent === 'yes'),
    undefined,
  )
  await retired()
  checks.push(
    'an immediately truthy actual node predicate returns void without allocating a result handle',
  )
  await page.locator('#target').waitForFunction('element => element.textContent === "yes"')
  await page.locator('#target').waitForFunction(async (element) => {
    await Promise.resolve()
    return element.textContent === 'yes'
  })
  await retired()
  checks.push(
    'string and Promise predicates execute against the actual selected main-world element',
  )
  await page.setContent('<span>initial</span>')
  await page.evaluate(() => {
    let n = 0
    const tick = () => {
      const old = document.querySelector('#target')
      const next = document.createElement('div')
      next.id = 'target'
      next.textContent = String(++n)
      old?.remove()
      document.body.append(next)
      if (n < 3) window.setTimeout(tick, 10)
    }
    window.setTimeout(tick, 10)
  })
  await page
    .locator('#target')
    .waitForFunction((element) => element.textContent === '3', undefined, { timeout: 2000 })
  await retired()
  checks.push(
    'native polling waits for a missing target and reselects genuine replacement nodes on each false retry',
  )
  await page.setContent('<div id="target">yes</div><div id="argument">actual-arg</div>')
  const argument = await page.locator('#argument').elementHandle()
  assert(argument)
  await page
    .locator('#target')
    .waitForFunction(
      (element, arg) => element.id === 'target' && arg.textContent === 'actual-arg',
      argument,
    )
  await argument.dispose()
  await retired()
  checks.push(
    'actual retained DOM handle arguments retain their native identity through predicate adoption',
  )
  await page
    .locator('#target')
    .waitForFunction(
      (_element, arg) =>
        arg.text.charCodeAt(0) === 0xd800 && arg.tag.__suitecutNativeUTF16Scalar === 'd800',
      { text: '\ud800', tag: { __suitecutNativeUTF16Scalar: 'd800' } },
    )
  await retired()
  checks.push(
    'typed argument decoding preserves isolated UTF16 and ordinary author tag objects independently',
  )
  await assert.rejects(
    page.locator('#target').waitForFunction(() => {
      throw new Error(
        'Actual predicate error: execution context destroyed; Element is not attached to the DOM',
      )
    }),
    /Actual predicate error/u,
  )
  await assert.rejects(
    page.locator('#target').waitForFunction(async () => {
      await Promise.resolve()
      throw new Error('Actual predicate rejection \ud800')
    }),
    (error) => error instanceof Error && error.message.includes('\ud800'),
  )
  await retired()
  checks.push(
    'author throw and Promise rejection propagate once rather than triggering string-based native retries',
  )
  await page.setContent('<div class="duplicate"></div><div class="duplicate"></div>')
  await assert.rejects(
    page.locator('.duplicate').waitForFunction(() => true),
    /strict mode violation/u,
  )
  await retired()
  checks.push('strict selection rejects actual duplicate nodes before invoking the predicate')
  await page.setContent('<div id="target"></div>')
  const already = new AbortController()
  already.abort('actual already-aborted reason')
  await assert.rejects(
    page.locator('#target').waitForFunction(() => true, undefined, { signal: already.signal }),
    (error) =>
      error instanceof Error &&
      error.name === 'AbortError' &&
      error.cause === 'actual already-aborted reason',
  )
  await retired()
  checks.push('already-aborted signal retains AbortError and exact cause without native admission')
  await assert.rejects(
    page.locator('#absent').waitForFunction(() => true, undefined, { timeout: 30 }),
    /timed out|Timeout/iu,
  )
  await retired()
  checks.push('a missing target retains the original native operation deadline across retries')
  await page.evaluate(() => {
    Object.assign(window, { actualPredicateStarted: false })
  })
  const controller = new AbortController(),
    reason = new Error('actual held author predicate cancellation')
  const held = page
    .locator('#target')
    .waitForFunction(
      () =>
        new Promise((resolve) => {
          Object.assign(window, { actualPredicateStarted: true, actualPredicateRelease: resolve })
        }),
      undefined,
      { timeout: 0, signal: controller.signal },
    )
    .catch((/** @type {unknown} */ error) => error)
  const started = await page.waitForFunction(
    () => Reflect.get(window, 'actualPredicateStarted') === true,
  )
  await started.dispose()
  const beforeAbort = stateSchema.parse(await source.runAutomation('diagnostics'))
  observations.push({ beforeAbort })
  assert(beforeAbort.nativeLocatorPredicates > 0 && beforeAbort.nativeSelectorWaitPhysical > 0)
  const abortedAt = performance.now()
  controller.abort(reason)
  const aborted = await held
  assert(aborted instanceof Error && aborted.name === 'AbortError' && aborted.cause === reason)
  assert(performance.now() - abortedAt < 1000)
  const afterAbort = stateSchema.parse(await source.runAutomation('diagnostics'))
  observations.push({ afterAbort })
  assert.equal(afterAbort.nativeLocatorPredicates, 0)
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  await page.evaluate(() => {
    const release = /** @type {unknown} */ (Reflect.get(window, 'actualPredicateRelease'))
    if (typeof release !== 'function') throw new Error('Actual held promise release unavailable')
    Reflect.apply(release, undefined, [true])
  })
  await retired()
  checks.push(
    'caller abort rejects promptly while real async predicate ownership remains until actual Promise ACK',
  )
  await page.goto(`http://127.0.0.1:${address.port}/main`)
  await page.setContent(`<iframe id="child" src="http://localhost:${address.port}/child"></iframe>`)
  await page
    .frameLocator('#child')
    .locator('#target')
    .waitForFunction(
      (element) => element.ownerDocument === document && element.textContent === 'actual-child',
      undefined,
      { timeout: 2000 },
    )
  await retired()
  checks.push(
    'OOPIF lazy frame selection adopts the exact selected backend node into its own author main world',
  )
  await page.goto('about:blank')
  await page.setContent('<div id="target">yes</div>')
  await page.evaluate(() => {
    Object.assign(window, { actualPredicateCalls: 0 })
  })
  await page.locator('#target').waitForFunction((element) => {
    const calls = Number(Reflect.get(window, 'actualPredicateCalls'))
    Reflect.set(window, 'actualPredicateCalls', calls + 1)
    return element.textContent === 'yes'
  })
  assert.equal(await page.evaluate(() => Number(Reflect.get(window, 'actualPredicateCalls'))), 1)
  await retired()
  checks.push(
    'a truthy predicate runs once and native publication does not replay author side effects',
  )
  assert.equal(checks.length, 12)
  await writeFile(
    `${directory}/report.json`,
    JSON.stringify({ completed: true, checks, observations, finalState: await retired() }, null, 2),
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
        error: error instanceof Error ? error.message : String(error),
      },
      null,
      2,
    ),
  )
} finally {
  try {
    await finishNativeProof(source, diagnostics, directory, errors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
if (errors.length) throw errors[0]
console.log(`Native locator predicate ${checks.length} groups PASS`)
