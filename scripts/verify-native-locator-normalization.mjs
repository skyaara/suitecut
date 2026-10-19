// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-locator-normalization'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end('<button id="actual-child">Actual child</button>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address === 'object')
const base = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
    executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
    headless: true,
    onDiagnostic: diagnostics.observe,
  }),
  page = createNativePage(source)
/** @type {unknown[]} */
const errors = []
/** @type {string[]} */
const checks = []
/** @type {Record<string,unknown>[]} */
const observations = []
const ownership = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
  bindingCalls: z.number(),
  pendingBindingCommands: z.number(),
})
try {
  await page.setContent('<button data-testid="real-normalized">Actual target</button>')
  const before = await page.locator('body').innerHTML(),
    normalized = await page.locator('button').normalize()
  const pins = normalized.normalization()
  assert(
    pins &&
      pins.backendNodeId > 0 &&
      pins.frame === page.mainFrame().identity() &&
      pins.mainIdentity &&
      pins.utilityIdentity,
  )
  assert(Object.isFrozen(pins))
  assert.equal(pins.kind, 'testId')
  assert.equal(
    await normalized.evaluate((element) => element === document.querySelector('button')),
    true,
  )
  assert.equal(await page.locator('body').innerHTML(), before)
  assert.equal(normalized.describe('User label').normalization(), pins)
  assert.equal(normalized.first().normalization(), null)
  observations.push({ initial: pins })
  checks.push(
    'genuine native creation pins accompany a same-Element builtin locator without author DOM mutation or diagnostic-string parsing',
  )
  await page.evaluate(() => {
    document
      .querySelector('button')
      ?.addEventListener('click', (event) =>
        Reflect.set(globalThis, 'normalizationTrusted', event.isTrusted),
      )
  })
  await normalized.click()
  assert.equal(await page.evaluate('globalThis.normalizationTrusted'), true)
  checks.push('the returned builtin locator enters actual trusted native platform input')
  await page.setContent('<button id="duplicate">One</button><button id="duplicate">Two</button>')
  const duplicate = await page.locator('button').nth(1).normalize()
  assert.equal(duplicate.normalization()?.kind, 'role')
  assert.equal(
    await duplicate.evaluate((element) => element === document.querySelectorAll('button')[1]),
    true,
  )
  const oldBackend = duplicate.normalization()?.backendNodeId
  await page
    .locator('button')
    .nth(1)
    .evaluate((element) => {
      element.replaceWith(element.cloneNode(true))
    })
  assert.equal(
    await duplicate.evaluate((element) => element === document.querySelectorAll('button')[1]),
    true,
  )
  assert.notEqual((await duplicate.normalize()).normalization()?.backendNodeId, oldBackend)
  checks.push(
    'nonunique ID candidates are rejected and the actual role locator resolves a replacement while creation provenance remains historical',
  )
  await page.setContent('<div></div><div></div><svg><circle></circle></svg>')
  await page.locator('div').evaluateAll((elements) =>
    elements.forEach((element) => {
      element.attachShadow({ mode: 'open' }).innerHTML = '<button></button><button></button>'
    }),
  )
  const shadow = await page.locator('button').nth(3).normalize()
  assert.equal(shadow.normalization()?.kind, 'path')
  assert.equal(
    await shadow.evaluate(
      (element) =>
        element === document.querySelectorAll('div')[1]?.shadowRoot?.querySelectorAll('button')[1],
    ),
    true,
  )
  assert.equal(
    await (
      await page.locator('circle').normalize()
    ).evaluate((element) => element === document.querySelector('circle')),
    true,
  )
  checks.push(
    'genuine shadow and SVG Elements receive builtin ASTs validated against their original identities',
  )
  await page.setContent('<button></button>')
  for (const value of ['Actual\ud800\0\\"\udfff', '{"__suitecutNativeUTF16Scalar":"d800"}']) {
    await page
      .locator('button')
      .evaluate((element, text) => element.setAttribute('data-testid', text), value)
    const exact = await page.locator('button').normalize()
    assert.equal(await exact.getAttribute('data-testid'), value)
    assert.equal(
      await exact.evaluate((element) => element === document.querySelector('button')),
      true,
    )
  }
  checks.push(
    'isolated UTF16/NUL and tag-shaped author values survive builtin AST transport as exact ordinary strings',
  )
  await page.goto(base)
  await page.evaluate((url) => {
    const iframe = document.createElement('iframe')
    iframe.id = 'actual-frame'
    iframe.src = url
    document.body.appendChild(iframe)
  }, `http://localhost:${address.port}/child`)
  await page.frameLocator('#actual-frame').getByText('Actual child').waitFor({ timeout: 2000 })
  const child = page
    .frames()
    .find((frame) => frame.url() === `http://localhost:${address.port}/child`)
  assert(child)
  const childLocator = await page
    .frameLocator('#actual-frame')
    .getByText('Actual child')
    .normalize()
  assert.equal(childLocator.normalization()?.frame, child.identity())
  assert.equal(
    await childLocator.evaluate((element) => element === document.querySelector('#actual-child')),
    true,
  )
  assert.equal(childLocator.page(), page)
  checks.push('actual OOPIF normalization retains genuine native frame and owner Page identity')
  const started = new AbortController(),
    began = new Promise((resolve) =>
      started.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
  const binding = await page.exposeFunction('normalizationHoldStarted', () => started.abort())
  const held = page.evaluate(
    '(()=>{globalThis.normalizationHoldStarted();const until=Date.now()+300;while(Date.now()<until){};})()',
  )
  await began
  await assert.rejects(page.locator('button').normalize({ timeout: 10 }), /timed out/u)
  const during = ownership.parse(await source.runAutomation('diagnostics'))
  assert(during.nativeSelectorWaitPhysical > 0)
  await held
  await binding.dispose()
  observations.push({ rendererHeld: during })
  checks.push(
    'original deadlines reject a real renderer-held normalization while physical work remains owned until the actual ACK',
  )
  await page.setContent('<button>One</button><button>Two</button>')
  const markup = await page.locator('body').innerHTML()
  await assert.rejects(page.locator('button').normalize(), /Strict|strict/u)
  await assert.rejects(
    source.runAutomation('normalizeLocator', {
      query: [{ engine: 'css', selector: 'button' }],
      nativeQueryNodes: 'invented',
    }),
    /reserved/u,
  )
  assert.equal(await page.locator('body').innerHTML(), markup)
  checks.push('strict ambiguity and forged private inputs reject without changing actual nodes')
  await page.goto('about:blank')
  const deadline = performance.now() + 1000
  let state = ownership.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(1)
    state = ownership.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  checks.push('every tracked operation/group/physical resource retires after genuine ACK')
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ completed: true, checks, observations, state }, null, 2) + '\n',
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
  await new Promise((resolve) => server.close(() => resolve(undefined)))
}
if (errors.length) throw errors[0]
