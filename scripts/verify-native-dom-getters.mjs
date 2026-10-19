// SPDX-License-Identifier: MIT
/* global document, Element, HTMLInputElement, HTMLElement, Node, setTimeout */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import {
  createNativePage,
  launchNativeBrowser,
  NativeBrowserContexts,
  NativeSelectors,
  expectNative,
} from '../dist/native.js'

const directory = '.suitecut/native-dom-getters'
await mkdir(directory, { recursive: true })
let stderr = ''
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html;charset=utf-8')
  response.end('<p id="remote" data-original="ACTUAL-CHILD">Actual child</p>')
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address === 'object')
const base = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (message) => {
    stderr += message
    assert(stderr.length < 1048576)
  },
})
const stateSchema = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeDOMGetters: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  nativeHandleDisposals: z.number(),
  nativeHandleDisposalPhysical: z.number(),
  bindingCalls: z.number(),
  pendingBindingCommands: z.number(),
})
const checks = [],
  rows = []
try {
  const context = await new NativeBrowserContexts(source).newContext(),
    page = await context.newPage()
  await page.goto(base)
  await page.setContent(
    '<input id="disabled" disabled><input id="readonly" readonly><input id="checked" type="checkbox" checked><p id="original" data-original="ACTUAL">Actual text</p>',
  )
  const exact = 'Actual\ud800\0\\"\udfff😀',
    literal = '{"__suitecutNativeUTF16Scalar":"d800"}'
  const actual = z
    .object({
      attribute: z.string(),
      literal: z.string(),
      text: z.string(),
      innerText: z.string(),
      html: z.string(),
      texts: z.array(z.string()),
      innerTexts: z.array(z.string()),
    })
    .parse(
      await page.evaluate(
        ({ exact, literal }) => {
          const element = document.querySelector('#original')
          if (!(element instanceof HTMLElement)) throw new Error('Actual HTML fixture missing')
          element.setAttribute('data-original', exact)
          element.setAttribute('data-literal', literal)
          element.textContent = exact
          return {
            attribute: element.getAttribute('data-original'),
            literal: element.getAttribute('data-literal'),
            text: element.textContent,
            innerText: element.innerText,
            html: element.innerHTML,
            texts: Array.from(document.querySelectorAll('p'), (node) => node.textContent ?? ''),
            innerTexts: Array.from(document.querySelectorAll('p'), (node) => node.innerText),
          }
        },
        { exact, literal },
      ),
    )
  const original = await page.$('#original')
  assert(original)
  await page.evaluate(() => {
    /** @type {[object,string][]} */
    const targets = [
      [Element.prototype, 'matches'],
      [Element.prototype, 'getAttribute'],
      [Node.prototype, 'textContent'],
      [Element.prototype, 'innerHTML'],
      [HTMLElement.prototype, 'innerText'],
      [HTMLInputElement.prototype, 'readOnly'],
      [HTMLInputElement.prototype, 'checked'],
    ]
    const descriptors = targets.map(([prototype, key]) => ({
      prototype,
      key,
      descriptor: Object.getOwnPropertyDescriptor(prototype, key),
    }))
    Reflect.set(globalThis, 'actualDOMGetterAuthorMarker', true)
    Reflect.set(globalThis, 'actualDOMGetterAuthorCalls', 0)
    Reflect.set(globalThis, 'restoreActualDOMGetters', () => {
      for (const entry of descriptors) {
        if (!entry.descriptor) throw new Error('Actual original descriptor missing')
        Object.defineProperty(entry.prototype, entry.key, entry.descriptor)
      }
    })
    const called = () =>
      Reflect.set(
        globalThis,
        'actualDOMGetterAuthorCalls',
        Number(Reflect.get(globalThis, 'actualDOMGetterAuthorCalls')) + 1,
      )
    Element.prototype.matches = () => {
      called()
      return false
    }
    Element.prototype.getAttribute = () => {
      called()
      return 'FORGED-ATTRIBUTE'
    }
    /** @type {[object,string,string|boolean][]} */
    const replacements = [
      [Node.prototype, 'textContent', 'FORGED-TEXT'],
      [Element.prototype, 'innerHTML', 'FORGED-HTML'],
      [HTMLElement.prototype, 'innerText', 'FORGED-INNER-TEXT'],
      [HTMLInputElement.prototype, 'readOnly', false],
      [HTMLInputElement.prototype, 'checked', false],
    ]
    for (const [prototype, key, value] of replacements)
      Object.defineProperty(prototype, key, {
        configurable: true,
        get: () => {
          called()
          return value
        },
      })
  })
  assert.equal(await page.locator('#disabled').isEnabled(), false)
  assert.equal(await page.isDisabled('#disabled'), true)
  assert.equal(await page.locator('#readonly').isEditable(), false)
  assert.equal(await page.isEditable('#readonly'), false)
  assert.equal(await page.locator('#checked').isChecked(), true)
  checks.push(
    'actual native disabled, readonly and checked states survive real author prototype replacement and Page conveniences share native state',
  )
  for (const target of [original, page.locator('#original')]) {
    assert.equal(await target.getAttribute('data-original'), actual.attribute)
    assert.equal(await target.getAttribute('data-literal'), actual.literal)
    assert.equal(await target.textContent(), actual.text)
    assert.equal(await target.innerText(), actual.innerText)
    assert.equal(await target.innerHTML(), actual.html)
  }
  assert.equal(await page.getAttribute('#original', 'data-original'), actual.attribute)
  assert.equal(await page.textContent('#original'), actual.text)
  assert.equal(await page.innerHTML('#original'), actual.html)
  assert.equal(await page.innerText('#original'), actual.innerText)
  checks.push(
    'actual utility wrappers preserve original native DOM attribute/text/HTML values, isolated units and opaque tag-looking strings for locator, Page and fixed handles',
  )
  assert.deepEqual(await page.locator('p').allTextContents(), actual.texts)
  assert.deepEqual(await page.locator('p').allInnerTexts(), actual.innerTexts)
  assert.equal(
    await page.evaluate(() => Number(Reflect.get(globalThis, 'actualDOMGetterAuthorCalls'))),
    0,
  )
  checks.push(
    'aggregate native getters use actual ordered utility nodes without entering any forged author DOM getter',
  )
  const selectors = new NativeSelectors(source)
  await selectors.register('actualDOMGetterMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'actualDOMGetterAuthorMarker') !== true)
        throw new Error('Selector did not run in actual authored realm')
      return root.querySelectorAll(selector)
    },
  }))
  assert.equal(
    await page.locator('actualDOMGetterMain=#original').getAttribute('data-original'),
    actual.attribute,
  )
  assert.equal(await page.locator('actualDOMGetterMain=#disabled').isEnabled(), false)
  assert.equal(
    await page.evaluate(() => Number(Reflect.get(globalThis, 'actualDOMGetterAuthorCalls'))),
    0,
  )
  checks.push(
    'declared main-world custom selection retains its genuine author environment while native primitive/state observation adopts actual backend nodes into utility',
  )
  assert.equal(
    await page.locator('#original').evaluate((element) => element.textContent),
    'FORGED-TEXT',
  )
  assert.deepEqual(
    await page
      .locator('p')
      .evaluateAll((elements) => elements.map((element) => element.textContent)),
    ['FORGED-TEXT'],
  )
  assert.equal(
    await page.evaluate(() => document.querySelector('#original')?.getAttribute('data-original')),
    'FORGED-ATTRIBUTE',
  )
  await page.evaluate('globalThis.restoreActualDOMGetters()')
  await original.dispose()
  checks.push(
    'public author evaluate and evaluateAll continue to observe actual author replacements; native getters never rewrite or restore author state themselves',
  )
  await page.setContent(
    '<label id="label" data-original="LABEL">Actual label<input id="control" disabled readonly style="width:0;height:0;padding:0;margin:0;border:0"></label><fieldset disabled><button><span id="span" data-original="SPAN">Actual span</span></button></fieldset>',
  )
  const label = await page.$('#label'),
    span = await page.$('#span')
  assert(label && span)
  assert.equal(await label.isEnabled(), false)
  assert.equal(await label.isEditable(), false)
  assert.equal(await page.locator('#label').isEnabled(), false)
  assert.equal(await page.locator('#label').isEditable(), false)
  assert.equal(await span.isEnabled(), false)
  assert.equal(await page.locator('#span').isEnabled(), false)
  assert.equal(await label.isVisible(), true)
  assert.equal(await page.locator('#control').isVisible(), false)
  assert.equal(await label.getAttribute('data-original'), 'LABEL')
  assert.equal(await span.getAttribute('data-original'), 'SPAN')
  assert.equal(await label.textContent(), 'Actual label')
  assert.equal(await span.textContent(), 'Actual span')
  await expectNative(page.locator('#label')).toBeDisabled()
  await expectNative(page.locator('#label')).not.toBeEditable()
  await expectNative(page.locator('#span')).toBeDisabled()
  await page.evaluate(() => {
    const input = document.querySelector('#control')
    if (!(input instanceof HTMLInputElement)) throw new Error('Actual control missing')
    input.disabled = false
    input.readOnly = false
    const fieldset = document.querySelector('fieldset')
    if (!fieldset) throw new Error('Actual fieldset missing')
    fieldset.disabled = false
  })
  await span.waitForElementState('enabled', { timeout: 1000 })
  await label.waitForElementState('editable', { timeout: 1000 })
  await expectNative(page.locator('#label')).toBeEditable()
  await label.dispose()
  await span.dispose()
  checks.push(
    'enabled/editable state and assertions follow genuine label.control/enclosing-button relationships while visibility and direct getters keep original selected identity',
  )
  await page.setContent(
    '<select><option id="option" disabled>Actual option</option><optgroup id="group" disabled label="Actual group"><option>Child option</option></optgroup></select><fieldset id="fieldset" disabled></fieldset><label for="anchor-input"><a id="anchor" href="#actual">Actual anchor</a></label><input id="anchor-input" disabled>',
  )
  assert.equal(await page.locator('#option').isEnabled(), false)
  assert.equal(await page.locator('#group').isEnabled(), false)
  assert.equal(await page.locator('#fieldset').isEnabled(), false)
  assert.equal(await page.locator('#anchor').isEnabled(), true)
  assert.equal(await page.locator('#option').getAttribute('id'), 'option')
  assert.equal(await page.locator('#anchor').getAttribute('id'), 'anchor')
  checks.push(
    'selected native option/optgroup/fieldset and actual interactive anchor retain their own genuine enabled state instead of jumping to enclosing select or label',
  )
  await page.setContent('<div id="detached" data-original="ACTUAL-DETACHED">Actual detached</div>')
  const detached = await page.$('#detached')
  assert(detached)
  await page.evaluate(() => {
    document.querySelector('#detached')?.remove()
    document.body.insertAdjacentHTML(
      'beforeend',
      '<div id="detached" data-original="REPLACEMENT">Replacement</div>',
    )
  })
  assert.equal(await detached.getAttribute('data-original'), 'ACTUAL-DETACHED')
  assert.equal(await detached.textContent(), 'Actual detached')
  assert.equal(await page.locator('#detached').textContent(), 'Replacement')
  await detached.dispose()
  checks.push(
    'detached retained DOM getters read their genuine original node and never reacquire a same-selector replacement',
  )
  await page.setContent('<div id="host"></div><svg id="vector">Actual vector</svg>')
  await page.evaluate(() => {
    const host = document.querySelector('#host')
    if (!host) throw new Error('Actual host missing')
    host.attachShadow({ mode: 'open' }).innerHTML = '<span>Actual shadow text</span>'
  })
  const shadow = await page.evaluateHandle(() => document.querySelector('#host')?.shadowRoot),
    root = shadow.asElement()
  assert(root)
  assert.equal(await root.textContent(), 'Actual shadow text')
  await shadow.dispose()
  assert.equal(await page.locator('#vector').textContent(), 'Actual vector')
  await assert.rejects(page.locator('#vector').innerText(), /Node is not an HTMLElement/u)
  checks.push(
    'actual retained ShadowRoot text and SVG text remain genuine, while unsupported rendered innerText rejects explicitly',
  )
  await page.setContent('<p id="later-container"></p>')
  await page.evaluate(() =>
    setTimeout(() => {
      document
        .querySelector('#later-container')
        ?.insertAdjacentHTML('beforeend', '<span data-later="ACTUAL-LATER">Actual later</span>')
    }, 60),
  )
  assert.equal(
    await page.locator('span').getAttribute('data-later', { timeout: 1500 }),
    'ACTUAL-LATER',
  )
  const began = performance.now()
  await assert.rejects(
    page.locator('#absent').innerText({ timeout: 45 }),
    /waiting for locator\('#absent'\)/u,
  )
  assert(performance.now() - began < 1200)
  checks.push(
    'native missing-node polling observes a real later insertion and rejects on the original getter deadline with its actual waiting selector',
  )
  await page.setContent('<iframe src="' + base + '/child"></iframe>')
  assert.equal(
    await page.frameLocator('iframe').locator('#remote').getAttribute('data-original'),
    'ACTUAL-CHILD',
  )
  const child = page.frames().find((frame) => frame.parentFrame())
  assert(child)
  await child.goto(`http://localhost:${address.port}/cross`)
  assert.equal(await child.locator('#remote').textContent(), 'Actual child')
  assert.equal(await child.getAttribute('#remote', 'data-original'), 'ACTUAL-CHILD')
  checks.push(
    'lazy frame paths and real OOPIF targets read actual native child DOM values through their owning contexts',
  )
  for (const fields of [
    { domGetter: 'invalid', dom: true },
    { domGetter: true, dom: false },
    { domGetter: true, dom: true, expectation: true },
    { domGetter: true, dom: true, count: true },
  ])
    await assert.rejects(
      page.runAutomation('queryEvaluate', { ...fields, function: 'function(){return true;}' }),
      /Invalid native DOM getter request/u,
    )
  checks.push(
    'malformed native getter flags cannot bypass native admission or repurpose expectation/count paths',
  )
  await page.setContent('<p id="held">Actual held document</p>')
  const abort = new AbortController(),
    projection = createNativePage(page.source, abort.signal),
    started = new AbortController(),
    start = new Promise((resolve) =>
      started.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    ),
    bridge = await page.exposeFunction('actualDOMGetterHoldStarted', () => started.abort())
  const busy = page.evaluate(
    '(()=>{globalThis.actualDOMGetterHoldStarted();const end=Date.now()+500;while(Date.now()<end){};})()',
  )
  await start
  const pending = assert.rejects(
    projection.locator('#held').textContent({ timeout: 3000 }),
    /Actual DOM getter cancellation/u,
  )
  let held
  for (let attempt = 0; attempt < 100; attempt++) {
    held = stateSchema.parse(await page.source.runAutomation('diagnostics'))
    if (held.nativeSelectorWaitPhysical > 0) break
    await delay(1)
  }
  assert(held && held.nativeSelectorWaitPhysical > 0)
  abort.abort(new Error('Actual DOM getter cancellation'))
  await pending
  const afterAbort = stateSchema.parse(await page.source.runAutomation('diagnostics'))
  assert.equal(afterAbort.nativeDOMGetters, 0)
  assert(afterAbort.nativeSelectorWaits > 0 && afterAbort.nativeSelectorWaitPhysical > 0)
  await busy
  await bridge.dispose()
  rows.push({ held, afterAbort })
  checks.push(
    'actual held renderer preparation remains physically owned after logical getter cancellation until real protocol ACK and group-release settlement',
  )
  for (let attempt = 0; attempt < 300; attempt++) {
    if (
      Object.values(stateSchema.parse(await page.source.runAutomation('diagnostics'))).every(
        (value) => value === 0,
      )
    )
      break
    await delay(5)
  }
  const state = stateSchema.parse(await page.source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  await context.close()
  checks.push(
    'all actual getter owners, retained handles, evaluation groups, physical selector preparations and binding commands settle to zero',
  )
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ checks, actual, rows, state }, null, 2) + '\n',
  )
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await new Promise((resolve) => server.close(() => resolve(undefined)))
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '', 'Native DOM getter diagnostics must remain empty')
