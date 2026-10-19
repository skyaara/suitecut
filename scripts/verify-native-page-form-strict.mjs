// SPDX-License-Identifier: MIT
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setImmediate } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeTimeoutError } from '../dist/native.js'

import { captureNativeStderr, finishNativeProof } from './native-stderr-proof.mjs'

const directory = resolve(
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-page-form-strict',
)
await mkdir(directory, { recursive: true })
const file = resolve(directory, 'genuine.txt')
await writeFile(file, 'Actual native file bytes')
const diagnostics = await captureNativeStderr(directory)
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  width: 640,
  height: 360,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(2000)
/** @type {unknown[]} */
const errors = []
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const observations = []
const events = `<script>globalThis.events=[];for(const type of ['click','input','change'])document.addEventListener(type,event=>events.push({type,id:event.target.id,trusted:event.isTrusted}),true)</script>`
const boxes = (checked = false) =>
  page.setContent(
    `<input id=one type=checkbox ${checked ? 'checked' : ''}><input id=two type=checkbox ${checked ? 'checked' : ''}>${events}`,
  )
const checked = async () =>
  z
    .array(z.boolean())
    .parse(await page.evaluate(`Array.from(document.querySelectorAll('input'),node=>node.checked)`))
const selection = `<select id=one><option value=a>A</option><option value=b>B</option></select><select id=two><option value=a>A</option><option value=b>B</option></select>`
const selected = async () =>
  z
    .array(z.string())
    .parse(await page.evaluate(`Array.from(document.querySelectorAll('select'),node=>node.value)`))
const ownership = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  formClicks: z.number(),
  inputWork: z.number(),
  pendingDevTools: z.number(),
  pendingUploadGroups: z.number(),
  mouseButtons: z.number(),
})
async function settledOwnership() {
  const deadline = performance.now() + 4000
  for (;;) {
    const actual = ownership.parse(await page.runAutomation('diagnostics'))
    if (Object.values(actual).every((value) => value === 0)) return actual
    assert(performance.now() < deadline, 'Actual form ownership did not settle after real ACKs')
    await setImmediate()
  }
}
try {
  for (const options of [{}, { strict: false }]) {
    await boxes()
    await page.check('input', options)
    assert.deepEqual(await checked(), [true, false])
    await boxes(true)
    await page.uncheck('input', options)
    assert.deepEqual(await checked(), [false, true])
    await boxes()
    await page.setChecked('input', true, options)
    assert.deepEqual(await checked(), [true, false])
    const real = z
      .array(z.object({ id: z.string(), trusted: z.boolean() }))
      .parse(await page.evaluate('events'))
    assert(real.some((event) => event.trusted))
    assert(real.every((event) => event.id === 'one' && event.trusted))
    observations.push(real)
  }
  for (const action of ['check', 'uncheck', 'setChecked']) {
    await boxes(action === 'uncheck')
    if (action === 'check')
      await assert.rejects(page.check('input', { strict: true }), /[Ss]trict/u)
    if (action === 'uncheck')
      await assert.rejects(page.uncheck('input', { strict: true }), /[Ss]trict/u)
    if (action === 'setChecked')
      await assert.rejects(page.setChecked('input', true, { strict: true }), /[Ss]trict/u)
    assert.deepEqual(await checked(), action === 'uncheck' ? [true, true] : [false, false])
    assert.deepEqual(await page.evaluate('events'), [])
  }
  checks.push(
    'Page checked actions select only the first real match; strict mode rejects before native pointer mutation',
  )

  await page.setContent(selection)
  assert.deepEqual(await page.selectOption('select', 'b'), ['b'])
  assert.deepEqual(await selected(), ['b', 'a'])
  await assert.rejects(page.selectOption('select', 'a', { strict: true }), /[Ss]trict/u)
  assert.deepEqual(await selected(), ['b', 'a'])
  checks.push(
    'Page selectOption first-match and strict policy are enforced by the actual native selector',
  )

  await page.setContent(`<input id=one type=file><input id=two type=file>${events}`)
  await page.setInputFiles('input', file)
  assert.deepEqual(
    await page.evaluate(
      `Promise.all(Array.from(document.querySelectorAll('input'),async node=>({count:node.files.length,text:node.files[0]?await node.files[0].text():null})))`,
    ),
    [
      { count: 1, text: 'Actual native file bytes' },
      { count: 0, text: null },
    ],
  )
  await assert.rejects(page.setInputFiles('input', file, { strict: true }), /[Ss]trict/u)
  const fileEvents = z
    .array(z.object({ type: z.string(), id: z.string(), trusted: z.boolean() }))
    .parse(await page.evaluate('events'))
  assert.deepEqual(fileEvents, [
    { type: 'input', id: 'one', trusted: true },
    { type: 'change', id: 'one', trusted: true },
  ])
  observations.push(fileEvents)
  checks.push(
    'Page nonempty upload targets the first actual input, preserving real File bytes and trusted native events',
  )

  await boxes()
  await assert.rejects(page.locator('input').check(), /[Ss]trict/u)
  const pinned = await page.$('#two')
  assert(pinned)
  try {
    await pinned.check()
    assert.deepEqual(await checked(), [false, true])
    await page.evaluate(`document.querySelector('#two').outerHTML='<input id=two type=checkbox>'`)
    await assert.rejects(
      pinned.check({ timeout: 100 }),
      /attached|detached|context|handle|timed out/iu,
    )
    assert.deepEqual(await checked(), [false, false])
  } finally {
    await pinned.dispose()
  }
  checks.push(
    'Locator strictness and retained-handle identity remain intact after a matching replacement appears',
  )

  await page.setContent(
    `<input id=outside type=checkbox><iframe name=child srcdoc="<input type=checkbox><input type=checkbox><select><option value=a>A</option><option value=b>B</option></select><select><option value=a>A</option><option value=b>B</option></select><input type=file><input type=file>"></iframe>`,
  )
  await page.frameLocator('iframe').locator('select').first().waitFor({ state: 'attached' })
  const frame = page.frame('child')
  assert(frame)
  await frame.check('input[type=checkbox]')
  await frame.selectOption('select', 'b')
  await frame.setInputFiles('input[type=file]', file)
  assert.deepEqual(
    await frame.evaluate(
      `({checks:Array.from(document.querySelectorAll('[type=checkbox]'),node=>node.checked),selected:Array.from(document.querySelectorAll('select'),node=>node.value),files:Array.from(document.querySelectorAll('[type=file]'),node=>node.files.length)})`,
    ),
    { checks: [true, false], selected: ['b', 'a'], files: [1, 0] },
  )
  await assert.rejects(frame.check('input[type=checkbox]', { strict: true }), /[Ss]trict/u)
  assert.equal(
    await page.$eval(
      '#outside',
      (element) => /** @type {unknown} */ (Reflect.get(element, 'checked')),
    ),
    false,
  )
  checks.push(
    'Frame form methods retain genuine child ownership and leave the root outside control untouched',
  )

  await page.setContent(
    `<label for=one>One</label><input id=one type=checkbox><label for=two>Two</label><input id=two type=checkbox>`,
  )
  await page.check('label')
  assert.deepEqual(await checked(), [true, false])
  await assert.rejects(page.check('label', { strict: true }), /[Ss]trict/u)
  assert.deepEqual(await checked(), [true, false])
  checks.push(
    'First-match policy preserves genuine label.control association without changing either selected label identity or other controls',
  )

  await page.setContent(`<input id=one type=checkbox hidden><input id=two type=checkbox>${events}`)
  await assert.rejects(
    page.check('input', { timeout: 80 }),
    (error) => error instanceof NativeTimeoutError,
  )
  assert.deepEqual(await checked(), [false, false])
  assert.deepEqual(await page.evaluate('events'), [])
  const state = await settledOwnership()
  checks.push(
    'Native original deadline does not fall through to a later actionable match; all form/handle/input ownership settles by actual ACK',
  )
  assert.equal(checks.length, 7)
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify({ completed: true, checks, observations, state }, null, 2) + '\n',
  )
} catch (error) {
  errors.push(error)
  await writeFile(
    resolve(directory, 'failure.json'),
    JSON.stringify({ completed: false, checks, observations, error: String(error) }, null, 2) +
      '\n',
  )
  throw error
} finally {
  await finishNativeProof(source, diagnostics, directory, errors)
}
console.info('Native Page form first-match7 passed')
