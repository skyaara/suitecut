// SPDX-License-Identifier: MIT
/* global HTMLInputElement */
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
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-input-value',
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
  pendingBindingCommands: z.number(),
  bindingCalls: z.number(),
})
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const behaviorErrors = []
const installJournal = () =>
  page.evaluate(
    "globalThis.actualReadEvents=[];for(const type of ['focus','blur','input','change'])document.addEventListener(type,event=>globalThis.actualReadEvents.push({type:event.type,id:event.target.id,trusted:event.isTrusted}),true)",
  )
const active = () => page.evaluate('document.activeElement.id')
/**
 * Observe genuine native ownership under a bounded proof deadline.
 * @param {(state:z.output<typeof ownership>)=>boolean} predicate
 */
async function waitState(predicate) {
  const deadline = performance.now() + 3000
  while (true) {
    const state = ownership.parse(await source.runAutomation('diagnostics'))
    if (predicate(state)) return state
    assert(
      performance.now() < deadline,
      'Native input value ownership did not reach the expected actual state',
    )
  }
}
try {
  await page.setContent(
    '<!doctype html><label id=label for=actual>Name <span id=desc>required</span></label><input id=actual value=REAL><input id=other value=OTHER>',
  )
  await page.locator('#other').focus()
  await installJournal()
  assert.equal(
    await page.evaluate(
      "document.querySelector('#label').control===document.querySelector('#actual')",
    ),
    true,
  )
  for (const selector of ['#actual', '#label', '#desc'])
    assert.equal(await page.inputValue(selector), 'REAL')
  assert.equal(await active(), 'other')
  assert.deepEqual(await page.evaluate('globalThis.actualReadEvents'), [])
  checks.push(
    'Page/locator reads use real label.control and descendant identity without focus, blur, input or change side effects',
  )

  await page.setContent(
    '<!doctype html><label for=external><input id=own value=OWN><textarea id=area>AREA</textarea><select id=choice><option value=CHOICE selected>Choice</option></select><span id=labelChild>Label</span></label><input id=external value=EXTERNAL><input id=other>',
  )
  await page.locator('#other').focus()
  await installJournal()
  for (const [selector, expected] of [
    ['#own', 'OWN'],
    ['#area', 'AREA'],
    ['#choice', 'CHOICE'],
    ['#labelChild', 'EXTERNAL'],
  ])
    assert.equal(await page.locator(selector).inputValue(), expected)
  assert.equal(await active(), 'other')
  assert.deepEqual(await page.evaluate('globalThis.actualReadEvents'), [])
  checks.push(
    'Direct input/textarea/select targets preserve their own native values under conflicting label associations',
  )

  for (const attribute of ['disabled', 'readonly', 'hidden']) {
    await page.setContent(
      `<!doctype html><label id=label for=actual>Label</label><input id=actual value=UNCHANGED ${attribute}><input id=other>`,
    )
    await page.locator('#other').focus()
    await installJournal()
    assert.equal(await page.locator('#label').inputValue(), 'UNCHANGED')
    assert.equal(await active(), 'other')
    assert.deepEqual(await page.evaluate('globalThis.actualReadEvents'), [])
  }
  await page.setContent(
    '<!doctype html><div id=wrong>Not a control</div><input class=duplicate><input class=duplicate>',
  )
  await assert.rejects(
    page.locator('#wrong').inputValue(),
    /Node is not an <input>, <textarea> or <select> element/u,
  )
  await assert.rejects(page.locator('.duplicate').inputValue(), /Strict|strict/u)
  page.setDefaultTimeout(70)
  await assert.rejects(
    page.locator('#missing').inputValue(),
    (error) => error instanceof NativeTimeoutError,
  )
  page.setDefaultTimeout(2000)
  checks.push(
    'Value inspection permits genuine disabled/readonly/hidden controls, while strict selection, noncontrols and missing-node original deadlines remain enforced',
  )

  await page.setContent('<!doctype html><input id=actual value=PINNED><input id=other>')
  const pinned = await page.$('#actual')
  assert(pinned)
  await page.evaluate(
    "document.querySelector('#actual').id='renamed';const replacement=document.createElement('input');replacement.id='actual';replacement.value='REPLACEMENT';document.body.append(replacement)",
  )
  assert.equal(await pinned.inputValue(), 'PINNED')
  assert.equal(await page.locator('#actual').inputValue(), 'REPLACEMENT')
  await pinned.evaluate((element) => {
    if (!(element instanceof HTMLInputElement)) throw Error('Pinned input is missing')
    element.remove()
  })
  assert.equal(await pinned.inputValue(), 'PINNED')
  await pinned.dispose()
  await assert.rejects(pinned.inputValue(), /disposed/u)
  checks.push(
    'ElementHandle.inputValue reads the actual retained node across rename/replacement/detachment; disposed handles reject without acquiring replacements',
  )

  await page.setContent(
    "<!doctype html><div id=host></div><input id=outside><iframe id=child srcdoc=\"<!doctype html><label for=actual id=label>Child</label><input id=actual value=CHILD><input id=other><script>document.querySelector('#other').focus();document.querySelector('#actual').dataset.ready='true'</script>\"></iframe>",
  )
  await page.evaluate(
    "document.querySelector('#host').attachShadow({mode:'open'}).innerHTML='<label id=shadowLabel for=shadowInput>Shadow</label><input id=shadowInput value=SHADOW>'",
  )
  const frame = page.frameLocator('#child')
  await frame.locator('input[data-ready=true]').waitFor({ state: 'attached' })
  await page.locator('#outside').focus()
  await installJournal()
  const childFocusBeforeRead = await frame
      .locator('#actual')
      .evaluate((element) => element.ownerDocument.activeElement?.id),
    rootFocusBeforeRead = z.string().parse(await active())
  assert.equal(rootFocusBeforeRead, 'outside')
  assert.equal(await page.locator('#shadowLabel').inputValue(), 'SHADOW')
  assert.equal(await frame.locator('#label').inputValue(), 'CHILD')
  const childFocusAfterRead = await frame
      .locator('#actual')
      .evaluate((element) => element.ownerDocument.activeElement?.id),
    rootFocusAfterRead = z.string().parse(await active())
  assert.equal(childFocusAfterRead, childFocusBeforeRead)
  assert.equal(rootFocusAfterRead, rootFocusBeforeRead)
  const frameFocusObservation = {
    childFocusBeforeRead,
    childFocusAfterRead,
    rootFocusBeforeRead,
    rootFocusAfterRead,
  }
  assert.deepEqual(await page.evaluate('globalThis.actualReadEvents'), [])
  checks.push(
    'Real shadow and lazy frame label values are read in their owning utility document while all existing focus owners remain unchanged',
  )

  await page.setContent(
    '<!doctype html><label id=label for=actual>Native value</label><input id=actual value=REAL><input id=other>',
  )
  await page.locator('#other').focus()
  await installJournal()
  await page.evaluate(
    "globalThis.valueFactoryMain=true;globalThis.originalValue=Object.getOwnPropertyDescriptor(HTMLInputElement.prototype,'value');globalThis.authorValueCalls=0;Object.defineProperty(HTMLInputElement.prototype,'value',{configurable:true,get(){globalThis.authorValueCalls++;throw Error('Author value getter must not run')},set:globalThis.originalValue.set})",
  )
  const selectors = new NativeSelectors(source)
  await selectors.register('actualValueMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'valueFactoryMain') !== true)
        throw Error('Expected the genuine main factory realm')
      return root.querySelectorAll(selector)
    },
  }))
  for (const selector of ['#actual', '#label', 'actualValueMain=#label'])
    assert.equal(await page.locator(selector).inputValue(), 'REAL')
  assert.equal(await page.evaluate('globalThis.authorValueCalls'), 0)
  assert.equal(await active(), 'other')
  assert.deepEqual(await page.evaluate('globalThis.actualReadEvents'), [])
  await page.evaluate(
    "Object.defineProperty(HTMLInputElement.prototype,'value',globalThis.originalValue)",
  )
  checks.push(
    'Native utility getters read real browser values despite main-world author prototype replacement, including a genuine main selector factory node adoption',
  )

  await page.setContent('<!doctype html><input id=actual>')
  const expected = 'A\ud800B\udfff🙂'
  await page.locator('#actual').evaluate((element, value) => {
    if (!(element instanceof HTMLInputElement)) throw Error('UTF16 input is missing')
    element.value = value
  }, expected)
  assert.equal(await page.locator('#actual').inputValue(), expected)
  const tagText = '{"__suitecutNativeUTF16Scalar":"d800"}'
  await page.locator('#actual').evaluate((element, value) => {
    if (!(element instanceof HTMLInputElement)) throw Error('Literal input is missing')
    element.value = value
  }, tagText)
  assert.equal(await page.locator('#actual').inputValue(), tagText)
  await page.locator('#actual').evaluate((element) => {
    if (!(element instanceof HTMLInputElement)) throw Error('Large input is missing')
    element.value = 'x'.repeat(1048576)
  })
  const large = await page.locator('#actual').inputValue()
  assert.equal(large, 'x'.repeat(1048576))
  checks.push(
    'Actual isolated UTF16 units survive the typed primitive response; tag-shaped text remains literal and one MiB of real control value is returned intact',
  )

  const held = await page.$('#actual')
  assert(held)
  /**
   * Receives the actual renderer binding call before the busy loop.
   * @type {()=>void}
   */
  let markStarted = () => {
    throw Error('The real renderer hold callback was not registered')
  }
  const started = new Promise((resolve) => {
    markStarted = () => resolve(undefined)
  })
  const bridge = await page.exposeFunction('actualValueHoldStarted', () => markStarted())
  const busy = page.evaluate(
    '(()=>{globalThis.actualValueHoldStarted();const until=performance.now()+650;while(performance.now()<until){};return 42;})()',
  )
  await started
  const controller = new AbortController(),
    reason = new Error('Original inputValue cancellation with actual renderer ACK held')
  const pending = held.inputValue({ signal: controller.signal, timeout: 0 }),
    rejected = assert.rejects(pending, (error) => error === reason)
  const during = await waitState((state) => state.nativeSelectorWaitPhysical > 0)
  controller.abort(reason)
  await rejected
  const afterAbort = ownership.parse(await source.runAutomation('diagnostics'))
  assert(afterAbort.nativeSelectorWaits > 0)
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  assert.equal(await busy, 42)
  await bridge.dispose()
  await held.dispose()
  const finalOwnership = await waitState((state) =>
    Object.values(state).every((value) => value === 0),
  )
  checks.push(
    'Original cancellation returns while native fixed-node read/adoption retains actual physical ACK ownership, then all groups, handles and native commands settle to zero',
  )
  assert.equal(checks.length, 8)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        checks,
        during,
        afterAbort,
        frameFocusObservation,
        largeValueUnits: large.length,
        finalOwnership,
      },
      null,
      2,
    ) + '\n',
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
  'Native inputValue8 passed with real readonly control values, labels, pinned/frame/shadow identity, UTF16, native deadlines and physical cleanup',
)
