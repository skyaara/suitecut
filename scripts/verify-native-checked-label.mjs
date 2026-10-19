// SPDX-License-Identifier: MIT
/* global HTMLInputElement */
import assert from 'node:assert/strict'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, expectNative, launchNativeBrowser } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(z.string().parse(process.env.SUITECUT_NATIVE_PROOF_DIRECTORY))
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
const options = { timeout: 1000 }
try {
  for (const html of [
    '<label id=label><input type=checkbox id=actual></label>',
    '<label id=label for=actual>Name</label><input type=checkbox id=actual>',
  ]) {
    await page.setContent(html)
    assert.equal(
      await page.evaluate(
        "document.querySelector('#label').control===document.querySelector('#actual')",
      ),
      true,
    )
    const label = page.locator('#label')
    await expectNative(label).not.toBeChecked(options)
    await expectNative(label).toBeChecked({ ...options, checked: false })
    await page.locator('#actual').evaluate((element) => {
      if (!(element instanceof HTMLInputElement)) throw Error('Actual checkbox missing')
      element.checked = true
    })
    await expectNative(label).toBeChecked(options)
    await expectNative(label).toBeChecked({ ...options, checked: true })
    await page.evaluate(
      "globalThis.checkEvents=[];for(const name of ['input','change'])document.querySelector('#actual').addEventListener(name,event=>globalThis.checkEvents.push({type:event.type,trusted:event.isTrusted,id:event.target.id}))",
    )
    await label.uncheck()
    await expectNative(label).not.toBeChecked(options)
    await label.check()
    await expectNative(label).toBeChecked(options)
    assert.deepEqual(await page.evaluate('globalThis.checkEvents'), [
      { type: 'input', trusted: true, id: 'actual' },
      { type: 'change', trusted: true, id: 'actual' },
      { type: 'input', trusted: true, id: 'actual' },
      { type: 'change', trusted: true, id: 'actual' },
    ])
    await page.locator('#actual').evaluate((element) => {
      if (!(element instanceof HTMLInputElement)) throw Error('Actual checkbox missing')
      element.indeterminate = true
    })
    await expectNative(label).toBeChecked({ ...options, indeterminate: true })
    await expectNative(label).toBeChecked(options)
  }
  checks.push(
    'Genuine implicit and explicit labels observe the real checkbox checked/unchecked/indeterminate states and trusted native check transitions',
  )
  await page.setContent(
    '<label for=other><input id=actual type=checkbox><div id=aria role=checkbox aria-checked=true></div></label><input id=other type=checkbox checked>',
  )
  await expectNative(page.locator('#actual')).not.toBeChecked(options)
  await expectNative(page.locator('#aria')).toBeChecked(options)
  await page.setContent(
    '<label id=label for=actual>Name</label><input id=actual type=text><div id=plain>Plain</div>',
  )
  await assert.rejects(
    expectNative(page.locator('#label')).toBeChecked(options),
    /Element is not a checkbox or radio/u,
  )
  await assert.rejects(
    expectNative(page.locator('#plain')).toBeChecked(options),
    /Element is not a checkbox or radio/u,
  )
  checks.push(
    'Intrinsic selected input and ARIA controls retain their own identity; associated non-checkbox controls and unassociated nodes preserve genuine errors',
  )
  await page.setContent(
    '<label id=label for=actual hidden>Name</label><input id=actual type=checkbox checked disabled hidden><input id=outside>',
  )
  await page.locator('#outside').focus()
  await page.evaluate(
    "globalThis.events=[];for(const name of ['focus','blur','input','change'])document.addEventListener(name,e=>globalThis.events.push(e.type),true)",
  )
  await expectNative(page.locator('#label')).toBeChecked(options)
  assert.equal(await page.evaluate('document.activeElement.id'), 'outside')
  assert.deepEqual(await page.evaluate('globalThis.events'), [])
  checks.push(
    'Hidden disabled checked controls remain inspectable through labels without focus/input/change side effects',
  )
  await page.setContent(
    '<div id=host></div><iframe id=child srcdoc="<label id=label for=actual>Name</label><input id=actual type=checkbox checked>"></iframe>',
  )
  await page.evaluate(
    "document.querySelector('#host').attachShadow({mode:'open'}).innerHTML='<label id=shadow for=actual>Shadow</label><input id=actual type=checkbox checked>';Object.defineProperty(HTMLLabelElement.prototype,'control',{configurable:true,get(){throw Error('Author control getter must not run')}})",
  )
  await expectNative(page.locator('#shadow')).toBeChecked(options)
  const child = page.frameLocator('#child')
  await child.locator('#actual').waitFor({ state: 'attached' })
  await expectNative(child.locator('#label')).toBeChecked(options)
  checks.push(
    'Real shadow/frame label controls are observed in their native utility document despite a poisoned main-world label.control getter',
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
