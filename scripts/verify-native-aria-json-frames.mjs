// SPDX-License-Identifier: MIT
/* global console, document, HTMLTextAreaElement */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance as hostPerformance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-aria-json-frames'
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
const ownership = z.object({
  nativeSnapshotFrameBatches: z.number(),
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
/**
 * Traverses only genuine native observations, including emitted child forests.
 * @param {import('../src/native-aria.js').NativeAriaJSONNode[]} nodes
 * @param {(node: import('../src/native-aria.js').NativeAriaJSONNode) => boolean} predicate
 */
function find(nodes, predicate) {
  const pending = [...nodes]
  while (pending.length) {
    const node = pending.pop()
    if (!node) break
    if (predicate(node)) return node
    if (node.children)
      for (const child of node.children) if (typeof child !== 'string') pending.push(child)
  }
  return undefined
}
async function retired() {
  const deadline = hostPerformance.now() + 3000
  let state = ownership.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && hostPerformance.now() < deadline) {
    await delay(2)
    state = ownership.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  return state
}
const server = createServer((request, response) => {
  response.setHeader('content-type', 'text/html; charset=utf-8')
  response.end(
    request.url === '/big'
      ? '<button>Actual excessive child</button>'.repeat(5000)
      : '<button>Actual child</button><textarea title="Actual value"></textarea>',
  )
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const main = `http://127.0.0.1:${address.port}`,
  cross = `http://localhost:${address.port}`
try {
  await page.setContent(
    '<button>Parent</button><iframe id="same" srcdoc="<button>Actual same-process child</button>"></iframe>',
  )
  await page.frameLocator('#same').getByRole('button').waitFor()
  let snapshot = await page.ariaSnapshotJSON({ mode: 'ai' })
  let iframe = find(snapshot, (node) => node.role === 'iframe')
  assert(iframe?.children)
  let child = find(
    iframe.children.filter((node) => typeof node !== 'string'),
    (node) => node.role === 'button',
  )
  assert(child?.ref)
  assert.equal(await page.getByRef(child.ref).textContent(), 'Actual same-process child')
  observations.push({ sameProcess: snapshot })
  checks.push(
    'actual same-process iframe contents and retained child references compose under their genuine parent',
  )

  await page.goto(main)
  await page.setContent(`<button>Parent</button><iframe id="cross" src="${cross}/child"></iframe>`)
  await page.frameLocator('#cross').getByRole('button').waitFor()
  snapshot = await page.ariaSnapshotJSON({ mode: 'ai' })
  iframe = find(snapshot, (node) => node.role === 'iframe')
  assert(iframe?.children)
  child = find(
    iframe.children.filter((node) => typeof node !== 'string'),
    (node) => node.role === 'button',
  )
  assert(child?.ref)
  const documentNode = z
    .object({ root: z.object({ nodeId: z.number().positive() }) })
    .parse(await page.sendDevToolsCommand('DOM.getDocument', { depth: 0 }))
  const ownerNode = z.object({ nodeId: z.number().positive() }).parse(
    await page.sendDevToolsCommand('DOM.querySelector', {
      nodeId: documentNode.root.nodeId,
      selector: '#cross',
    }),
  )
  const actualOwner = z
    .object({
      node: z.object({ backendNodeId: z.number().positive(), frameId: z.string().min(1) }),
    })
    .parse(
      await page.sendDevToolsCommand('DOM.describeNode', { nodeId: ownerNode.nodeId, depth: 0 }),
    )
  const target = await page.getByRef(child.ref).elementHandle()
  assert(target)
  const actualFrame = await target.ownerFrame()
  assert(actualFrame)
  await target.dispose()
  assert.equal(actualFrame.identity(), actualOwner.node.frameId)
  assert(
    z.object({ sessions: z.number().positive() }).parse(await source.runAutomation('diagnostics')),
  )
  observations.push({ crossProcess: snapshot, actualOwner: actualOwner.node })
  checks.push(
    'OOPIF references resolve to the exact child frame attested by the real parent-session backend node',
  )

  await page
    .frameLocator('#cross')
    .locator('body')
    .evaluate((body, url) => {
      const frame = document.createElement('iframe')
      frame.id = 'nested'
      frame.src = url
      body.appendChild(frame)
    }, main + '/nested')
  await page.frameLocator('#cross').frameLocator('#nested').getByRole('button').waitFor()
  snapshot = await page.ariaSnapshotJSON({ mode: 'ai' })
  iframe = find(snapshot, (node) => node.role === 'iframe')
  assert(iframe?.children)
  const nested = find(
    iframe.children.filter((node) => typeof node !== 'string'),
    (node) => node.role === 'iframe',
  )
  assert(nested?.children)
  const leaf = find(
    nested.children.filter((node) => typeof node !== 'string'),
    (node) => node.role === 'button',
  )
  assert(leaf?.ref)
  assert.equal(await page.getByRef(leaf.ref).textContent(), 'Actual child')
  checks.push(
    'genuine nested frame forests merge bottom-up while every reference retains its actual document',
  )

  await page.locator('#cross').evaluate((frame) => {
    frame.setAttribute('style', 'display:none')
  })
  snapshot = await page.ariaSnapshotJSON({ mode: 'ai' })
  assert.equal(
    find(snapshot, (node) => node.role === 'iframe'),
    undefined,
  )
  assert.equal(await page.getByRef(leaf.ref).textContent(), 'Actual child')
  checks.push(
    'CSS-hidden frame owners are absent from the actual emitted forest without fabricated empty captures or global child eviction',
  )

  await page.goto(main)
  await page.setContent(
    `<section id="scope" aria-owns="owned"><button>Scope</button></section><iframe id="owned" src="${cross}/owned"></iframe>`,
  )
  await page.frameLocator('#owned').getByRole('button').waitFor()
  snapshot = await page.locator('#scope').ariaSnapshotJSON({ mode: 'ai' })
  iframe = find(snapshot, (node) => node.role === 'iframe')
  assert(iframe?.children)
  child = find(
    iframe.children.filter((node) => typeof node !== 'string'),
    (node) => node.role === 'button',
  )
  assert(child?.ref)
  assert.equal(await page.getByRef(child.ref).textContent(), 'Actual child')
  checks.push(
    'aria-owned iframe records outside the DOM subtree use actual backend identity and their genuine child document',
  )

  const exact = '\ud800 {"__suitecutNativeUTF16Scalar":"dfff"}'
  await page
    .frameLocator('#owned')
    .getByRole('textbox')
    .evaluate((element, value) => {
      if (!(element instanceof HTMLTextAreaElement)) throw new Error('Actual textarea missing')
      element.value = value
    }, exact)
  snapshot = await page.ariaSnapshotJSON({ mode: 'ai' })
  assert.equal(find(snapshot, (node) => node.role === 'textbox')?.text, exact)
  checks.push(
    'exact isolated UTF16 and ordinary tag-shaped child text survive native JSON composition without publishing a validation projection',
  )

  const oldChild = find(snapshot, (node) => node.role === 'button' && node.name === 'Actual child'),
    oldParent = find(snapshot, (node) => node.role === 'button' && node.name === 'Scope')
  assert(oldChild?.ref && oldParent?.ref)
  const newer = await page.ariaSnapshotJSON({ mode: 'ai' }),
    newerChild = find(newer, (node) => node.role === 'button' && node.name === 'Actual child')
  assert(newerChild?.ref)
  assert.equal(newerChild.ref, oldChild.ref)
  assert.equal(
    find(newer, (node) => node.role === 'button' && node.name === 'Scope')?.ref,
    oldParent.ref,
  )
  assert.equal(await page.getByRef(oldChild.ref).textContent(), 'Actual child')
  assert.equal(await page.getByRef(oldParent.ref).textContent(), 'Scope')
  assert.equal(await page.getByRef(newerChild.ref).textContent(), 'Actual child')
  checks.push(
    'one successful aggregate publication atomically transfers stable main and child references to the newly retained arrays',
  )

  await page.goto(main)
  await page.setContent('<button>Preserved old publication</button>')
  const previous = find(
    await page.ariaSnapshotJSON({ mode: 'ai' }),
    (node) => node.role === 'button',
  )
  assert(previous?.ref)
  await page.evaluate((url) => {
    const frame = document.createElement('iframe')
    frame.id = 'excessive'
    frame.src = url
    document.body.appendChild(frame)
  }, cross + '/big')
  await page.frameLocator('#excessive').getByRole('button').first().waitFor()
  await assert.rejects(
    page.ariaSnapshotJSON({ mode: 'ai', timeout: 5000 }),
    /node limit|capacity|exceeds|limit exceeded/u,
  )
  assert.equal(await page.getByRef(previous.ref).textContent(), 'Preserved old publication')
  checks.push(
    'an actual excessive child capture fails before commit and preserves the previous connected main-frame publication',
  )

  await page.goto(main)
  await page.setContent(`<iframe id="held" src="${cross}/held"></iframe>`)
  await page.frameLocator('#held').getByRole('button').waitFor()
  const owner = await page.$('#held')
  assert(owner)
  const held = await owner.contentFrame()
  await owner.dispose()
  assert(held)
  let startedResolve = () => undefined
  const started = new Promise((resolve) => {
    startedResolve = () => resolve(undefined)
  })
  const binding = await page.exposeFunction('actualAggregateHoldStarted', () => startedResolve())
  const busy = held.evaluate(
    '(()=>{globalThis.actualAggregateHoldStarted();const until=performance.now()+1000;while(performance.now()<until){};})()',
  )
  await started
  const abort = new AbortController(),
    cause = new Error('Actual aggregate capture cancellation')
  const pending = page.ariaSnapshotJSON({ mode: 'ai', timeout: 5000, signal: abort.signal }),
    rejection = assert.rejects(
      pending,
      (error) => error instanceof Error && error.name === 'AbortError' && error.cause === cause,
    )
  const until = hostPerformance.now() + 1500
  let beforeAbort = ownership.parse(await source.runAutomation('diagnostics'))
  while (
    (!beforeAbort.nativeSnapshotFrameBatches || !beforeAbort.nativeSelectorWaitPhysical) &&
    hostPerformance.now() < until
  ) {
    await delay(2)
    beforeAbort = ownership.parse(await source.runAutomation('diagnostics'))
  }
  assert(beforeAbort.nativeSnapshotFrameBatches && beforeAbort.nativeSelectorWaitPhysical)
  abort.abort(cause)
  await rejection
  const afterAbort = ownership.parse(await source.runAutomation('diagnostics'))
  assert.equal(afterAbort.nativeSnapshotFrameBatches, 0)
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  await busy
  await binding.dispose()
  observations.push({ beforeAbort, afterAbort })
  await page.goto('about:blank')
  const state = await retired()
  checks.push(
    'logical cancellation retires staged publication promptly while the actual renderer-held command retains physical ownership until its real ACK',
  )
  assert.equal(checks.length, 9)
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
console.log(`Native aggregate structured accessibility ${checks.length} groups PASS`)
