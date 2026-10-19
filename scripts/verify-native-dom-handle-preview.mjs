// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-dom-handle-preview',
)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 420,
  height: 320,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
/** @type {unknown[]} */
const behaviorErrors = []
/** @type {string[]} */
const checks = []
/**
 * Observe a real native-owned handle and release its physical ownership.
 * @param {string} expression
 * @param {string} expected
 */
async function caption(expression, expected) {
  const handle = await page.evaluateHandle(expression)
  try {
    assert.equal(String(handle), expected)
  } finally {
    await handle.dispose()
  }
}
try {
  await page.setContent(
    '<div id="outer" name="value"><div id="inner">Text,\nmore text</div></div><input id="check" type=checkbox checked foo="bar&quot;">',
  )
  await caption("document.querySelector('#outer')", 'JSHandle@<div id="outer" name="value">…</div>')
  await caption(
    "document.querySelector('#inner')",
    'JSHandle@<div id="inner">Text,↵more text</div>',
  )
  await caption("document.querySelector('#inner').firstChild", 'JSHandle@#text=Text,↵more text')
  await caption(
    "document.querySelector('#check')",
    'JSHandle@<input checked id="check" foo="bar"" type="checkbox"/>',
  )
  checks.push(
    'Genuine node fields produce collapsed nested markup, newline text captions and native boolean/id/lexical attribute display without affecting handles',
  )
  const emoji = '😛'.repeat(100)
  await page.setContent('<div id=unicode></div>')
  await page.evaluate((value) => {
    const element = document.querySelector('#unicode')
    if (!element) throw Error('Unicode node missing')
    element.setAttribute('title', value)
    element.textContent = value
  }, emoji)
  await caption(
    "document.querySelector('#unicode')",
    `JSHandle@<div id="unicode" title="${emoji}">${'😛'.repeat(49)}…</div>`,
  )
  const isolated = 'a\ud800b\udfffc'
  await page.evaluate((value) => {
    const element = document.querySelector('#unicode')
    if (!element) throw Error('Unicode node missing')
    element.setAttribute('title', value)
    element.textContent = value
  }, isolated)
  await caption(
    "document.querySelector('#unicode')",
    `JSHandle@<div id="unicode" title="${isolated}">${isolated}</div>`,
  )
  checks.push(
    'Actual attributes retain all non-ASCII/isolated UTF16 units while bounded text captions preserve complete surrogate pairs',
  )
  await page.setContent(
    '<div id=host></div><iframe id=child srcdoc="<div id=inside>Child</div>"></iframe>',
  )
  await page.evaluate(
    "document.querySelector('#host').attachShadow({mode:'open'}).innerHTML='<span>Shadow</span>';globalThis.actualComment=document.createComment('Comment\\nvalue');globalThis.actualFragment=document.createDocumentFragment();Object.defineProperty(Element.prototype,'outerHTML',{configurable:true,get(){throw Error('Author outerHTML getter must not run')}});Object.defineProperty(Element.prototype,'attributes',{configurable:true,get(){throw Error('Author attributes getter must not run')}});Object.defineProperty(Node.prototype,'nodeValue',{configurable:true,get(){throw Error('Author nodeValue getter must not run')}});void 0",
  )
  await caption('globalThis.actualComment', 'JSHandle@<!--Comment↵value-->')
  await caption('globalThis.actualFragment', 'JSHandle@#document-fragment')
  await caption("document.querySelector('#host').shadowRoot", 'JSHandle@#document-fragment')
  const frame = page.frameLocator('#child')
  await frame.locator('#inside').waitFor({ state: 'attached' })
  const child = await frame.locator('#inside').elementHandle()
  assert(child)
  assert.equal(String(child), 'JSHandle@<div id="inside">Child</div>')
  await child.dispose()
  checks.push(
    'Native Inspector metadata preserves comment/fragment/shadow/frame provenance and bypasses poisoned author property getters',
  )
  const finalOwnership = z
    .object({
      otherOperations: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      evaluationGroups: z.number(),
      nativeSelectorWaits: z.number(),
      nativeSelectorWaitPhysical: z.number(),
      pendingDevTools: z.number(),
      contextRetryQueued: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert(Object.values(finalOwnership).every((value) => value === 0))
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: true, checks, finalOwnership }, null, 2) + '\n',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, checks, error: String(error) }, null, 2) + '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, diagnostics, directory, behaviorErrors)
}
