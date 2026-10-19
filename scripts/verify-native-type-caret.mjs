// SPDX-License-Identifier: MIT
/* global HTMLInputElement, HTMLTextAreaElement */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeTimeoutError } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-type-caret',
)
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath,
  width: 400,
  height: 300,
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
  inputWork: z.number(),
  pressedKeys: z.number(),
})
const inputEvents = z.array(z.object({ id: z.string(), trusted: z.boolean() }))
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const behaviorErrors = []
const journal = () =>
  page.evaluate(
    "globalThis.actualTypeEvents=[];document.addEventListener('input',event=>globalThis.actualTypeEvents.push({id:event.target.id,trusted:event.isTrusted}),true)",
  )
async function settled() {
  const deadline = performance.now() + 3000
  while (true) {
    const state = ownership.parse(await source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) return state
    assert(performance.now() < deadline, 'Actual native type ownership did not settle')
  }
}
try {
  for (const tag of ['input', 'textarea']) {
    const markup =
      tag === 'input' ? '<input id=target value=hello>' : '<textarea id=target>hello</textarea>'
    for (const focused of [false, true]) {
      await page.setContent(`<!doctype html>${markup}<button id=other>Other</button>`)
      await journal()
      await page.locator('#target').evaluate((element, focused) => {
        if (!(element instanceof HTMLInputElement || element instanceof HTMLTextAreaElement))
          throw Error('Actual native text control is missing')
        if (focused) element.focus()
        element.setSelectionRange(2, 4)
        if (!focused) {
          const other = element.ownerDocument.querySelector('button')
          if (!other) throw Error('Actual focus peer is missing')
          other.focus()
        }
      }, focused)
      await page.type('#target', 'world')
      assert.equal(await page.locator('#target').inputValue(), focused ? 'heworldo' : 'worldhello')
      const events = inputEvents.parse(await page.evaluate('globalThis.actualTypeEvents'))
      assert.equal(events.length, 5)
      assert(events.every((event) => event.id === 'target' && event.trusted))
    }
  }
  checks.push(
    'Real input and textarea type reset stale selection only when previously unfocused; focused selection is preserved and each character produces genuine trusted input',
  )

  await page.setContent('<!doctype html><input id=target value=hello>')
  await page.locator('#target').evaluate((element) => {
    if (!(element instanceof HTMLInputElement)) throw Error('Input missing')
    element.focus()
    element.setSelectionRange(2, 4)
  })
  await page.keyboard.type('raw')
  assert.equal(await page.locator('#target').inputValue(), 'herawo')
  await page.locator('#target').press('ArrowRight')
  await page.locator('#target').press('X')
  assert.equal(await page.locator('#target').inputValue(), 'herawoX')
  await page.setContent('<!doctype html><input id=target type=number value=2>')
  await page.type('#target', '13')
  assert.equal(await page.locator('#target').inputValue(), '132')
  checks.push(
    'Raw keyboard typing, locator press and existing unsupported-selection number-control caret behavior retain their native contracts',
  )

  await page.setContent('<!doctype html><div id=host></div><button id=other>Other</button>')
  await page.evaluate(
    "document.querySelector('#host').attachShadow({mode:'open'}).innerHTML='<input id=target value=hello>'",
  )
  const shadow = page.locator('#target')
  await shadow.evaluate((element) => {
    if (!(element instanceof HTMLInputElement)) throw Error('Shadow input missing')
    element.focus()
    element.setSelectionRange(2, 4)
  })
  await shadow.type('world')
  assert.equal(await shadow.inputValue(), 'heworldo')
  await shadow.evaluate((element) => {
    if (!(element instanceof HTMLInputElement)) throw Error('Shadow input missing')
    element.value = 'hello'
    element.setSelectionRange(2, 4)
    const other = element.ownerDocument.querySelector('button')
    if (!other) throw Error('Focus peer missing')
    other.focus()
  })
  await shadow.pressSequentially('world')
  assert.equal(await shadow.inputValue(), 'worldhello')
  checks.push(
    'Actual shadow-root focus is inspected rather than the document host; pressSequentially uses the same genuine native type preparation',
  )

  await page.setContent(
    '<!doctype html><input id=target value=hello><button id=other>Other</button>',
  )
  const pinned = await page.$('#target')
  assert(pinned)
  await page.evaluate(
    "const input=document.querySelector('#target');input.id='renamed';input.setSelectionRange(2,4);document.querySelector('button').focus();const replacement=document.createElement('input');replacement.id='target';replacement.value='REPLACEMENT';document.body.append(replacement)",
  )
  await pinned.type('world', { timeout: 1000 })
  assert.equal(await page.locator('#renamed').inputValue(), 'worldhello')
  assert.equal(await page.locator('#target').inputValue(), 'REPLACEMENT')
  await pinned.evaluate((element) => {
    if (!(element instanceof HTMLInputElement)) throw Error('Pinned input missing')
    element.remove()
  })
  await assert.rejects(
    pinned.type('undelivered', { timeout: 1000 }),
    /not attached|detached|unavailable/iu,
  )
  assert.equal(await page.locator('#target').inputValue(), 'REPLACEMENT')
  await pinned.dispose()
  checks.push(
    'ElementHandle.type is one pinned native operation, retaining old-node identity, initial focus facts and the original deadline without a separate host focus call',
  )

  await page.setContent(
    "<!doctype html><input id=target value=ROOT><iframe id=child srcdoc=\"<!doctype html><input id=target value=hello><button>Other</button><script>const input=document.querySelector('input');input.setSelectionRange(2,4);document.querySelector('button').focus();input.dataset.ready='true'</script>\"></iframe>",
  )
  const child = page.frameLocator('#child').locator('input[data-ready=true]')
  await child.type('world')
  assert.equal(await child.inputValue(), 'worldhello')
  assert.equal(await page.locator('#target').inputValue(), 'ROOT')
  checks.push(
    'Lazy child-frame typing resets the actual child control selection and leaves the root input unchanged',
  )

  await page.setContent(
    '<!doctype html><input id=target value=hello><button id=other>Other</button>',
  )
  const timed = await page.$('#target')
  assert(timed)
  await timed.evaluate((element) => {
    if (!(element instanceof HTMLInputElement)) throw Error('Timed input missing')
    element.setSelectionRange(2, 4)
    const other = element.ownerDocument.querySelector('button')
    if (!other) throw Error('Focus peer missing')
    other.focus()
  })
  page.setDefaultTimeout(70)
  await assert.rejects(
    timed.type('abcdef', { delay: 300 }),
    (error) => error instanceof NativeTimeoutError,
  )
  page.setDefaultTimeout(2000)
  const timedValue = await page.locator('#target').inputValue()
  assert(
    ['hello', 'ahello'].includes(timedValue),
    'A native deadline must prevent characters beyond the first held-delay interval',
  )
  await timed.dispose()
  const finalOwnership = await settled()
  checks.push(
    'The current native default deadline covers pinned focus preparation and delayed typing together; timeout stops later characters and releases all actual keys and ownership',
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: true, binarySHA256, checks, timedValue, finalOwnership }, null, 2) +
      '\n',
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
  'Native type caret6 passed with actual focused/unfocused input, pinned/frame/shadow identity, trusted events and original deadline cleanup',
)
