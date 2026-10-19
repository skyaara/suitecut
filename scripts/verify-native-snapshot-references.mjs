// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-snapshot-references'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end(
    request.url === '/child'
      ? '<div id="container"><button id="child">Child actual</button></div>'
      : request.url === '/next'
        ? '<button>Next actual</button>'
        : '<div id="container"><button id="main">Main actual</button></div>',
  )
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
  nativeSnapshotReferences: z.number(),
  nativeSnapshotReferenceResidents: z.number(),
  nativeSnapshotReferenceGroups: z.number(),
  nativeSnapshotReferenceBytes: z.number(),
  nativeSnapshotReferenceInstalls: z.number(),
  nativeSnapshotReferenceReleases: z.number(),
  nativeSnapshotReferenceBorrows: z.number(),
  nativeSnapshotReferenceDispatches: z.number(),
  nativeSnapshotReferenceProcessDispatches: z.number(),
  nativeSnapshotReferenceProcessInstalls: z.number(),
  nativeSnapshotReferenceProcessReleases: z.number(),
  nativeSnapshotReferenceProcessResidents: z.number(),
})
/**
 * Extracts only genuinely returned opaque tokens, without expected IDs.
 * @param {string} yaml
 */
function tokens(yaml) {
  return [...yaml.matchAll(/\[ref=(r[A-Za-z0-9]{1,64}_[1-9][0-9]*_(?:0|[1-9][0-9]*))\]/gu)].map(
    (match) => z.string().parse(match[1]),
  )
}
/**
 * Finds a token on an actual named node.
 * @param {string} yaml
 * @param {string} name
 */
function named(yaml, name) {
  const line = yaml.split('\n').find((candidate) => candidate.includes(JSON.stringify(name)))
  assert(line, `Actual snapshot name ${name} missing`)
  const found = tokens(line)
  assert.equal(found.length, 1)
  return z.string().parse(found[0])
}
/** Polls genuine native receipts only, within the checker cleanup budget. */
async function retired() {
  const deadline = performance.now() + 1000
  let state = ownership.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(1)
    state = ownership.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  return state
}
try {
  await page.setContent(
    '<h1>Actual heading</h1><section id="scope"><button id="alpha">Alpha actual</button></section><section id="outside"><button id="beta">Beta actual</button></section><div id="empty"></div>',
  )
  const markup = await page.locator('body').innerHTML(),
    plain = await page.ariaSnapshot(),
    yaml = await page.ariaSnapshot({ mode: 'ai' }),
    alpha = named(yaml, 'Alpha actual'),
    beta = named(yaml, 'Beta actual')
  assert(tokens(yaml).length >= 3)
  assert(!plain.includes('[ref='))
  assert.equal(await page.ariaSnapshot(), plain)
  assert.equal(await page.locator('body').innerHTML(), markup)
  checks.push(
    'AI refs are genuine native metadata; default snapshots and actual author DOM attributes/markup remain unchanged',
  )
  assert.equal(
    await page.getByRef(alpha).evaluate((element) => element === document.querySelector('#alpha')),
    true,
  )
  assert.equal(await page.getByRef(beta).textContent(), 'Beta actual')
  checks.push(
    'opaque tokens resolve the exact actual backend Elements through authored callbacks and native utility getters',
  )
  await page.evaluate(() => {
    document.querySelector('#alpha')?.addEventListener('click', (event) => {
      Reflect.set(globalThis, 'actualRefTrusted', event.isTrusted)
    })
  })
  await page.getByRef(alpha).click()
  assert.equal(await page.evaluate('globalThis.actualRefTrusted'), true)
  checks.push(
    'reference clicks enter the genuine native action pipeline and dispatch trusted platform input',
  )
  assert.equal(await page.locator('#scope').getByRef(alpha).count(), 1)
  assert.equal(await page.locator('#outside').getByRef(alpha).count(), 0)
  assert.equal(
    await page
      .getByRef(alpha)
      .and(page.getByRole('button', { name: 'Alpha actual' }))
      .count(),
    1,
  )
  checks.push(
    'prefixed CSS and composed locator plans preserve actual scope and matched Element identity',
  )
  const firstState = ownership.parse(await source.runAutomation('diagnostics'))
  assert.equal(firstState.nativeSnapshotReferences, new Set(tokens(yaml)).size)
  assert.equal(firstState.nativeSnapshotReferenceGroups, 1)
  assert.equal(firstState.nativeSnapshotReferenceInstalls, 0)
  assert.equal(firstState.nativeSnapshotReferenceDispatches, 0)
  observations.push({ successful: firstState })
  checks.push(
    'successful operation completion releases temporary groups while the real persistent snapshot group remains owned and resolvable',
  )
  await page.evaluate(() => {
    const host = document.createElement('div')
    host.id = 'shadow'
    document.body.appendChild(host)
    host.attachShadow({ mode: 'open' }).innerHTML =
      '<button id="shadow-button">Shadow actual</button>'
    const owner = document.createElement('section')
    owner.id = 'owner'
    owner.setAttribute('aria-owns', 'owned')
    document.body.appendChild(owner)
    const child = document.createElement('button')
    child.id = 'owned'
    child.textContent = 'Owned actual'
    document.body.appendChild(child)
  })
  const composed = await page.ariaSnapshot({ mode: 'ai' }),
    shadow = named(composed, 'Shadow actual')
  assert.equal(
    await page
      .getByRef(shadow)
      .evaluate(
        (element) =>
          element === document.querySelector('#shadow')?.shadowRoot?.querySelector('button'),
      ),
    true,
  )
  const owned = named(await page.locator('#owner').ariaSnapshot({ mode: 'ai' }), 'Owned actual')
  assert.equal(
    await page.getByRef(owned).evaluate((element) => element === document.querySelector('#owned')),
    true,
  )
  checks.push(
    'shadow descendants and real aria-owned out-of-subtree nodes map to actual Elements without invented anonymous-node references',
  )
  await page.evaluate(() => {
    for (let i = 0; i < 650; i++) {
      const button = document.createElement('button')
      button.className = 'overflow'
      button.textContent = 'x'
      document.body.appendChild(button)
    }
  })
  await assert.rejects(page.ariaSnapshot({ mode: 'ai' }), /64 KiB|limit exceeded/u)
  assert.equal(await page.getByRef(owned).evaluate((element) => element.id), 'owned')
  await page
    .locator('.overflow')
    .evaluateAll((elements) => elements.forEach((element) => element.remove()))
  checks.push(
    'a genuine bounded capture/publication failure preserves the last successful snapshot refs and retires failed staging work',
  )
  const newer = await page.locator('#alpha').ariaSnapshot({ mode: 'ai' }),
    newAlpha = named(newer, 'Alpha actual')
  assert.notEqual(newAlpha, alpha)
  await assert.rejects(page.getByRef(owned).textContent({ timeout: 100 }), /unknown|stale/u)
  assert.equal(await page.getByRef(newAlpha).textContent(), 'Alpha actual')
  assert.equal(
    named(await page.locator('#alpha').ariaSnapshot({ mode: 'ai' }), 'Alpha actual'),
    newAlpha,
  )
  assert.equal(await page.getByRef(newAlpha).textContent(), 'Alpha actual')
  checks.push(
    'successful snapshots invalidate omitted nodes and preserve the exact same retained backend reference across repeated captures',
  )
  assert.equal(await page.locator('#empty').ariaSnapshot({ mode: 'ai' }), '')
  await assert.rejects(page.getByRef(newAlpha).textContent({ timeout: 100 }), /unknown|stale/u)
  checks.push(
    'a successful empty forest invalidates old same-frame refs and retires its genuine empty group',
  )
  await page.setContent('<button id="units"></button>')
  const units = 'Actual\ud800\0\\"\udfff'
  await page.locator('#units').evaluate((element, text) => {
    element.textContent = text
  }, units)
  const unitsRef = z.string().parse(tokens(await page.ariaSnapshot({ mode: 'ai' }))[0])
  assert.equal(await page.getByRef(unitsRef).textContent(), units)
  checks.push(
    'real isolated UTF16 units survive native precommit YAML and exact reference lookup without generic author-object tag decoding',
  )
  await page.goto(base)
  await page.locator('#main').waitFor()
  await page.evaluate((url) => {
    const iframe = document.createElement('iframe')
    iframe.id = 'actual-child'
    iframe.src = url
    document.body.appendChild(iframe)
  }, `http://localhost:${address.port}/child`)
  await page.frameLocator('#actual-child').getByText('Child actual').waitFor({ timeout: 2000 })
  const mainRef = named(await page.locator('#main').ariaSnapshot({ mode: 'ai' }), 'Main actual'),
    childRef = named(
      await page.frameLocator('#actual-child').locator('body').ariaSnapshot({ mode: 'ai' }),
      'Child actual',
    )
  assert.equal(await page.getByRef(childRef).evaluate((element) => element.id), 'child')
  assert.equal(
    await page
      .frameLocator('#actual-child')
      .getByRef(childRef)
      .evaluate((element) => element.id),
    'child',
  )
  await assert.rejects(
    page.locator('#container').getByRef(childRef).textContent({ timeout: 100 }),
    /different scoped frame/u,
  )
  checks.push(
    'direct references select their genuine child/OOPIF frame while CSS-prefixed main-frame locators cannot silently reroot into that child',
  )
  const actualChild = page
    .frames()
    .find((frame) => frame.url() === `http://localhost:${address.port}/child`)
  assert(actualChild)
  await actualChild.goto(`http://localhost:${address.port}/next`)
  await assert.rejects(
    page.getByRef(childRef).textContent({ timeout: 100 }),
    /unknown|stale|replaced/u,
  )
  assert.equal(await page.getByRef(mainRef).evaluate((element) => element.id), 'main')
  checks.push(
    'actual child navigation/session replacement invalidates child refs while unrelated parent snapshot ownership remains valid',
  )
  const started = new AbortController(),
    began = new Promise((resolve) =>
      started.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
  const binding = await page.exposeFunction('actualReferenceHoldStarted', () => started.abort())
  const held = page.evaluate(
    '(()=>{globalThis.actualReferenceHoldStarted();const until=Date.now()+300;while(Date.now()<until){};})()',
  )
  await began
  await assert.rejects(page.ariaSnapshot({ mode: 'ai', timeout: 10 }), /timed out/u)
  const during = ownership.parse(await source.runAutomation('diagnostics'))
  assert(during.nativeSelectorWaitPhysical > 0)
  await held
  await binding.dispose()
  assert.equal(await page.getByRef(mainRef).evaluate((element) => element.id), 'main')
  observations.push({ heldCapture: during })
  checks.push(
    'a genuine renderer-held capture times out logically, preserves previous refs, and retains temporary physical work until real ACK',
  )
  await page.goto('about:blank')
  const state = await retired()
  checks.push(
    'actual navigation releases every retained snapshot group, physical dispatch/install/borrow and ordinary selected-node resource after genuine ACK',
  )
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
