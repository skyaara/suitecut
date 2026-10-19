// SPDX-License-Identifier: MIT
/* global console, setTimeout */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, expectNative, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = '.suitecut/native-accessible-description'
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
/**
 * Reads only Chromium's actual backend-attested AX description for the node.
 * @param {string} selector
 */
async function inspector(selector) {
  const doc = z
    .object({ root: z.object({ nodeId: z.number().positive() }) })
    .parse(await page.sendDevToolsCommand('DOM.getDocument', { depth: 0 }))
  const node = z
    .object({ nodeId: z.number().positive() })
    .parse(
      await page.sendDevToolsCommand('DOM.querySelector', { nodeId: doc.root.nodeId, selector }),
    )
  const backend = z
    .object({ node: z.object({ backendNodeId: z.number().positive() }) })
    .parse(await page.sendDevToolsCommand('DOM.describeNode', { nodeId: node.nodeId, depth: 0 }))
  const tree = z
    .object({
      nodes: z.array(
        z.object({
          backendDOMNodeId: z.number().optional(),
          name: z.object({ value: z.string() }).optional(),
          description: z.object({ value: z.string() }).optional(),
        }),
      ),
    })
    .parse(
      await page.sendDevToolsCommand('Accessibility.getPartialAXTree', {
        backendNodeId: backend.node.backendNodeId,
        fetchRelatives: false,
      }),
    )
  const actual = tree.nodes.find(
    (candidate) => candidate.backendDOMNodeId === backend.node.backendNodeId,
  )
  assert(actual)
  observations.push({ selector, backend: backend.node.backendNodeId, actual })
  return actual
}
const ownership = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
})
const server = createServer((_request, response) => {
  response.setHeader('content-type', 'text/html; charset=utf-8')
  response.end('<button aria-label="Same" title="Same">Fallback</button>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
try {
  await page.setContent(
    '<button id="label" aria-label="Same" title="Same">Fallback</button><button id="content" title="Same">Same</button>',
  )
  for (const selector of ['#label', '#content']) {
    const actual = await inspector(selector)
    assert.equal(actual.name?.value, 'Same')
    if (selector === '#label') assert.equal(actual.description?.value, 'Same')
    else assert.equal(actual.description, undefined) // Actual Chromium omits the equal content/title description; authored DOM semantics remain independently asserted below.
    await expectNative(page.locator(selector)).toHaveAccessibleDescription('Same', {
      timeout: 1000,
    })
  }
  assert.equal(await page.getByRole('button', { description: 'Same', exact: true }).count(), 2)
  checks.push(
    'a title remains a genuine description when a separate label or content supplied an identical name',
  )

  await page.setContent(
    '<button id="described" aria-describedby="hidden hint" aria-description="Lower">Save</button><span id="hidden" hidden aria-label="Hidden alternative">Invisible fallback</span><span id="hint" aria-label="Actual alternative">Visible fallback</span>',
  )
  const described = await inspector('#described')
  assert.equal(described.description?.value, 'Hidden alternative Actual alternative')
  await expectNative(page.locator('#described')).toHaveAccessibleDescription(
    'Hidden alternative Actual alternative',
    { timeout: 1000 },
  )
  assert.equal(
    await page
      .getByRole('button', { description: 'Hidden alternative Actual alternative', exact: true })
      .count(),
    1,
  )
  checks.push(
    'ordered aria-describedby references use their actual hidden and explicit text alternatives before aria-description',
  )

  await page.setContent(
    '<button id="title" title="Only title"></button><img id="image" alt="Same" title="Same"><button id="explicit" title="Lower" aria-description="Explicit">Name</button>',
  )
  const title = await inspector('#title')
  assert.equal(title.name?.value, 'Only title')
  assert.equal(title.description?.value ?? '', '')
  await expectNative(page.locator('#title')).toHaveAccessibleDescription('', { timeout: 1000 })
  const image = await inspector('#image')
  assert.equal(image.description?.value, 'Same')
  await expectNative(page.locator('#image')).toHaveAccessibleDescription('Same', { timeout: 1000 })
  await expectNative(page.locator('#explicit')).toHaveAccessibleDescription('Explicit', {
    timeout: 1000,
  })
  checks.push(
    'actual title-name consumption suppresses reuse while independent image alt and explicit description sources retain precedence',
  )

  await page.locator('#explicit').evaluate((element) => {
    setTimeout(() => element.setAttribute('aria-description', 'Changed'), 30)
  })
  await expectNative(page.locator('#explicit')).toHaveAccessibleDescription('Changed', {
    timeout: 1000,
  })
  await assert.rejects(
    expectNative(page.locator('#explicit')).toHaveAccessibleDescription('Never', { timeout: 15 }),
    /15ms|timed out/u,
  )
  checks.push(
    'the existing native assertion owner retries real metadata changes and retains the original failure deadline',
  )

  await page.goto(`http://127.0.0.1:${address.port}`)
  await page.setContent(`<iframe id="child" src="http://localhost:${address.port}/child"></iframe>`)
  const child = page.frameLocator('#child').getByRole('button')
  await child.waitFor()
  await expectNative(child).toHaveAccessibleDescription('Same', { timeout: 1000 })
  const exact = 'Actual\ud800 {"__suitecutNativeUTF16Scalar":"dfff"}'
  await child.evaluate((element, value) => element.setAttribute('aria-description', value), exact)
  await expectNative(child).toHaveAccessibleDescription(exact, { timeout: 1000 })
  assert.equal(
    await page
      .frameLocator('#child')
      .getByRole('button', { description: exact, exact: true })
      .count(),
    1,
  )
  checks.push(
    'actual OOPIF metadata and isolated UTF16 descriptions use native frame identity and typed observation transport',
  )

  await page.goto('about:blank')
  const deadline = performance.now() + 3000
  let state = ownership.parse(await source.runAutomation('diagnostics'))
  while (Object.values(state).some((value) => value !== 0) && performance.now() < deadline) {
    await delay(2)
    state = ownership.parse(await source.runAutomation('diagnostics'))
  }
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
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
  try {
    await finishNativeProof(source, diagnostics, directory, errors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
if (errors.length) throw errors[0]
console.log(`Native accessible description ${checks.length} groups PASS`)
