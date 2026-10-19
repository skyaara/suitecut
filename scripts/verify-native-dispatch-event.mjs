// SPDX-License-Identifier: MIT
/* global document, Element, EventTarget, Node */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { createServer } from 'node:http'
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
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-dispatch-event',
)
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(
    request.url === '/child'
      ? `<!doctype html><button id=child>Child</button><script>globalThis.childEvents=[];document.addEventListener('click',event=>childEvents.push({id:event.target.id,trusted:event.isTrusted,kind:event.constructor.name}));document.querySelector('button').dataset.ready='true'</script>`
      : '<!doctype html><body>Native dispatch fixture</body>',
  )
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const source = await launchNativeBrowser({
  executablePath,
  width: 320,
  height: 180,
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
  contextRetryQueued: z.number(),
  pendingBindingCommands: z.number(),
  bindingCalls: z.number(),
})
/** @type {string[]} */ const checks = []
/** @type {unknown[]} */ const behaviorErrors = []
/**
 * Observe genuine native ownership until the specified bounded condition.
 * @param {(state:z.output<typeof ownership>)=>boolean} predicate
 */
async function waitState(predicate) {
  const deadline = performance.now() + 3000
  while (true) {
    const state = ownership.parse(await source.runAutomation('diagnostics'))
    if (predicate(state)) return state
    assert(performance.now() < deadline, 'Actual native dispatch ownership did not settle')
  }
}
const markup =
  '<!doctype html><input id=outside value=outside><button id=button hidden disabled>Disabled hidden</button><span id=related></span><button class=duplicate id=one>One</button><button class=duplicate id=two>Two</button><label id=label for=outside>Label</label><div id=host></div>'
const reset = async () => {
  await page.setContent(markup)
  await page.evaluate(
    `globalThis.actualEvents=[];for(const type of ['click','pointerdown','keydown','beforeinput','wheel','custom','mouseover'])document.addEventListener(type,event=>{actualEvents.push({type:event.type,id:event.target.id,trusted:event.isTrusted,kind:event.constructor.name,bubbles:event.bubbles,cancelable:event.cancelable,composed:event.composed,button:event.button??null,key:event.key??null,data:event.data??null,deltaY:event.deltaY??null,pointerId:event.pointerId??null,related:event.relatedTarget?.id??null});event.preventDefault()},true);document.querySelector('#outside').focus()`,
  )
}
const events = () => page.evaluate('globalThis.actualEvents')
try {
  await page.goto(`http://127.0.0.1:${address.port}/`)
  await reset()
  await page.locator('#button').dispatchEvent('click', { button: 1 })
  assert.deepEqual(await events(), [
    {
      type: 'click',
      id: 'button',
      trusted: false,
      kind: 'MouseEvent',
      bubbles: true,
      cancelable: true,
      composed: true,
      button: 1,
      key: null,
      data: null,
      deltaY: null,
      pointerId: null,
      related: null,
    },
  ])
  assert.equal(await page.locator('#outside').inputValue(), 'outside')
  assert.equal(await page.evaluate('document.activeElement.id'), 'outside')
  checks.push(
    'Actual hidden/disabled selected node receives browser MouseEvent with synthetic identity and requested fields; no trusted gesture or focus/input substitution',
  )

  await page.evaluate('globalThis.actualEvents=[]')
  for (const [
    type,
    init,
  ] of /** @type {[string,import('../dist/untrusted.js').UntrustedInput][]} */ ([
    ['pointerdown', { pointerId: 42 }],
    ['keydown', { key: 'K', bubbles: false, cancelable: false, composed: false }],
    ['beforeinput', { data: 'actual data', inputType: 'insertText' }],
    ['wheel', { deltaY: 17 }],
  ]))
    await page.locator('#button').dispatchEvent(type, init)
  const typed = z
    .array(
      z.object({
        type: z.string(),
        kind: z.string(),
        pointerId: z.number().nullable(),
        key: z.string().nullable(),
        data: z.string().nullable(),
        deltaY: z.number().nullable(),
        bubbles: z.boolean(),
        cancelable: z.boolean(),
        composed: z.boolean(),
        trusted: z.boolean(),
      }),
    )
    .parse(await events())
  assert.deepEqual(
    typed.map((event) => event.kind),
    ['PointerEvent', 'KeyboardEvent', 'InputEvent', 'WheelEvent'],
  )
  assert.equal(typed[0]?.pointerId, 42)
  assert.equal(typed[1]?.key, 'K')
  assert.equal(typed[1]?.bubbles, false)
  assert.equal(typed[1]?.cancelable, false)
  assert.equal(typed[1]?.composed, false)
  assert.equal(typed[2]?.data, 'actual data')
  assert.equal(typed[3]?.deltaY, 17)
  assert(typed.every((event) => !event.trusted))
  checks.push(
    'Browser Pointer/Keyboard/Input/Wheel constructors retain genuine subclass data and explicit EventInit false flags',
  )

  await page.evaluate(
    `document.querySelector('#button').addEventListener('custom',event=>{const d=event.detail;globalThis.customObservation={units:d.units.charCodeAt(0),self:d.self===d,ownProto:Object.hasOwn(d,'__proto__'),protoValue:d.__proto__,cleanPrototype:Object.getPrototypeOf(d)!==null&&Object.getPrototypeOf(d)!==d.__proto__&&Object.getPrototypeOf(Object.getPrototypeOf(d))===null,negativeZero:Object.is(d.zero,-0)}},{once:true})`,
  )
  /** @type {Record<string,import('../dist/untrusted.js').UntrustedInput>} */ const detail = {
    units: '\ud800',
    zero: -0,
  }
  detail.self = detail
  Object.defineProperty(detail, '__proto__', { value: 'literal data', enumerable: true })
  await page.locator('#button').dispatchEvent('custom', { detail })
  assert.deepEqual(await page.evaluate('globalThis.customObservation'), {
    units: 0xd800,
    self: true,
    ownProto: true,
    protoValue: 'literal data',
    cleanPrototype: true,
    negativeZero: true,
  })
  checks.push(
    'Native-owned tagged decoder preserves cyclic CustomEvent detail, isolated UTF-16, negative zero and literal __proto__ without prototype mutation',
  )

  await page.evaluate('globalThis.actualEvents=[]')
  await page.dispatchEvent('.duplicate', 'click')
  await assert.rejects(page.locator('.duplicate').dispatchEvent('click'), /Strict|strict/u)
  await assert.rejects(
    page.dispatchEvent('.duplicate', 'click', {}, { strict: true }),
    /Strict|strict/u,
  )
  await page.locator('#label').dispatchEvent('custom')
  assert.deepEqual(
    z
      .array(z.object({ id: z.string() }))
      .parse(await events())
      .map((event) => event.id),
    ['one', 'label'],
  )
  checks.push(
    'Page first-match and native strict Locator/Page guards preserve original label identity rather than retargeting its associated input',
  )

  const pinned = await page.$('#one')
  assert(pinned)
  await page.evaluate(
    `document.querySelector('#one').id='renamed';const n=document.createElement('button');n.id='one';n.textContent='Actual replacement Text';document.body.append(n);globalThis.actualEvents=[]`,
  )
  await pinned.dispatchEvent('click')
  assert.equal(z.array(z.object({ id: z.string() })).parse(await events())[0]?.id, 'renamed')
  await pinned.evaluate((element) => {
    if (!(element instanceof Element)) throw new Error('Actual pinned Element required')
    element.remove()
  })
  await assert.rejects(pinned.dispatchEvent('click'), /not attached|unavailable|detached/iu)
  await pinned.dispose()
  assert.equal(z.array(z.object({ id: z.string() })).parse(await events()).length, 1)
  checks.push(
    'Pinned renamed node dispatches once; actual detached identity never reacquires its live selector replacement',
  )

  const textHandle = await page.locator('#one').evaluateHandle((element) => element.firstChild)
  const text = textHandle.asElement()
  assert(text)
  await text.evaluate((node) => {
    if (!(node instanceof Node)) throw new Error('Actual Text Node required')
    node.addEventListener('custom', (event) =>
      Reflect.set(node.ownerDocument?.defaultView ?? {}, 'actualTextDispatch', {
        type: node.nodeType,
        target: event.target === node,
        trusted: event.isTrusted,
      }),
    )
  })
  await text.dispatchEvent('custom')
  assert.deepEqual(await page.evaluate('globalThis.actualTextDispatch'), {
    type: 3,
    target: true,
    trusted: false,
  })
  await text.dispose()

  const related = await page.$('#related')
  assert(related)
  await page.locator('#button').dispatchEvent('mouseover', { relatedTarget: related })
  assert.equal(
    z
      .array(z.object({ related: z.string().nullable() }))
      .parse(await events())
      .at(-1)?.related,
    'related',
  )
  await related.dispose()
  const transfer = await page.evaluateHandle(
    `globalThis.actualTransfer=new DataTransfer();actualTransfer.items.add(new File(['genuine native bytes'],'native-☃.txt',{type:'text/plain'}));document.querySelector('#button').addEventListener('drop',event=>{globalThis.actualTransferObservation={same:event.dataTransfer===actualTransfer,trusted:event.isTrusted,kind:event.constructor.name,name:event.dataTransfer.files[0].name};globalThis.actualTransferFile=event.dataTransfer.files[0]},{once:true});actualTransfer`,
  )
  await page.locator('#button').dispatchEvent('drop', { dataTransfer: transfer })
  assert.deepEqual(await page.evaluate('globalThis.actualTransferObservation'), {
    same: true,
    trusted: false,
    kind: 'DragEvent',
    name: 'native-☃.txt',
  })
  assert.equal(await page.evaluate('globalThis.actualTransferFile.text()'), 'genuine native bytes')
  await transfer.dispose()
  checks.push(
    'Real DOM handle adopts by native backend identity; genuine same-document DataTransfer and File identity/bytes pass through the native retained pristine main factory',
  )

  await page.evaluate(
    `globalThis.authorMarker=true;globalThis.authorDispatchCalls=0;globalThis.originalEventDispatch=EventTarget.prototype.dispatchEvent;EventTarget.prototype.dispatchEvent=function(){globalThis.authorDispatchCalls++;throw new Error('Poisoned main dispatch')};globalThis.originalMouseEvent=MouseEvent;globalThis.MouseEvent=function(){throw new Error('Poisoned main constructor')}`,
  )
  const selectors = new NativeSelectors(source)
  await selectors.register('dispatchMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'authorMarker') !== true) throw new Error('Expected main factory')
      return Array.from(root.querySelectorAll(selector))
    },
  }))
  await selectors.register(
    'dispatchUtility',
    () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (Reflect.get(globalThis, 'authorMarker') !== undefined)
          throw new Error('Expected utility factory')
        return Array.from(root.querySelectorAll(selector))
      },
    }),
    { contentScript: true },
  )
  for (const selector of [
    'dispatchMain=#button',
    'dispatchUtility=#button',
    'dispatchMain=body >> dispatchUtility=#button',
  ])
    await page.locator(selector).dispatchEvent('click')
  assert.equal(await page.evaluate('globalThis.authorDispatchCalls'), 0)
  await page.evaluate(
    'EventTarget.prototype.dispatchEvent=globalThis.originalEventDispatch;globalThis.MouseEvent=globalThis.originalMouseEvent',
  )
  checks.push(
    'Main/utility/mixed selector factories preserve their declared worlds while genuine adopted DOM dispatch uses isolated platform constructors under main-world poisoning',
  )

  const lazy = page
    .frameLocator('#late')
    .locator('button[data-ready=true]')
    .dispatchEvent('click', { button: 0 }, { timeout: 2000 })
  await page.evaluate((port) => {
    const frame = document.createElement('iframe')
    frame.id = 'late'
    frame.src = `http://localhost:${port}/child`
    document.body.append(frame)
  }, address.port)
  await lazy
  assert.deepEqual(
    JSON.parse(
      z.string().parse(
        await page
          .frameLocator('#late')
          .locator('button')
          .evaluate((element) =>
            JSON.stringify(Reflect.get(element.ownerDocument.defaultView ?? {}, 'childEvents')),
          ),
      ),
    ),
    [{ id: 'child', trusted: false, kind: 'MouseEvent' }],
  )
  const childSessions = z
    .object({ sessions: z.number() })
    .parse(await source.runAutomation('diagnostics')).sessions
  assert(childSessions > 0)
  checks.push(
    'Native lazy OOPIF selection dispatches in the actual child owner document with genuine browser event identity',
  )

  await assert.rejects(
    page.locator('#missing').dispatchEvent('click', {}, { timeout: 60 }),
    (error) => error instanceof NativeTimeoutError,
  )
  const target = await page.$('#button')
  assert(target)
  /**
   * Marks receipt of the actual renderer binding callback.
   * @type {()=>void}
   */ let observed = () => {
    throw new Error('Hold observer missing')
  }
  const started = new Promise((resolve) => {
    observed = () => resolve(undefined)
  })
  const binding = await page.exposeFunction('dispatchHoldStarted', () => {
    observed()
    return undefined
  })
  const busy = page.evaluate(
    '(()=>{globalThis.dispatchHoldStarted();const until=performance.now()+650;while(performance.now()<until){};return 42})()',
  )
  await started
  const abort = new AbortController(),
    reason = new Error('Exact dispatch cancellation with held physical ACK')
  const pending = target.dispatchEvent('click', {}, { timeout: 0, signal: abort.signal }),
    rejection = assert.rejects(pending, (error) => error === reason)
  const during = await waitState((state) => state.nativeSelectorWaitPhysical > 0)
  abort.abort(reason)
  await rejection
  const afterAbort = ownership.parse(await source.runAutomation('diagnostics'))
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  assert(afterAbort.nativeSelectorWaits > 0)
  assert.equal(await busy, 42)
  await binding.dispose()
  await target.dispose()
  checks.push(
    'Native original timeout and exact abort retire logical work while held actual node-adoption/release ACKs remain physically owned',
  )

  await page.evaluate(
    `const shadow=document.querySelector('#host').attachShadow({mode:'open'});shadow.innerHTML='<button id=deep>Deep</button>';globalThis.shadowEvents=[];shadow.addEventListener('custom',event=>globalThis.shadowEvents.push({target:event.target.id,first:event.composedPath()[0].id,composed:event.composed}));globalThis.actualEvents=[]`,
  )
  await page.locator('#deep').dispatchEvent('custom', { composed: false })
  await page.locator('#deep').dispatchEvent('custom', { composed: true })
  assert.deepEqual(await page.evaluate('globalThis.shadowEvents'), [
    { target: 'deep', first: 'deep', composed: false },
    { target: 'deep', first: 'deep', composed: true },
  ])
  assert.deepEqual(
    z
      .array(z.object({ id: z.string() }))
      .parse(await events())
      .map((event) => event.id),
    ['host'],
  )
  checks.push(
    'Actual shadow dispatch obeys browser composed-path propagation without retargeting the selected node to its host',
  )

  const closing = await page.context().newPage()
  await closing.setContent('<button id=live>Child</button>')
  const waitClosing = closing.locator('#never').dispatchEvent('click', {}, { timeout: 0 })
  const closedRejection = assert.rejects(waitClosing, /closed|close|cancel|detached|destroyed/iu)
  const closeDeadline = performance.now() + 2000
  while (true) {
    const state = ownership.parse(await closing.source.runAutomation('diagnostics'))
    if (state.nativeSelectorWaits > 0) break
    assert(performance.now() < closeDeadline, 'Native close candidate was never admitted')
  }
  await closing.close()
  await closedRejection
  assert.equal(await page.evaluate('6*7'), 42)
  checks.push(
    'Actual page close rejects an admitted unbounded missing-target dispatch and leaves its healthy owner root operational',
  )

  await page.evaluate(
    `document.querySelector('#button').addEventListener('custom',()=>{throw new Error('Actual dispatch listener exception')},{once:true})`,
  )
  const listenerError = page.waitForEvent('pageerror')
  await page.locator('#button').dispatchEvent('custom')
  assert.match((await listenerError).message, /Actual dispatch listener exception/u)
  const final = await waitState((state) => Object.values(state).every((value) => value === 0))
  assert.equal(checks.length, 11)
  checks.push(
    'Browser reports listener exceptions through actual pageerror while DOM dispatch completes normally; native operation/group/handle/physical registries settle to zero',
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        checks,
        during,
        afterAbort,
        finalOwnership: final,
        genuineChildSessions: childSessions,
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
  try {
    await finishNativeProof(source, diagnostics, directory, behaviorErrors)
  } finally {
    server.closeAllConnections()
    await new Promise((resolve) => server.close(() => resolve(undefined)))
  }
}
console.info('Native explicit synthetic DOM dispatch12 passed')
