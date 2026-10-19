// SPDX-License-Identifier: MIT
/* global console, document, window, Element, HTMLTextAreaElement */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-aria-json'
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
const ownership = z.object({
  nativeAriaJSON: z.number(),
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
})
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const observations = []
/** @type {unknown[]} */
const errors = []
async function retired() {
  const deadline = performance.now() + 2000
  let state = ownership.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(2)
    state = ownership.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  return state
}
/**
 * Finds a genuine emitted semantic node without deriving its reference from text.
 * @param {import('../src/native-aria.js').NativeAriaJSONNode[]} nodes
 * @param {string} role
 */
function find(nodes, role) {
  const pending = [...nodes]
  while (pending.length) {
    const node = pending.pop()
    if (!node) break
    if (node.role === role) return node
    if (node.children)
      for (const child of node.children) if (typeof child !== 'string') pending.push(child)
  }
  return undefined
}
const server = createServer((_request, response) => {
  response.setHeader('content-type', 'text/html; charset=utf-8')
  response.end('<button style="cursor:pointer">Actual child</button>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
try {
  await page.setContent('<h1>Title</h1><ul aria-label="List"><li>one</li><li>two</li></ul>')
  assert.deepEqual(await page.ariaSnapshotJSON(), [
    { role: 'heading', name: 'Title', level: 1 },
    {
      role: 'list',
      name: 'List',
      children: [
        { role: 'listitem', text: 'one' },
        { role: 'listitem', text: 'two' },
      ],
    },
  ])
  await retired()
  checks.push(
    'native roles/names/sole text are projected directly as structured data without YAML interpretation',
  )
  await page.setContent(
    '<input type="checkbox" title="Check" checked><button disabled>Click</button>',
  )
  assert.deepEqual(await page.ariaSnapshotJSON(), [
    { role: 'checkbox', name: 'Check', checked: true },
    { role: 'button', name: 'Click', disabled: true },
  ])
  await retired()
  checks.push(
    'real control flags retain typed native state values rather than formatted suffix strings',
  )
  await page.setContent('<a href="/real">Link</a><input title="Input" value="hello">')
  assert.deepEqual(await page.ariaSnapshotJSON(), [
    { role: 'link', name: 'Link', url: '/real' },
    { role: 'textbox', name: 'Input', text: 'hello' },
  ])
  await retired()
  checks.push(
    'real link raw attributes and normalized textbox values retain their native property provenance',
  )
  await page.setContent('Before<p>Hello <a href="/real">world</a> again</p>After')
  assert.deepEqual(await page.ariaSnapshotJSON(), [
    { role: 'text', text: 'Before' },
    {
      role: 'paragraph',
      children: ['Hello', { role: 'link', name: 'world', url: '/real' }, 'again'],
    },
    { role: 'text', text: 'After' },
  ])
  await retired()
  checks.push('top-level text records and nested string fragments preserve actual native order')
  await page.setContent('<h1>Outside</h1><ul><li><button>One</button></li></ul>')
  assert.deepEqual(await page.locator('ul').ariaSnapshotJSON({ depth: 1 }), [
    { role: 'list', children: [{ role: 'listitem' }] },
  ])
  await retired()
  checks.push('locator scope and requested depth prune only the captured actual subtree')
  await page.setContent(
    '<button id="target" style="position:absolute;left:20px;top:30px;width:80px;height:40px;border:0;padding:0">Box</button>',
  )
  const remote = z.object({ result: z.object({ objectId: z.string() }) }).parse(
    await page.sendDevToolsCommand('Runtime.evaluate', {
      expression: 'document.querySelector("#target")',
      returnByValue: false,
      objectGroup: 'actual-aria-json-box-proof',
    }),
  )
  const quad = z
    .object({ model: z.object({ border: z.array(z.number()).length(8) }) })
    .parse(await page.sendDevToolsCommand('DOM.getBoxModel', { objectId: remote.result.objectId }))
    .model.border
  await page.sendDevToolsCommand('Runtime.releaseObjectGroup', {
    objectGroup: 'actual-aria-json-box-proof',
  })
  const xs = quad.filter((_v, i) => i % 2 === 0),
    ys = quad.filter((_v, i) => i % 2 === 1)
  const actualBox = {
    x: Math.min(...xs),
    y: Math.min(...ys),
    width: Math.max(...xs) - Math.min(...xs),
    height: Math.max(...ys) - Math.min(...ys),
  }
  assert.deepEqual(find(await page.ariaSnapshotJSON({ boxes: true }), 'button')?.box, actualBox)
  observations.push({ actualInspectorQuad: quad })
  await retired()
  checks.push(
    'structured boxes are genuine utility geometry agreeing with the actual Inspector border quad',
  )
  await page.evaluate(() => {
    Element.prototype.getBoundingClientRect = function () {
      throw new Error('Actual author DOMRect poison')
    }
    window.getComputedStyle = function () {
      throw new Error('Actual author CSS poison')
    }
  })
  assert.deepEqual(find(await page.ariaSnapshotJSON({ boxes: true }), 'button')?.box, actualBox)
  await assert.rejects(
    page.locator('#target').evaluate((element) => element.getBoundingClientRect()),
    /Actual author DOMRect poison/u,
  )
  await retired()
  checks.push(
    'native structured observation remains utility-isolated while authored callbacks preserve the genuine author poison',
  )
  await page.goto('about:blank')
  await page.setContent('<textarea title="Input"></textarea>')
  const actualText = '\ud800 {"__suitecutNativeUTF16Scalar":"d800"}'
  await page.locator('textarea').evaluate((element, text) => {
    if (!(element instanceof HTMLTextAreaElement)) throw new Error('Actual textarea missing')
    element.value = text
  }, actualText)
  assert.deepEqual(await page.ariaSnapshotJSON(), [
    { role: 'textbox', name: 'Input', text: actualText },
  ])
  await retired()
  checks.push(
    'selected JSON string encoding preserves exact isolated UTF16 and ordinary author tag-shaped text',
  )
  await page.goto('about:blank')
  await page.setContent('<button style="cursor:pointer">Actual ref</button>')
  const ai = await page.ariaSnapshotJSON({ mode: 'ai' }),
    button = find(ai, 'button'),
    scope = find(ai, 'generic')
  assert(button?.ref && scope)
  assert.equal(button.cursor, 'pointer')
  assert.equal(
    scope.active === true,
    await page.evaluate(() => document.activeElement === document.body),
  )
  assert.equal(await page.getByRef(button.ref).textContent(), 'Actual ref')
  observations.push({ ai })
  checks.push(
    'AI scope/focus/cursor and opaque references derive from actual native Elements and CSS, never expected token constants',
  )
  await page.ariaSnapshotJSON()
  assert.equal(await page.getByRef(button.ref).textContent(), 'Actual ref')
  const newer = find(await page.ariaSnapshotJSON({ mode: 'ai' }), 'button')
  assert(newer?.ref && newer.ref !== button.ref)
  await assert.rejects(page.getByRef(button.ref).textContent(), /stale|unknown/u)
  assert.equal(await page.getByRef(newer.ref).textContent(), 'Actual ref')
  await page.goto('about:blank')
  await retired()
  checks.push(
    'default JSON does not replace committed AI identity while a new successful AI generation invalidates actual prior references',
  )
  await page.setContent('<div class="duplicate"></div><div class="duplicate"></div>')
  await assert.rejects(page.locator('.duplicate').ariaSnapshotJSON(), /strict mode violation/u)
  await assert.rejects(
    page.locator('#absent').ariaSnapshotJSON({ timeout: 30 }),
    /timed out|Timeout/iu,
  )
  await retired()
  checks.push(
    'strict actual scope and missing-element original deadlines remain native errors with complete physical retirement',
  )
  await page.goto(`http://127.0.0.1:${address.port}/main`)
  await page.setContent(`<iframe id="child" src="http://localhost:${address.port}/child"></iframe>`)
  assert.deepEqual(
    await page.frameLocator('#child').locator('body').ariaSnapshotJSON({ timeout: 2000 }),
    [{ role: 'button', name: 'Actual child' }],
  )
  await retired()
  checks.push(
    'a lazy OOPIF scope captures its genuine utility DOM and native semantic ownership without main-frame reacquisition',
  )
  assert.equal(checks.length, 12)
  await writeFile(
    `${directory}/report.json`,
    JSON.stringify({ completed: true, checks, observations, state: await retired() }, null, 2),
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
          error instanceof Error ? { message: error.message, stack: error.stack } : String(error),
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
console.log(`Native structured accessibility ${checks.length} groups PASS`)
