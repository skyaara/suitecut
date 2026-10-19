// SPDX-License-Identifier: MIT
/* global document, setTimeout, HTMLElement, Element, queueMicrotask */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeSelectors } from '../dist/native.js'

const directory = '.suitecut/native-element-selector-wait'
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
const checks = []
const schema = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  nativeHandleDisposals: z.number(),
  nativeHandleDisposalPhysical: z.number(),
})
try {
  const page = createNativePage(source)
  await page.setContent(
    '<section id="root"><span id="target" style="display:none">Actual hidden</span></section><b id="outside">Outside</b>',
  )
  const root = await page.$('#root')
  assert(root)
  const attached = await root.waitForSelector('#target', { state: 'attached', timeout: 1000 })
  assert(attached)
  assert.equal(await attached.isVisible(), false)
  assert.equal(await attached.evaluate((node) => node === document.querySelector('#target')), true)
  await attached.dispose()
  checks.push(
    'attached wait returns the actual hidden descendant backend node inside the retained root',
  )
  await page.evaluate(() =>
    setTimeout(() => {
      const node = document.querySelector('#target')
      if (!(node instanceof HTMLElement)) throw new Error('Actual target missing')
      node.style.display = 'block'
    }, 30),
  )
  const visible = await root.waitForSelector('#target', { timeout: 1500 })
  assert(visible)
  assert.equal(await visible.isVisible(), true)
  await visible.dispose()
  checks.push('default visible native wait observes the genuine same descendant become visible')
  await assert.rejects(
    root.waitForSelector('#outside', { timeout: 35, state: 'attached' }),
    /Timeout 35ms/,
  )
  assert.equal(await root.waitForSelector('#outside', { state: 'hidden', timeout: 1000 }), null)
  checks.push(
    'outside nodes never satisfy the actual retained subtree scope, including hidden observations',
  )
  await root.evaluate((node) => {
    node.innerHTML = '<i class="many" style="display:none">First</i><i class="many">Second</i>'
  })
  await assert.rejects(
    root.waitForSelector('.many', { state: 'attached', strict: true, timeout: 1000 }),
    /strict mode violation/,
  )
  await assert.rejects(root.waitForSelector('.many', { timeout: 35 }), /Timeout 35ms/)
  const first = await root.waitForSelector('.many', { state: 'attached', timeout: 1000 })
  assert(first)
  assert.equal(await first.textContent(), 'First')
  await first.dispose()
  checks.push(
    'strict matching rejects multiplicity and nonstrict matching preserves the genuine first match before visibility',
  )
  await root.evaluate((node) => {
    node.innerHTML = '<b id="hide">Real visible</b>'
  })
  await page.evaluate(() =>
    setTimeout(() => {
      const node = document.querySelector('#hide')
      if (!(node instanceof HTMLElement)) throw new Error('Actual descendant missing')
      node.style.display = 'none'
    }, 30),
  )
  assert.equal(await root.waitForSelector('#hide', { state: 'hidden', timeout: 1500 }), null)
  await page.evaluate(() => setTimeout(() => document.querySelector('#hide').remove(), 30))
  assert.equal(await root.waitForSelector('#hide', { state: 'detached', timeout: 1500 }), null)
  checks.push(
    'hidden and detached waits poll actual scoped metadata and return null without creating matching handles',
  )
  const selectors = new NativeSelectors(source)
  await selectors.register('scopeWaitRace', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      const node = root.querySelector(selector)
      if (node?.textContent === 'Original')
        queueMicrotask(() => {
          const copy = node.cloneNode(true)
          copy.textContent = 'Replacement'
          node.replaceWith(copy)
        })
      return node ? [node] : []
    },
  }))
  await root.evaluate((node) => {
    node.innerHTML = '<button id="race">Original</button>'
  })
  const original = await root.waitForSelector('scopeWaitRace=#race', {
    state: 'attached',
    timeout: 1500,
  })
  assert(original)
  assert.equal(await original.textContent(), 'Original')
  assert.equal(
    await original.evaluate((node) => node instanceof Element && node.isConnected),
    false,
  )
  assert.equal(await page.locator('#race').textContent(), 'Replacement')
  await original.dispose()
  checks.push(
    'atomic native selector decision returns the exact original node across a real queued replacement, without selector reacquisition',
  )
  await root.evaluate((node) => {
    if (!(node instanceof Element)) throw new Error('Actual retained root missing')
    const copy = node.cloneNode(true)
    node.replaceWith(copy)
  })
  const retained = await root.waitForSelector('#race', { state: 'attached', timeout: 1000 })
  assert(retained)
  assert.equal(
    await retained.evaluate((node) => node instanceof Element && node.isConnected),
    false,
  )
  await retained.dispose()
  await root.dispose()
  checks.push(
    'detached retained roots continue to search their actual original subtree rather than a document replacement',
  )
  await page.setContent(
    '<iframe srcdoc="<section id=root><b id=child>Child</b></section>"></iframe>',
  )
  await page.frameLocator('iframe').locator('#child').waitFor({ timeout: 1500 })
  const childFrame = page.frames().find((frame) => frame.parentFrame())
  assert(childFrame)
  const childRoot = await childFrame.$('#root')
  assert(childRoot)
  const child = await childRoot.waitForSelector('#child', { timeout: 1500 })
  assert(child)
  assert.equal(await child.evaluate((node) => node === document.querySelector('#child')), true)
  await child.dispose()
  const destroyed = assert.rejects(
    childRoot.waitForSelector('#never', { timeout: 2000 }),
    /detached|destroyed|no object with guid/i,
  )
  await page.evaluate(() => document.querySelector('iframe').remove())
  await destroyed
  await childRoot.dispose()
  checks.push(
    'child-frame returned handles use the actual frame identity and pending retained-root waits reject genuine frame destruction',
  )
  await page.setContent('<section id=root></section>')
  const abort = new AbortController(),
    projected = createNativePage(source, abort.signal)
  const projectedRoot = await projected.$('#root')
  assert(projectedRoot)
  const waiting = assert.rejects(
    projectedRoot.waitForSelector('#never', { timeout: 3000 }),
    /Actual scoped cancellation/,
  )
  await delay(25)
  abort.abort(new Error('Actual scoped cancellation'))
  await waiting
  await source.runAutomation('handle', { action: 'dispose', handle: projectedRoot.id })
  const live = await page.$('#root')
  assert(live)
  await assert.rejects(
    live.waitForSelector('#never', { state: 'detached', strict: true, timeout: -1 }),
  )
  await assert.rejects(live.waitForSelector('##', { timeout: 1000 }), /Unexpected token/)
  await live.dispose()
  checks.push(
    'logical cancellation, invalid options and real selector errors preserve failure semantics with actual cleanup through the live source',
  )
  for (let attempt = 0; attempt < 300; attempt++) {
    const state = schema.parse(await source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) break
    await delay(5)
  }
  const state = schema.parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(state)) assert.equal(value, 0, name)
  checks.push(
    'successful, detached, failed and canceled scoped waits retire all actual handles, preparation groups and physical ownership',
  )
  await writeFile(directory + '/report.json', JSON.stringify({ checks, state }, null, 2) + '\n')
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '', 'Native scoped wait diagnostics must remain empty')
