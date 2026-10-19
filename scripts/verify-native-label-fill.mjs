// SPDX-License-Identifier: MIT
/* global Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import * as z from 'zod'

import {
  createNativePage,
  launchNativeBrowser,
  NativeSelectors,
  NativeTimeoutError,
} from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-label-fill',
)
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath,
  width: 420,
  height: 320,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(2000)
const binarySHA256 = createHash('sha256')
  .update(await readFile(executablePath))
  .digest('hex')
const events = z.array(z.object({ type: z.string(), id: z.string(), trusted: z.boolean() }))
const ownership = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
  pausedActions: z.number(),
  contextRetryQueued: z.number(),
})
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const behaviorErrors = []
const installJournal = () =>
  page.evaluate(
    "globalThis.actualLabelInputEvents=[];for(const type of ['input','change'])document.addEventListener(type,event=>globalThis.actualLabelInputEvents.push({type:event.type,id:event.target.id,trusted:event.isTrusted}),true)",
  )
const journal = async () => events.parse(await page.evaluate('globalThis.actualLabelInputEvents'))
try {
  await page.setContent(
    '<!doctype html><label id=label for=actual>Name <span id=desc>required</span></label><input id=actual value=ORIGINAL><input id=other>',
  )
  await installJournal()
  assert.equal(
    await page.evaluate(
      "document.querySelector('#label').control===document.querySelector('#actual')",
    ),
    true,
  )
  await page.locator('#label').fill('NATIVE LABEL')
  assert.equal(await page.locator('#actual').inputValue(), 'NATIVE LABEL')
  assert.equal(await page.evaluate('document.activeElement.id'), 'actual')
  let actualEvents = await journal()
  assert(
    actualEvents.some((event) => event.type === 'input' && event.id === 'actual' && event.trusted),
  )
  assert(actualEvents.every((event) => event.id === 'actual' && event.trusted))
  await page.locator('#other').focus()
  assert.equal(
    (await journal()).filter(
      (event) => event.type === 'change' && event.id === 'actual' && event.trusted,
    ).length,
    1,
  )
  await page.locator('#desc').clear()
  assert.equal(await page.locator('#actual').inputValue(), '')
  actualEvents = await journal()
  assert(
    actualEvents.filter((event) => event.type === 'input' && event.id === 'actual' && event.trusted)
      .length >= 2,
  )
  checks.push(
    'Explicit label and descendant resolve genuine label.control before real trusted fill/clear input and change events',
  )

  await page.setContent(
    '<!doctype html><label id=implicit>Comment <span id=implicitDesc>text</span><textarea id=area>BEFORE</textarea></label><label id=orphan>no control</label>',
  )
  await page.locator('#implicitDesc').fill('actual textarea')
  assert.equal(await page.locator('#area').inputValue(), 'actual textarea')
  await page.locator('#implicit').clear()
  assert.equal(await page.locator('#area').inputValue(), '')
  await assert.rejects(page.locator('#orphan').fill('invalid'), /not an.*input/iu)
  await page.setContent(
    '<!doctype html><label for=external id=conflict><input id=intrinsic value=OWN><div id=editable contenteditable>OWN EDITABLE</div></label><input id=external value=EXTERNAL>',
  )
  await page.locator('#intrinsic').fill('INTRINSIC')
  await page.locator('#editable').fill('EDITED HOST')
  assert.equal(await page.locator('#external').inputValue(), 'EXTERNAL')
  assert.equal(await page.locator('#intrinsic').inputValue(), 'INTRINSIC')
  assert.equal(await page.locator('#editable').textContent(), 'EDITED HOST')
  checks.push(
    'Implicit textarea associations work; orphan labels reject and directly selected inputs/contenteditables retain their own identities under conflicting label associations',
  )

  for (const attribute of ['disabled', 'readonly', 'hidden']) {
    await page.setContent(
      `<!doctype html><label for=actual id=label>Visible label</label><input id=actual value=UNCHANGED ${attribute}>`,
    )
    page.setDefaultTimeout(70)
    await assert.rejects(
      page.locator('#label').fill('blocked'),
      (error) => error instanceof NativeTimeoutError,
    )
    await assert.rejects(
      page.locator('#label').clear(),
      (error) => error instanceof NativeTimeoutError,
    )
    assert.equal(await page.locator('#actual').inputValue(), 'UNCHANGED')
    page.setDefaultTimeout(2000)
    await page.evaluate(
      `document.querySelector('#actual').removeAttribute(${JSON.stringify(attribute)})`,
    )
    await page.locator('#label').fill('ENABLED')
    assert.equal(await page.locator('#actual').inputValue(), 'ENABLED')
  }
  checks.push(
    'Visibility, disabled and readonly checks apply to the associated control under the original native default deadline rather than to the visible label',
  )

  await page.setContent(
    '<!doctype html><label id=label for=old>Old association</label><input id=old value=OLD><input id=new value=NEW>',
  )
  const pinned = await page.$('#label')
  assert(pinned)
  await page.evaluate(
    "document.querySelector('#label').id='renamed';const replacement=document.createElement('label');replacement.id='label';replacement.htmlFor='new';replacement.textContent='New association';document.body.append(replacement)",
  )
  await pinned.fill('PINNED')
  assert.equal(await page.locator('#old').inputValue(), 'PINNED')
  assert.equal(await page.locator('#new').inputValue(), 'NEW')
  await assert.rejects(page.locator('label').fill('duplicate'), /Strict|strict/u)
  await pinned.evaluate((element) => {
    if (!(element instanceof Element)) throw Error('The pinned label is not an Element')
    element.remove()
  })
  await assert.rejects(pinned.clear(), /not attached|detached|unavailable/iu)
  assert.equal(await page.locator('#new').inputValue(), 'NEW')
  await pinned.dispose()
  checks.push(
    'A pinned label keeps the real selected label identity across selector replacement; detachment fails without editing a replacement control and duplicate locators remain strict',
  )

  await page.setContent(
    '<!doctype html><div id=host></div><iframe id=child srcdoc="<!doctype html><label id=childLabel for=childInput>Child</label><input id=childInput value=CHILD>"></iframe>',
  )
  await page.evaluate(
    "document.querySelector('#host').attachShadow({mode:'open'}).innerHTML='<label id=shadowLabel for=shadowInput>Shadow</label><input id=shadowInput value=SHADOW>'",
  )
  await page.locator('#shadowLabel').fill('REAL SHADOW')
  assert.equal(await page.locator('#shadowInput').inputValue(), 'REAL SHADOW')
  const frame = page.frameLocator('#child')
  await frame.locator('#childLabel').fill('REAL CHILD')
  assert.equal(await frame.locator('#childInput').inputValue(), 'REAL CHILD')
  await frame.locator('#childLabel').clear()
  assert.equal(await frame.locator('#childInput').inputValue(), '')
  checks.push(
    'Actual open-shadow and lazy child-frame labels resolve controls in their owning document without changing root input identity',
  )

  await page.setContent(
    '<!doctype html><label id=label for=actual>Isolated</label><input id=actual>',
  )
  await installJournal()
  await page.evaluate(
    "globalThis.labelFactoryWorld=true;globalThis.authorClosestCalls=0;globalThis.originalClosest=Element.prototype.closest;Element.prototype.closest=function(){globalThis.authorClosestCalls++;throw Error('Author closest must not run')}",
  )
  const selectors = new NativeSelectors(source)
  await selectors.register('actualLabelMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'labelFactoryWorld') !== true)
        throw Error('Main selector factory did not use main realm')
      return root.querySelectorAll(selector)
    },
  }))
  await page.locator('actualLabelMain=#label').fill('GENUINE UTILITY')
  assert.equal(await page.locator('#actual').inputValue(), 'GENUINE UTILITY')
  assert.equal(await page.evaluate('globalThis.authorClosestCalls'), 0)
  assert((await journal()).some((event) => event.id === 'actual' && event.trusted))
  await page.evaluate('Element.prototype.closest=globalThis.originalClosest')
  checks.push(
    'A main-world selector factory returns the actual label while isolated utility DOM methods perform native retarget and trusted input despite author prototype replacement',
  )

  const deadline = performance.now() + 3000
  let finalOwnership
  do {
    finalOwnership = ownership.parse(await source.runAutomation('diagnostics'))
    assert(performance.now() < deadline, 'Actual native label/control ownership did not settle')
  } while (Object.values(finalOwnership).some((value) => value !== 0))
  assert.equal(checks.length, 6)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: true, binarySHA256, checks, finalOwnership }, null, 2) + '\n',
  )
} catch (error) {
  behaviorErrors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, binarySHA256, checks, error: String(error) }, null, 2) +
      '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, diagnostics, directory, behaviorErrors)
}
console.info(
  'Native associated-label fill/clear6 passed with real input, control actionability, exact node identity and ownership cleanup',
)
