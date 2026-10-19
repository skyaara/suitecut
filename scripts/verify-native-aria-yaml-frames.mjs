// SPDX-License-Identifier: MIT
/* global console, document */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-aria-yaml-frames'
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
/**
 * Reads only a genuine token emitted on the named snapshot record.
 * @param {string} yaml
 * @param {string} name
 */
function named(yaml, name) {
  const line = yaml.split('\n').find((value) => value.includes(JSON.stringify(name)))
  assert(line, `Actual ${name} missing`)
  const tokens = [...line.matchAll(/\[ref=(r[A-Za-z0-9]{1,64}_[1-9][0-9]*_(?:0|[1-9][0-9]*))\]/gu)]
  assert.equal(tokens.length, 1)
  return z.string().parse(tokens[0]?.[1])
}
/**
 * Finds a genuine named record beneath the actual JSON root containers.
 * @param {import('../src/native-aria.js').NativeAriaJSONNode[]} forest
 * @param {string} name
 */
function actualButton(forest, name) {
  const pending = [...forest]
  while (pending.length) {
    const node = pending.pop()
    if (!node) break
    if (node.role === 'button' && node.name === name) return node
    if (node.children)
      for (const child of node.children) if (typeof child !== 'string') pending.push(child)
  }
  return undefined
}
async function retired() {
  const deadline = performance.now() + 3000
  let state = ownership.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(2)
    state = ownership.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  return state
}
const server = createServer((_request, response) => {
  response.setHeader('content-type', 'text/html; charset=utf-8')
  response.end('<button>Actual child</button>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const main = `http://127.0.0.1:${address.port}`,
  cross = `http://localhost:${address.port}`
try {
  await page.setContent(
    '<button>Actual parent</button><iframe id="same" srcdoc="<button>Actual same-process child</button>"></iframe>',
  )
  await page.frameLocator('#same').getByRole('button').waitFor()
  const markup = await page.locator('body').innerHTML(),
    plain = await page.ariaSnapshot()
  let yaml = await page.ariaSnapshot({ mode: 'ai' })
  assert.match(yaml, /- iframe.*:\n\s+- button "Actual same-process child"/u)
  assert.equal(
    await page.getByRef(named(yaml, 'Actual same-process child')).textContent(),
    'Actual same-process child',
  )
  assert.equal(await page.ariaSnapshot(), plain)
  assert.equal(await page.locator('body').innerHTML(), markup)
  observations.push({ sameProcess: yaml, plain })
  checks.push(
    'AI YAML directly formats actual child forests while default snapshots and genuine DOM remain unchanged',
  )

  await page.goto(main)
  await page.setContent(
    `<button>Actual parent</button><iframe id="cross" src="${cross}/child"></iframe>`,
  )
  await page.frameLocator('#cross').getByRole('button').waitFor()
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
  yaml = await page.ariaSnapshot({ mode: 'ai' })
  const childRef = named(yaml, 'Actual child')
  const doc = z
    .object({ root: z.object({ nodeId: z.number().positive() }) })
    .parse(await page.sendDevToolsCommand('DOM.getDocument', { depth: 0 }))
  const queried = z.object({ nodeId: z.number().positive() }).parse(
    await page.sendDevToolsCommand('DOM.querySelector', {
      nodeId: doc.root.nodeId,
      selector: '#cross',
    }),
  )
  const actual = z
    .object({
      node: z.object({ backendNodeId: z.number().positive(), frameId: z.string().min(1) }),
    })
    .parse(await page.sendDevToolsCommand('DOM.describeNode', { nodeId: queried.nodeId, depth: 0 }))
  const handle = await page.getByRef(childRef).elementHandle()
  assert(handle)
  const owner = await handle.ownerFrame()
  await handle.dispose()
  assert(owner)
  assert.equal(owner.identity(), actual.node.frameId)
  const owners = yaml.split('\n').filter((line) => line.includes('- iframe'))
  assert.equal(owners.length, 2)
  assert(z.string().parse(owners[1]).indexOf('-') > z.string().parse(owners[0]).indexOf('-'))
  observations.push({ nestedOOPIF: yaml, actualOwner: actual.node })
  checks.push(
    'genuine OOPIF and nested forests retain native parent ordering and actual backend-attested child ownership',
  )

  const boxed = await page.ariaSnapshot({ mode: 'ai', boxes: true }),
    shallow = await page.ariaSnapshot({ mode: 'ai', depth: 0 })
  assert.match(boxed, /- button "Actual child" \[box=[^\]]+\] \[ref=/u)
  assert.equal(shallow.includes('Actual child'), false)
  assert(shallow.includes('Actual parent'))
  checks.push('canonical YAML depth and real box fields apply after the native forests compose')

  const exact = 'Actual\ud800\0\\"\udfff {"__suitecutNativeUTF16Scalar":"dfff"}'
  await page
    .frameLocator('#cross')
    .getByRole('button')
    .evaluate((element, text) => {
      element.textContent = text
    }, exact)
  yaml = await page.ariaSnapshot({ mode: 'ai' })
  const exactRef = named(yaml, exact),
    oldParent = named(yaml, 'Actual parent')
  assert.equal(await page.getByRef(exactRef).textContent(), exact)
  assert(yaml.includes(JSON.stringify(exact)))
  checks.push(
    'isolated UTF16 and author tag-shaped names survive canonical YAML and exact retained-node lookup',
  )

  const replacement = await page.ariaSnapshot({ mode: 'ai' }),
    latest = named(replacement, exact)
  assert.equal(latest, exactRef)
  assert.equal(named(replacement, 'Actual parent'), oldParent)
  assert.equal(await page.getByRef(exactRef).textContent(), exact)
  assert.equal(await page.getByRef(oldParent).textContent(), 'Actual parent')
  assert.equal(await page.getByRef(latest).textContent(), exact)
  checks.push(
    'successful YAML publication atomically transfers stable main and child references to the newly retained arrays',
  )

  await page.locator('#cross').evaluate((element) => {
    element.setAttribute('style', 'display:none')
  })
  const hidden = await page.ariaSnapshot({ mode: 'ai' })
  assert.equal(hidden.includes('iframe'), false)
  assert.equal(await page.getByRef(latest).textContent(), exact)
  checks.push(
    'omitted hidden owners do not fabricate empty child captures or evict a still-live independent child publication',
  )

  await page.locator('#cross').evaluate((element) => {
    element.removeAttribute('style')
  })
  await page
    .frameLocator('#cross')
    .locator('body')
    .evaluate((body) => {
      body.replaceChildren()
      let parent = body
      for (let i = 0; i < 55; ++i) {
        const group = document.createElement('div')
        group.setAttribute('role', 'group')
        group.setAttribute('aria-label', 'G')
        parent.appendChild(group)
        parent = group
      }
      for (let i = 0; i < 240; ++i) {
        const button = document.createElement('button')
        button.textContent = 'x'
        parent.appendChild(button)
      }
    })
  const json = await page.ariaSnapshotJSON({ mode: 'ai' })
  assert(new TextEncoder().encode(JSON.stringify(json)).byteLength <= 65536)
  const parent = actualButton(json, 'Actual parent')
  assert(parent?.ref)
  await assert.rejects(
    page.ariaSnapshot({ mode: 'ai', timeout: 5000 }),
    /ARIA snapshot exceeds 64 KiB/u,
  )
  assert.equal(await page.getByRef(parent.ref).textContent(), 'Actual parent')
  checks.push(
    'a YAML-specific formatting bound fails before publication and preserves the previous connected main reference',
  )

  await page.goto('about:blank')
  const state = await retired()
  assert.equal(checks.length, 7)
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
  try {
    await finishNativeProof(source, diagnostics, directory, errors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
if (errors.length) throw errors[0]
console.log(`Native aggregate YAML accessibility ${checks.length} groups PASS`)
