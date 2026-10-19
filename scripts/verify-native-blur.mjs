// SPDX-License-Identifier: MIT
/* global document, HTMLElement, Element */
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

const directory = resolve(process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-blur')
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const server = createServer((request, response) => {
  response.setHeader('Content-Type', 'text/html')
  response.end(
    request.url === '/child'
      ? `<!doctype html><input id=child><script>globalThis.childBlurCount=0;document.querySelector('input').addEventListener('blur',event=>{globalThis.childBlurCount++;globalThis.childBlurTrusted=event.isTrusted});document.querySelector('input').focus();document.querySelector('input').dataset.blurReady='true'</script>`
      : '<!doctype html><body>Native blur fixture</body>',
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
  pausedActions: z.number(),
  contextRetryQueued: z.number(),
  pendingBindingCommands: z.number(),
  bindingCalls: z.number(),
})
const events = z.array(z.object({ type: z.string(), id: z.string(), trusted: z.boolean() }))
/** @type {string[]} */
const checks = []
/** @type {unknown[]} */
const behaviorErrors = []
const markup =
  '<!doctype html><input id=first value=original><input id=other><textarea id=area>actual text</textarea><input id=hidden hidden><input id=disabled disabled><input class=duplicate><input class=duplicate><div id=host></div>'
const installJournal = () =>
  page.evaluate(
    "globalThis.actualBlurLog=[];for(const type of ['blur','focusout','input','change'])document.addEventListener(type,event=>globalThis.actualBlurLog.push({type:event.type,id:event.target.id||'',trusted:event.isTrusted}),true)",
  )
const clearJournal = () => page.evaluate('globalThis.actualBlurLog=[]')
const journal = async () => events.parse(await page.evaluate('globalThis.actualBlurLog'))
/**
 * Wait for actual native ownership facts under a bounded proof deadline.
 * @param {(state:z.output<typeof ownership>)=>boolean} predicate
 */
async function waitState(predicate) {
  const deadline = performance.now() + 3000
  while (true) {
    const state = ownership.parse(await source.runAutomation('diagnostics'))
    if (predicate(state)) return state
    assert(
      performance.now() < deadline,
      'Actual native blur ownership did not reach expected state',
    )
  }
}
try {
  await page.goto(`http://127.0.0.1:${address.port}/`)
  await page.setContent(markup)
  await installJournal()
  for (const id of ['first', 'area']) {
    await page.locator('#' + id).focus()
    await clearJournal()
    await page.locator('#' + id).blur()
    assert.deepEqual(await journal(), [
      { type: 'blur', id, trusted: true },
      { type: 'focusout', id, trusted: true },
    ])
    assert.equal(await page.evaluate('document.activeElement.tagName'), 'BODY')
  }
  await page.locator('#first').fill('changed by actual native input')
  await clearJournal()
  await page.locator('#first').blur()
  const changed = await journal()
  assert.deepEqual(changed.map((event) => event.type).sort(), ['blur', 'change', 'focusout'])
  assert(changed.every((event) => event.id === 'first' && event.trusted))
  assert.equal(await page.locator('#first').inputValue(), 'changed by actual native input')
  checks.push(
    'Actual input/textarea blur and focusout are trusted; a real prior user edit produces one genuine change rather than synthetic input',
  )

  await page.locator('#other').focus()
  await clearJournal()
  for (const selector of ['#first', '#hidden', '#disabled'])
    await page.locator(selector).blur({ timeout: 1000 })
  assert.equal(await page.evaluate('document.activeElement.id'), 'other')
  assert.deepEqual(await journal(), [])
  checks.push(
    'Attached hidden/disabled and inactive nodes blur without visibility/enabled waits and preserve another actual focus owner',
  )

  await assert.rejects(page.locator('.duplicate').blur(), /Strict|strict/u)
  const reason = new Error('Original native blur cancellation'),
    abort = new AbortController()
  abort.abort(reason)
  await assert.rejects(
    page.locator('#missing').blur({ signal: abort.signal }),
    (error) => error === reason,
  )
  page.setDefaultTimeout(70)
  await assert.rejects(
    page.locator('#missing').blur(),
    (error) => error instanceof NativeTimeoutError && error.message.includes('Timeout'),
  )
  page.setDefaultTimeout(2000)
  assert.equal(await page.evaluate('document.activeElement.id'), 'other')
  assert.deepEqual(await journal(), [])
  checks.push(
    'Strict selection, exact already-aborted caller reason and current native default deadline reject without blurring another node',
  )

  const pinned = await page.$('#first')
  assert(pinned)
  await page.evaluate(
    "const old=document.querySelector('#first');old.id='renamed';const replacement=document.createElement('input');replacement.id='first';document.body.append(replacement);replacement.focus()",
  )
  await pinned.blur()
  assert.equal(await page.evaluate('document.activeElement.id'), 'first')
  await pinned.focus()
  await clearJournal()
  await pinned.blur()
  assert.deepEqual(await journal(), [
    { type: 'blur', id: 'renamed', trusted: true },
    { type: 'focusout', id: 'renamed', trusted: true },
  ])
  await pinned.evaluate((element) => {
    if (!(element instanceof Element)) throw new Error('Pinned blur target is not an Element')
    element.remove()
  })
  await page.locator('#first').focus()
  await assert.rejects(pinned.blur(), /not attached|detached|unavailable|not found/iu)
  assert.equal(await page.evaluate('document.activeElement.id'), 'first')
  await pinned.dispose()
  checks.push(
    'Pinned node rename/selector replacement remains exact and detached handles never blur the live replacement',
  )

  await page.evaluate(
    "const shadow=document.querySelector('#host').attachShadow({mode:'open'});shadow.innerHTML='<input id=deep>';globalThis.shadowBlur=[];shadow.querySelector('input').addEventListener('blur',event=>globalThis.shadowBlur.push({id:event.target.id,trusted:event.isTrusted}))",
  )
  await page.locator('#deep').focus()
  await page.locator('#deep').blur()
  assert.deepEqual(await page.evaluate('globalThis.shadowBlur'), [{ id: 'deep', trusted: true }])
  assert.equal(
    await page.evaluate("document.querySelector('#host').shadowRoot.activeElement"),
    null,
  )
  checks.push(
    'The actual selected open-shadow input blurs with trusted events and no ancestor substitution',
  )

  await page.evaluate(
    "globalThis.blurMainMarker=true;globalThis.authorBlurCalls=0;globalThis.originalAuthorBlur=HTMLElement.prototype.blur;HTMLElement.prototype.blur=function(){globalThis.authorBlurCalls++;throw new Error('Main author blur is not the native controller')}",
  )
  const selectors = new NativeSelectors(source)
  await selectors.register('actualBlurMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'blurMainMarker') !== true)
        throw new Error('Expected genuine main selector world')
      return root.querySelectorAll(selector)
    },
  }))
  await selectors.register(
    'actualBlurUtility',
    () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (Reflect.get(globalThis, 'blurMainMarker') !== undefined)
          throw new Error('Expected genuine utility selector world')
        return root.querySelectorAll(selector)
      },
    }),
    { contentScript: true },
  )
  for (const selector of [
    'actualBlurMain=#first',
    'actualBlurUtility=#first',
    'actualBlurMain=body >> actualBlurUtility=#first',
  ]) {
    await page.locator('#first').focus()
    await clearJournal()
    await page.locator(selector).blur()
    assert.equal(await page.evaluate('document.activeElement.tagName'), 'BODY')
    assert.equal(
      (await journal()).filter(
        (event) => event.type === 'blur' && event.id === 'first' && event.trusted,
      ).length,
      1,
    )
  }
  assert.equal(await page.evaluate('globalThis.authorBlurCalls'), 0)
  await page.evaluate('HTMLElement.prototype.blur=globalThis.originalAuthorBlur')
  checks.push(
    'Main/utility/mixed factories retain declared worlds while actual adopted utility wrappers call genuine browser blur under author prototype replacement',
  )

  const lazy = page
    .frameLocator('#late')
    .locator('input[data-blur-ready=true]')
    .blur({ timeout: 2000 })
  await page.evaluate((port) => {
    const frame = document.createElement('iframe')
    frame.id = 'late'
    frame.src = `http://localhost:${port}/child`
    document.body.append(frame)
  }, address.port)
  await lazy
  const child = page.frameLocator('#late').locator('input')
  assert.deepEqual(
    await child.evaluate((element) => {
      const view = element.ownerDocument.defaultView ?? {}
      const count = /** @type {unknown} */ (Reflect.get(view, 'childBlurCount'))
      const trusted = /** @type {unknown} */ (Reflect.get(view, 'childBlurTrusted'))
      if (typeof count !== 'number' || typeof trusted !== 'boolean')
        throw new Error('Actual child blur observations are missing')
      return { active: element.ownerDocument.activeElement === element, count, trusted }
    }),
    { active: false, count: 1, trusted: true },
  )
  const frameDiagnostics = z
    .object({ sessions: z.number() })
    .parse(await source.runAutomation('diagnostics'))
  assert(frameDiagnostics.sessions > 0)
  checks.push(
    'A lazy genuine cross-site frame is resolved natively and its real initially focused child receives one trusted blur',
  )

  const held = await page.$('#first')
  assert(held)
  await held.focus()
  await clearJournal()
  /**
   * Receives the actual renderer binding call.
   * @type {()=>void}
   */
  let markStarted = () => {
    throw new Error('Actual hold observation was not registered')
  }
  const started = new Promise((resolve) => {
    markStarted = () => resolve(undefined)
  })
  const bridge = await page.exposeFunction('actualBlurHoldStarted', () => markStarted())
  const busy = page.evaluate(
    '(()=>{globalThis.actualBlurHoldStarted();const until=performance.now()+650;while(performance.now()<until){};return 42;})()',
  )
  await started
  const controller = new AbortController(),
    heldReason = new Error('Cancel native blur with real renderer ACK held')
  const pending = held.blur({ timeout: 0, signal: controller.signal })
  const pendingAssertion = assert.rejects(pending, (error) => error === heldReason)
  const during = await waitState((state) => state.nativeSelectorWaitPhysical > 0)
  controller.abort(heldReason)
  await pendingAssertion
  const afterAbort = ownership.parse(await source.runAutomation('diagnostics'))
  assert(afterAbort.nativeSelectorWaits > 0)
  assert(afterAbort.nativeSelectorWaitPhysical > 0)
  assert.equal(await busy, 42)
  assert.equal(await page.evaluate('document.activeElement.id'), 'first')
  assert.deepEqual(await journal(), [])
  await bridge.dispose()
  await held.dispose()
  const final = await waitState((state) => Object.values(state).every((value) => value === 0))
  checks.push(
    'Fixed-node logical cancellation retains actual native physical adoption/release ownership until genuine blocked-renderer ACKs settle; all registries return to zero',
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
        finalOwnership: final,
        genuineChildSessions: frameDiagnostics.sessions,
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
console.info(
  'Native actual trusted blur, no-op states, pinned/shadow/frame identity, realms, deadlines and physical cancellation8 passed',
)
