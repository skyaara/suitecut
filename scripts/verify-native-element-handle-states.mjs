// SPDX-License-Identifier: MIT
/* global document */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeElementHandle } from '../dist/native.js'

const directory = resolve('.suitecut/native-element-handle-states')
await mkdir(directory, { recursive: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
let stderr = ''
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic(message) {
    if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  await page.setContent(
    '<input id="input" disabled readonly><div id="plain">Plain</div><div id="contents" style="display:contents">Actual text</div><details><summary>Summary</summary><span id="closed">Hidden</span></details>',
  )
  const input = await page.locator('#input').elementHandle()
  assert(input)
  assert.equal(await input.isVisible(), true)
  assert.equal(await input.isHidden(), false)
  assert.equal(await input.isEnabled(), false)
  assert.equal(await input.isDisabled(), true)
  assert.equal(await input.isEditable(), false)
  await page.evaluate(() => {
    document.querySelector('input').disabled = false
    document.querySelector('input').readOnly = false
  })
  assert.equal(await input.isEnabled(), true)
  assert.equal(await input.isDisabled(), false)
  assert.equal(await input.isEditable(), true)
  const plain = await page.locator('#plain').elementHandle()
  assert(plain)
  await assert.rejects(plain.isEditable(), /not an input/)
  const contents = await page.locator('#contents').elementHandle()
  assert(contents)
  assert.equal(await contents.isVisible(), true)
  const closed = await page.locator('#closed').elementHandle()
  assert(closed)
  assert.equal(await closed.isVisible(), false)
  checks.push(
    'all five immediate fixed-handle state APIs use actual native visibility, disabled, readonly and editable semantics',
  )
  const value = 'original\ud800\0\\"\udfff😀'
  await page.evaluate((value) => {
    document.querySelector('#plain').setAttribute('data-original', value)
    document.querySelector('#plain').textContent = value
  }, value)
  await page.evaluate(() => {
    document.querySelector('#plain').remove()
    document.body.insertAdjacentHTML('beforeend', '<div id="plain">Replacement</div>')
  })
  assert.equal(await plain.isVisible(), false)
  assert.equal(await plain.isHidden(), true)
  await assert.rejects(plain.isEnabled(), /not attached/)
  assert.equal(await plain.textContent(), value)
  assert.equal(await plain.getAttribute('data-original'), value)
  assert.equal(await page.locator('#plain').isVisible(), true)
  checks.push(
    'original detached node retains exact UTF16 getter values and immediate hidden state while same-selector replacement stays independent',
  )
  const controller = new AbortController()
  const projected = new NativeElementHandle(
    createNativePage(source, controller.signal),
    input.id,
    '<input>',
  )
  controller.abort(new Error('actual state projection cancellation'))
  await assert.rejects(projected.isVisible(), /actual state projection cancellation/)
  assert.equal(await input.isVisible(), true)
  checks.push(
    'actual aborted page projection rejects its immediate read without disposing canonical retained handle',
  )
  for (const handle of [input, plain, contents, closed]) await handle.dispose()
  await page.setContent('<iframe srcdoc="<input disabled aria-label=child>"></iframe>')
  const child = await page.frameLocator('iframe').locator('input').elementHandle({ timeout: 2000 })
  assert(child)
  assert.equal(await child.isVisible(), true)
  assert.equal(await child.isDisabled(), true)
  assert.equal(await child.getAttribute('aria-label'), 'child')
  await child.dispose()
  checks.push(
    'genuine returned child-frame handle reads state and attributes from its owning native frame',
  )
  const state = z
    .object({
      activeHandles: z.number(),
      pendingHandles: z.number(),
      evaluationGroups: z.number(),
      otherOperations: z.number(),
      nativeElementStates: z.number(),
      nativeElementStatePhysical: z.number(),
      nativeElementStateReleases: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  const report = {
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks,
    state,
  }
  await writeFile(directory + '/report.json', JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
