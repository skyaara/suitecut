// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { launchNativeBrowser, NativeBrowserContexts } from '../dist/native.js'

const directory = '.suitecut/native-ownerless-elements'
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
const checks = [],
  handles = []
const schema = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  nativeOwnerFrameDocuments: z.number(),
  nativeOwnerFrameGroups: z.number(),
  nativeOwnerFramePhysical: z.number(),
  nativeOwnerFrameProcessPhysical: z.number(),
})
try {
  const browser = new NativeBrowserContexts(source),
    context = await browser.newContext(),
    page = await context.newPage()
  await page.setContent('<p>Actual live main document</p>')
  const body = (
    await page.evaluateHandle(
      () => document.implementation.createHTMLDocument('Actual unattached document').body,
    )
  ).asElement()
  assert(body)
  handles.push(body)
  assert.equal(await body.ownerFrame(), null)
  checks.push(
    'an actual body inside an unattached HTMLDocument has no live owning frame after native unique-context and retained-node validation',
  )
  const detached = (
    await page.evaluateHandle(() =>
      document.implementation.createHTMLDocument('Actual separate document').createElement('div'),
    )
  ).asElement()
  assert(detached)
  handles.push(detached)
  assert.equal(await detached.ownerFrame(), null)
  checks.push(
    'an actual detached element created by an unattached Document is ownerless without inferring from layout, attributes or author getters',
  )
  const retained = (
    await page.evaluateHandle(() => {
      const frame = document.createElement('iframe')
      frame.id = 'actual-ownerless-frame'
      document.body.appendChild(frame)
      return frame.contentDocument?.body
    })
  ).asElement()
  assert(retained)
  handles.push(retained)
  const child = await retained.ownerFrame()
  assert(child)
  assert.equal(child.parentFrame(), page.mainFrame())
  const iframe = await child.frameElement()
  try {
    assert.equal(await iframe.getAttribute('id'), 'actual-ownerless-frame')
    await iframe.evaluate((node) => {
      if (!(node instanceof Element)) throw new Error('Actual iframe owner element missing')
      node.remove()
    })
  } finally {
    await iframe.dispose()
  }
  assert.equal(await retained.ownerFrame(), null)
  checks.push(
    'the real retained child body changes from its genuine child frame to no live frame after actual iframe removal',
  )
  for (const handle of handles.splice(0)) await handle.dispose()
  for (let attempt = 0; attempt < 200; attempt++) {
    if (
      Object.values(schema.parse(await page.source.runAutomation('diagnostics'))).every(
        (value) => value === 0,
      )
    )
      break
    await delay(5)
  }
  const state = schema.parse(await page.source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(state)) assert.equal(value, 0, name)
  await context.close()
  await writeFile(directory + '/report.json', JSON.stringify({ checks, state }, null, 2) + '\n')
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '', 'Native ownerless element diagnostics must remain empty')
