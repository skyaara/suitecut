// SPDX-License-Identifier: MIT
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
  process.env.SUITECUT_NATIVE_PROOF_DIRECTORY ?? '.suitecut/native-pristine-dispatch',
)
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
await mkdir(directory, { recursive: true })
const diagnostics = await captureNativeStderr(directory)
const fixture = `<!doctype html><button id=target onclick="globalThis.actualAtomic=(globalThis.actualAtomic||0)+1">Actual</button><script>
globalThis.actualTransfer=new DataTransfer();actualTransfer.items.add(new File(['genuine file bytes'],'native-☃.txt',{type:'text/plain'}));
globalThis.actualDrops=[];document.querySelector('#target').addEventListener('drop',event=>{actualDrops.push({same:event.dataTransfer===actualTransfer,trusted:event.isTrusted,kind:event.constructor.name,name:event.dataTransfer.files[0].name});globalThis.actualFile=event.dataTransfer.files[0]});
globalThis.savedNativeExec=RegExp.prototype.exec;
globalThis.Event=function(){throw Error('Actual author Event poison')};globalThis.MouseEvent=function(){throw Error('Actual author MouseEvent poison')};globalThis.DragEvent=function(){throw Error('Actual author DragEvent poison')};EventTarget.prototype.dispatchEvent=function(){throw Error('Actual author dispatch poison')};
</script>`
const server = createServer((_request, response) => {
  response.setHeader('Content-Type', 'text/html; charset=utf-8')
  response.end(fixture)
})
await new Promise((resolve) => server.listen(0, '0.0.0.0', () => resolve(undefined)))
const address = server.address()
assert(address && typeof address !== 'string')
const origin = `http://127.0.0.1:${address.port}`,
  childOrigin = `http://localhost:${address.port}`
const source = await launchNativeBrowser({
  executablePath,
  width: 360,
  height: 240,
  headless: true,
  onDiagnostic: diagnostics.observe,
})
const page = createNativePage(source)
page.setDefaultTimeout(2000)
const binarySHA256 = createHash('sha256')
  .update(await readFile(executablePath))
  .digest('hex')
const state = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
  pristineDispatchContexts: z.number(),
  pristineDispatchGroups: z.number(),
  pristineDispatchRetiringGroups: z.number(),
  pristineDispatchPhysical: z.number(),
  nativeDebuggerPhysical: z.number(),
  nativeDebuggerFactoryReceipts: z.number(),
  nativeDebuggerPauses: z.number(),
  nativeDebuggerLeases: z.number(),
  nativeDebuggerRawOwners: z.number(),
})
const settled = (/** @type {z.output<typeof state>} */ value) =>
  value.otherOperations === 0 &&
  value.pendingHandles === 0 &&
  value.evaluationGroups === 0 &&
  value.nativeSelectorWaits === 0 &&
  value.nativeSelectorWaitPhysical === 0 &&
  value.pendingDevTools === 0 &&
  value.pristineDispatchRetiringGroups === 0 &&
  value.pristineDispatchPhysical === 0 &&
  value.nativeDebuggerPhysical === 0 &&
  value.nativeDebuggerFactoryReceipts === 0 &&
  value.nativeDebuggerPauses === 0
/**
 * Observe genuine native leases until their requested bounded condition.
 * @param {(value:z.output<typeof state>)=>boolean} predicate
 */
async function waitState(predicate) {
  const deadline = performance.now() + 3000
  while (true) {
    const value = state.parse(await source.runAutomation('diagnostics'))
    if (predicate(value)) return value
    assert(performance.now() < deadline, 'Actual pristine-dispatch ownership did not settle')
  }
}
/** @type {string[]} */ const checks = []
/** @type {unknown[]} */ const behaviorErrors = []
try {
  const initial = await waitState(settled)
  assert.equal(initial.pristineDispatchContexts, 1)
  assert.equal(initial.pristineDispatchGroups, 1)
  assert.equal(await page.evaluate('location.href'), 'about:blank')
  checks.push(
    'Actual initial empty document owns one native-retained factory before any authored document',
  )
  await page.goto(origin)
  const transfer = await page.evaluateHandle('globalThis.actualTransfer')
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.deepEqual(await page.evaluate('globalThis.actualDrops'), [
    { same: true, trusted: false, kind: 'DragEvent', name: 'native-☃.txt' },
  ])
  assert.equal(await page.evaluate('globalThis.actualFile.text()'), 'genuine file bytes')
  checks.push(
    'First-author constructor/dispatch poisoning cannot replace real native factory intrinsics; DataTransfer/File identity and bytes remain genuine',
  )

  const selectors = new NativeSelectors(source)
  await selectors.register('nativeAtomicDispatch', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      const nodes = Array.from(root.querySelectorAll(selector))
      for (const node of nodes)
        void Promise.resolve().then(() => {
          ;/** @type {HTMLElement} */ (node).onclick = null
        })
      return nodes
    },
  }))
  await page.locator('nativeAtomicDispatch=#target').dispatchEvent('click')
  assert.equal(await page.evaluate('globalThis.actualAtomic'), 1)
  await selectors.register('nativeReplayError', () => ({
    query() {
      throw new Error('Element is not attached to the DOM')
    },
    queryAll(root) {
      const node = root.querySelector('#target')
      if (node)
        node.setAttribute(
          'data-native-replay',
          String(Number(node.getAttribute('data-native-replay') ?? '0') + 1),
        )
      throw new Error('Element is not attached to the DOM')
    },
  }))
  await assert.rejects(
    page
      .locator('nativeReplayError=#target')
      .dispatchEvent('drop', { dataTransfer: transfer }, { timeout: 500 }),
    (error) =>
      error instanceof Error &&
      !(error instanceof NativeTimeoutError) &&
      error.message.includes('Element is not attached to the DOM'),
  )
  assert.equal(
    await page.evaluate(`document.querySelector('#target').getAttribute('data-native-replay')`),
    '1',
  )
  checks.push(
    'Actual main selector and dispatch run atomically; an authored detached-message error propagates once without replay',
  )

  await page.evaluate(
    `RegExp.prototype.exec=function(){throw Error('Actual author RegExp exec poison')};Object.defineProperty(Node.prototype,'isConnected',{get(){throw Error('Actual author connected poison')},configurable:true});Object.defineProperty(Node.prototype,'nodeType',{get(){throw Error('Actual author nodeType poison')},configurable:true});void 0`,
  )
  const target = await page.$('#target')
  assert(target)
  await target.dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(await page.evaluate('globalThis.actualDrops.length'), 2)
  await target.dispose()
  const adopted = await page.$('#target')
  assert(adopted)
  await page.evaluate(
    `const child=document.createElement('iframe');child.id='actualSameDocumentChild';document.body.appendChild(child);child.contentDocument.body.appendChild(child.contentDocument.adoptNode(document.querySelector('#target')))`,
  )
  await assert.rejects(
    adopted.dispatchEvent('drop', { dataTransfer: transfer }),
    /Native main dispatch target belongs to another document/u,
  )
  await selectors.register('nativeForeignTarget', () => ({
    query(root, selector) {
      return root.querySelector('iframe')?.contentDocument?.querySelector(selector) ?? null
    },
    queryAll(root, selector) {
      const node = root.querySelector('iframe')?.contentDocument?.querySelector(selector)
      return node ? [node] : []
    },
  }))
  await assert.rejects(
    page.locator('nativeForeignTarget=#target').dispatchEvent('drop', { dataTransfer: transfer }),
    /Native main dispatch target belongs to another document/u,
  )
  assert.equal(await page.evaluate('globalThis.actualDrops.length'), 2)
  await page.evaluate(
    `const child=document.querySelector('#actualSameDocumentChild');document.body.appendChild(document.adoptNode(child.contentDocument.querySelector('#target')));child.remove()`,
  )
  await adopted.dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(await page.evaluate('globalThis.actualDrops.length'), 3)
  await adopted.dispose()
  checks.push(
    'Native captured accessors resist poisoning; genuinely adopted foreign-document targets reject without delivery and the same pinned node works only after actual restoration',
  )

  const beforeRaw = await waitState(settled)
  await assert.rejects(
    source.runAutomation('devtools', { method: 'Debugger.disable', params: {} }),
    /Raw Debugger command conflicts with native factory\/coverage ownership/u,
  )
  for (const method of ['Page.disable', 'Runtime.disable'])
    await assert.rejects(
      source.runAutomation('devtools', { method, params: {} }),
      /Raw Runtime\/Page disable conflicts with native pristine document lifecycle ownership/u,
    )
  const afterRaw = await waitState(settled)
  assert.equal(afterRaw.nativeDebuggerRawOwners, beforeRaw.nativeDebuggerRawOwners)
  assert.equal(afterRaw.nativeDebuggerLeases, beforeRaw.nativeDebuggerLeases)
  assert.equal(afterRaw.pristineDispatchContexts, beforeRaw.pristineDispatchContexts)
  checks.push(
    'Actual raw Debugger/Runtime/Page disable conflicts explicitly and changes no native ownership or factory readiness',
  )

  await page.coverage.startJSCoverage({ reportAnonymousScripts: true })
  const duringCoverage = await waitState(settled)
  assert.equal(duringCoverage.nativeDebuggerLeases, beforeRaw.nativeDebuggerLeases + 1)
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(
    await page.evaluate(
      'function actualPristineCoverageFunction(){return 42};actualPristineCoverageFunction()',
    ),
    42,
  )
  const coverage = await page.coverage.stopJSCoverage()
  assert(
    coverage.some((entry) =>
      entry.functions.some(
        (fn) =>
          fn.functionName === 'actualPristineCoverageFunction' &&
          fn.ranges.some((range) => range.count > 0),
      ),
    ),
  )
  const afterCoverage = await waitState(settled)
  assert.equal(afterCoverage.nativeDebuggerLeases, beforeRaw.nativeDebuggerLeases)
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer })
  assert.equal(await page.evaluate('globalThis.actualDrops.length'), 5)
  checks.push(
    'Genuine JS coverage and opaque dispatch share the domain; stopping coverage preserves the live native factory lease and dispatch',
  )

  await assert.rejects(
    page.locator('#absent').dispatchEvent('drop', { dataTransfer: transfer }, { timeout: 30 }),
    NativeTimeoutError,
  )
  const aborter = new AbortController(),
    reason = new Error('Exact native opaque dispatch abort')
  const pending = page
    .locator('#absent')
    .dispatchEvent('drop', { dataTransfer: transfer }, { timeout: 0, signal: aborter.signal })
  const rejected = assert.rejects(pending, (error) => error === reason)
  await waitState((value) => value.nativeSelectorWaits > 0)
  aborter.abort(reason)
  await rejected
  await waitState(settled)
  checks.push(
    'Original native deadline and exact caller cancellation reject while real selector/group ownership settles',
  )

  await page.evaluate(
    `document.body.insertAdjacentHTML('beforeend','<iframe id=child src="${childOrigin}/child"></iframe>')`,
  )
  const frame = page.frameLocator('#child'),
    childTransfer = await frame.locator('#target').evaluateHandle(() => {
      const actual = /** @type {typeof globalThis & {actualTransfer:DataTransfer}} */ (globalThis)
      return actual.actualTransfer
    })
  await frame.locator('#target').dispatchEvent('drop', { dataTransfer: childTransfer })
  assert.equal(
    z.array(z.object({ same: z.boolean() })).parse(
      await frame.locator('#target').evaluate(() => {
        const actual = /** @type {typeof globalThis & {actualDrops:{same:boolean}[]}} */ (
          globalThis
        )
        return actual.actualDrops
      }),
    )[0]?.same,
    true,
  )
  await assert.rejects(
    frame.locator('#target').dispatchEvent('drop', { dataTransfer: transfer }),
    /JSHandles can be evaluated only in the context they were created!/u,
  )
  const sessionCount = z
    .object({ sessions: z.number() })
    .parse(await source.runAutomation('diagnostics')).sessions
  assert(sessionCount > 0, 'Actual OOPIF Inspector session required')
  await childTransfer.dispose()
  await page.evaluate('document.querySelector("#child").remove()')
  await waitState(settled)
  checks.push(
    'Genuine OOPIF factory receives its own DataTransfer and rejects a root-context opaque object without forging a transfer',
  )

  const closing = await page.context().newPage()
  await closing.goto(origin)
  const closingTransfer = await closing.evaluateHandle('globalThis.actualTransfer')
  const closeWait = closing
    .locator('#absent')
    .dispatchEvent('drop', { dataTransfer: closingTransfer }, { timeout: 0 })
  const closeRejected = assert.rejects(closeWait, /closed|close|cancel|detached|destroyed/iu)
  const closeDeadline = performance.now() + 3000
  while (true) {
    const observed = z
      .object({ nativeSelectorWaits: z.number() })
      .parse(await closing.source.runAutomation('diagnostics'))
    if (observed.nativeSelectorWaits > 0) break
    assert(performance.now() < closeDeadline, 'Actual closing-page dispatch was not admitted')
  }
  await closing.close()
  await closeRejected
  assert.equal(await page.evaluate('6*7'), 42)
  checks.push(
    'Actual page closure retires its pending opaque dispatch/factory while the healthy owner root remains usable',
  )

  await page.goto(origin + '/next')
  await assert.rejects(
    page.locator('#target').dispatchEvent('drop', { dataTransfer: transfer }),
    /disposed|context|destroyed|guid|unavailable/iu,
  )
  await transfer.dispose()
  const fresh = await page.evaluateHandle('globalThis.actualTransfer')
  await page.locator('#target').dispatchEvent('drop', { dataTransfer: fresh })
  assert.equal(await page.evaluate('globalThis.actualDrops[0].same'), true)
  await fresh.dispose()
  const final = await waitState(
    (value) =>
      settled(value) &&
      value.activeHandles === 0 &&
      value.pristineDispatchGroups === value.pristineDispatchContexts,
  )
  assert.equal(final.pristineDispatchContexts, 1)
  assert.equal(checks.length, 9)
  checks.push(
    'A new actual document owns a fresh factory, old handles never revive, and all temporary physical/group/handle work settles',
  )
  await writeFile(
    resolve(directory, 'report.json'),
    JSON.stringify(
      {
        completed: true,
        binarySHA256,
        checks,
        initial,
        finalOwnership: final,
        genuineChildSessions: sessionCount,
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
console.info('Native pristine main dispatch10 passed')
