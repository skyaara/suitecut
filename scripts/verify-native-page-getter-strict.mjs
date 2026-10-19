// SPDX-License-Identifier: MIT
/* global document, HTMLElement, HTMLInputElement, Element */
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
} from '../dist/native.js'

const directory = '.suitecut/native-page-getter-strict'
await mkdir(directory, { recursive: true })
let stderr = ''
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html;charset=utf-8')
  response.end(
    '<span data-value="FIRST">First child</span><span data-value="SECOND">Second child</span>',
  )
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address === 'object')
const base = `http://127.0.0.1:${address.port}`
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (line) => {
    stderr += line
    assert(stderr.length < 1048576)
  },
})
const checks = [],
  rows = []
const schema = z.object({
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
try {
  const context = await new NativeBrowserContexts(source).newContext(),
    page = await context.newPage()
  await page.goto(base)
  const exact = 'Actual\ud800\0\\"\udfff😀',
    literal = '{"__suitecutNativeUTF16Scalar":"d800"}'
  await page.setContent(
    '<span id="first"></span><span id="second">Second</span><input class="values"><input class="values" value="Second">',
  )
  const actual = z
    .object({ text: z.string(), html: z.string(), attribute: z.string(), literal: z.string() })
    .parse(
      await page.evaluate(
        ({ exact, literal }) => {
          const node = document.querySelector('#first'),
            input = document.querySelector('input')
          if (!(node instanceof HTMLElement) || !(input instanceof HTMLInputElement))
            throw new Error('Actual fixture missing')
          node.textContent = exact
          node.setAttribute('data-value', exact)
          node.setAttribute('data-literal', literal)
          input.value = exact
          return {
            text: node.textContent,
            html: node.innerHTML,
            attribute: node.getAttribute('data-value'),
            literal: node.getAttribute('data-literal'),
          }
        },
        { exact, literal },
      ),
    )
  assert.equal(await page.textContent('span'), actual.text)
  assert.equal(await page.innerText('span'), actual.text)
  assert.equal(await page.innerHTML('span'), actual.html)
  assert.equal(await page.getAttribute('span', 'data-value'), actual.attribute)
  assert.equal(await page.getAttribute('span', 'data-literal'), actual.literal)
  assert.equal(await page.inputValue('input'), exact)
  checks.push(
    'Page primitive defaults select the real first matching DOM node and preserve exact isolated-unit strings and opaque tag-looking string values',
  )
  for (const call of [
    () => page.textContent('span', { strict: true }),
    () => page.innerHTML('span', { strict: true }),
    () => page.innerText('span', { strict: true }),
    () => page.getAttribute('span', 'id', { strict: true }),
  ])
    await assert.rejects(call(), /strict mode violation/u)
  await assert.rejects(page.inputValue('input', { strict: true }), /[Ss]trict.*multiple/u)
  await assert.rejects(page.$('span', { strict: true }), /[Ss]trict.*(multiple|violation)/u)
  const first = await page.$('span')
  assert(first)
  assert.equal(await first.getAttribute('id'), 'first')
  await first.dispose()
  checks.push(
    'explicit Page strictness rejects actual multiple matches natively, including existing input-value and retained-element query controllers',
  )
  await page.setContent(
    '<span class="visible" hidden>First hidden</span><span class="visible">Second visible</span><input class="enabled" disabled><input class="enabled"><input class="editable" readonly><input class="editable"><input class="checked" type="checkbox"><input class="checked" type="checkbox" checked>',
  )
  assert.equal(await page.isVisible('.visible'), false)
  assert.equal(await page.isHidden('.visible'), true)
  assert.equal(await page.isEnabled('.enabled'), false)
  assert.equal(await page.isDisabled('.enabled'), true)
  assert.equal(await page.isEditable('.editable'), false)
  assert.equal(await page.isChecked('.checked'), false)
  for (const call of [
    () => page.isVisible('.visible', { strict: true }),
    () => page.isEnabled('.enabled', { strict: true }),
    () => page.isEditable('.editable', { strict: true }),
    () => page.isChecked('.checked', { strict: true }),
  ])
    await assert.rejects(call(), /strict mode violation/u)
  checks.push(
    'Page visibility and enabled/editable/checked observations use genuine first-node state while strict mode observes actual ambiguity',
  )
  await assert.rejects(page.locator('.visible').isVisible(), /strict mode violation/u)
  await assert.rejects(page.locator('.enabled').isEnabled(), /strict mode violation/u)
  assert.equal(await page.locator('.visible').nth(1).isVisible(), true)
  assert.deepEqual(await page.locator('.visible').allTextContents(), [
    'First hidden',
    'Second visible',
  ])
  checks.push(
    'Locator scalar strictness and all-match aggregate ordering remain independent from Page first-match defaults',
  )
  const retained = await page.$('.visible')
  assert(retained)
  await retained.evaluate((node) => {
    if (!(node instanceof Element)) throw new Error('Actual retained element missing')
    node.remove()
  })
  await page.evaluate(() =>
    document.body.insertAdjacentHTML('afterbegin', '<span class="visible">Replacement</span>'),
  )
  assert.equal(await retained.textContent(), 'First hidden')
  assert.equal(await page.textContent('.visible'), 'Replacement')
  await retained.dispose()
  checks.push(
    'genuine retained element references never rematch a replacement even when Page query defaults select the new first node',
  )
  await page.setContent('<iframe src="' + base + '/child"></iframe>')
  assert.equal(
    await page.frameLocator('iframe').locator('span').first().textContent(),
    'First child',
  )
  const child = page.frames().find((frame) => frame.parentFrame())
  assert(child)
  await child.goto(base + '/child')
  assert.equal(await child.textContent('span'), 'First child')
  await assert.rejects(child.textContent('span', { strict: true }), /strict mode violation/u)
  await child.goto(`http://localhost:${address.port}/child`)
  assert.equal(await child.getAttribute('span', 'data-value'), 'FIRST')
  await assert.rejects(
    page.frameLocator('iframe').locator('span').textContent(),
    /strict mode violation/u,
  )
  checks.push(
    'actual same-origin and OOPIF Frame Page facades preserve native first/strict policy while lazy frame locators retain strict scalar semantics',
  )
  await page.setContent('<span>First</span><span>Second</span>')
  await page.evaluate(() => Reflect.set(globalThis, 'actualStrictSelectorAuthorMarker', true))
  await new NativeSelectors(source).register('actualPageStrictMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'actualStrictSelectorAuthorMarker') !== true)
        throw new Error('Wrong selector world')
      return root.querySelectorAll(selector)
    },
  }))
  assert.equal(await page.textContent('actualPageStrictMain=span'), 'First')
  await assert.rejects(
    page.textContent('actualPageStrictMain=span', { strict: true }),
    /strict mode violation/u,
  )
  const author = { __suitecutNativeUTF16Scalar: 'd800' }
  assert.deepEqual(
    await page.locator('span').evaluateAll(() => ({ __suitecutNativeUTF16Scalar: 'd800' })),
    author,
  )
  checks.push(
    'declared author-world custom engines retain their real selection realm and native utility observation, while public author objects remain opaque',
  )
  const began = performance.now()
  assert.equal(await page.isVisible('#absent'), false)
  assert.equal(await page.isHidden('#absent'), true)
  assert(performance.now() - began < 1000)
  await assert.rejects(page.textContent('#absent', { timeout: 45 }), /waiting for locator/u)
  for (const fields of [
    { strict: 'invalid' },
    { immediate: 'invalid' },
    { immediate: true, all: true },
  ])
    await assert.rejects(
      page.runAutomation('queryEvaluate', {
        domGetter: true,
        dom: true,
        query: [{ engine: 'css', selector: 'span' }],
        function: 'function(){return true;}',
        ...fields,
      }),
      /Invalid native DOM getter strictness or immediate mode/u,
    )
  const authored = z
    .object({ exceptionDetails: z.object({ exception: z.object({ description: z.string() }) }) })
    .parse(
      await page.runAutomation('queryEvaluate', {
        strict: false,
        query: [{ engine: 'css', selector: 'span' }],
        function: 'function(element){return element.textContent;}',
      }),
    )
  assert.match(authored.exceptionDetails.exception.description, /strict mode violation/u)
  checks.push(
    'missing-element visibility stays immediate, attachment-dependent reads keep their original deadline, and malformed raw flags cannot weaken public author strictness',
  )
  const abort = new AbortController(),
    projection = createNativePage(page.source, abort.signal),
    started = new AbortController(),
    ready = new Promise((resolve) =>
      started.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    )
  const bridge = await page.exposeFunction('actualPageStrictHoldStarted', () => started.abort())
  const busy = page.evaluate(
    '(()=>{globalThis.actualPageStrictHoldStarted();const end=Date.now()+500;while(Date.now()<end){};})()',
  )
  await ready
  const pending = assert.rejects(
    projection.isVisible('span'),
    /Actual strict visibility cancellation/u,
  )
  let held
  for (let attempt = 0; attempt < 100; attempt++) {
    held = schema.parse(await page.source.runAutomation('diagnostics'))
    if (held.nativeSelectorWaitPhysical > 0) break
    await delay(1)
  }
  assert(held && held.nativeSelectorWaitPhysical > 0)
  abort.abort(new Error('Actual strict visibility cancellation'))
  await pending
  const afterAbort = schema.parse(await page.source.runAutomation('diagnostics'))
  assert.equal(afterAbort.nativeDOMGetters, 0)
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  await busy
  await bridge.dispose()
  rows.push({ held, afterAbort })
  checks.push(
    'immediate native visibility still owns genuine renderer work and private groups after logical cancellation until actual protocol settlement',
  )
  for (let attempt = 0; attempt < 300; attempt++) {
    if (
      Object.values(schema.parse(await page.source.runAutomation('diagnostics'))).every(
        (value) => value === 0,
      )
    )
      break
    await delay(5)
  }
  const state = schema.parse(await page.source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  await context.close()
  checks.push(
    'all actual Page getter, retained node, private evaluation, physical selector and binding ownership settles to zero',
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
assert.equal(stderr, '')
