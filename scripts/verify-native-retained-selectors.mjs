// SPDX-License-Identifier: MIT
/* global document, Element, setTimeout */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  createNativePage,
  launchNativeBrowser,
  NativeSelectors,
  NativeElementHandle,
} from '../dist/native.js'

function latch() {
  const signal = new AbortController()
  return {
    open: () => signal.abort(),
    promise: new Promise((resolve) =>
      signal.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    ),
  }
}
const directory = resolve('.suitecut/native-retained-selectors')
await mkdir(directory, { recursive: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
let stderr = ''
const source = await launchNativeBrowser({
  executablePath,
  headless: true,
  onDiagnostic(message) {
    if (stderr.length < 1048576) stderr += message.slice(0, 1048576 - stderr.length)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  await page.setContent(
    '<section id="scope"><span id="first">First</span><span id="second">Second</span><div id="shadow"></div></section><span id="outside">Outside</span>',
  )
  const root = await page.$('#scope')
  assert(root)
  assert.equal(await root.$eval('css=span', (node) => node.id), 'first')
  assert.deepEqual(await root.$$eval('css=span', (nodes) => nodes.map((node) => node.id)), [
    'first',
    'second',
  ])
  const children = await root.$$('span')
  assert.equal(children.length, 2)
  for (const child of children) {
    assert.equal(await child.evaluate((node, parent) => node.parentNode === parent, root), true)
    await child.dispose()
  }
  assert.equal(await root.$('#outside'), null)
  assert.deepEqual(await root.$$eval('.missing', (nodes) => nodes), [])
  await assert.rejects(
    root.$eval('.missing', (node) => node.id),
    /Failed to find element matching selector/,
  )
  checks.push(
    'scalar, array and retained descendant selectors query only the actual native root with first-match and empty-snapshot semantics',
  )
  await root.evaluate((node) => {
    if (!(node instanceof Element)) throw new Error('Actual element missing')
    const replacement = node.cloneNode(true)
    if (!(replacement instanceof Element)) throw new Error('Actual replacement element missing')
    replacement.querySelector('span').textContent = 'Replacement'
    node.replaceWith(replacement)
  })
  assert.equal(await root.$eval('span', (node) => node.textContent), 'First')
  assert.equal(await page.$eval('#scope span', (node) => node.textContent), 'Replacement')
  checks.push(
    'replacement and detachment preserve selectors on the original actual retained subtree',
  )
  await root.evaluate((node) => {
    if (!(node instanceof Element)) throw new Error('Actual element missing')
    node.querySelector('#shadow').attachShadow({ mode: 'open' }).innerHTML =
      '<b id="inside">Actual shadow child</b>'
  })
  assert.equal(await root.$eval('css=#shadow >> css=b', (node) => node.id), 'inside')
  const shadow = await root.evaluateHandle((node) => {
    if (!(node instanceof Element)) throw new Error('Actual element missing')
    return node.querySelector('#shadow').shadowRoot
  })
  const shadowRoot = shadow.asElement()
  assert(shadowRoot)
  assert.equal(await shadowRoot.$eval('b', (node) => node.id), 'inside')
  await shadow.dispose()
  checks.push(
    'real shadow traversal and retained ShadowRoot handles use native backend-node adoption',
  )
  const exact = 'selector\ud800\0\\"\udfff😀'
  await root.evaluate((node, id) => {
    if (!(node instanceof Element)) throw new Error('Actual element missing')
    node.querySelector('span').textContent = id
  }, exact)
  const tagObject = { __suitecutNativeUTF16Scalar: 'd800' }
  assert.deepEqual(
    await root.$eval(
      'text="' + exact.replace(/\\/gu, '\\\\').replace(/"/gu, '\\"') + '"',
      (node, { parent, value, tag }) => ({ same: node.parentNode === parent, value, tag }),
      { parent: root, value: exact, tag: tagObject },
    ),
    { same: true, value: exact, tag: tagObject },
  )
  checks.push(
    'isolated UTF16 selector/argument units, author tag objects and nested native handle arguments remain exact',
  )
  await page.evaluate(() => Reflect.set(globalThis, 'retainedAuthorMarker', true))
  const selectors = new NativeSelectors(source)
  await selectors.register('retainedMain', () => ({
    query(root, s) {
      return root.querySelector(s)
    },
    queryAll(root, s) {
      if (Reflect.get(globalThis, 'retainedAuthorMarker') !== true)
        throw new Error('Actual author realm missing')
      return root.querySelectorAll(s)
    },
  }))
  await selectors.register(
    'retainedUtility',
    () => ({
      query(root, s) {
        return root.querySelector(s)
      },
      queryAll(root, s) {
        if (Reflect.get(globalThis, 'retainedAuthorMarker') !== undefined)
          throw new Error('Actual isolated realm saw author marker')
        return root.querySelectorAll(s)
      },
    }),
    { contentScript: true },
  )
  for (const engine of ['retainedMain', 'retainedUtility'])
    assert.equal(
      await root.$eval(
        engine + '=span',
        (node) =>
          node instanceof Element && Reflect.get(globalThis, 'retainedAuthorMarker') === true,
      ),
      true,
    )
  checks.push(
    'custom selector factories keep their declared worlds over retained roots and callbacks receive actual main-world nodes',
  )
  await assert.rejects(
    root.$eval('unknown-engine=x', (node) => node.id),
    /Unknown engine/,
  )
  await assert.rejects(
    root.$$eval('##', (nodes) => nodes.length),
    /Unexpected token/,
  )
  await assert.rejects(
    root.$eval('span', () => {
      throw new Error('actual scoped callback failure')
    }),
    /actual scoped callback failure/,
  )
  await assert.rejects(
    source.runAutomation('handle', {
      action: 'create',
      query: [{ engine: 'css', selector: 'body' }],
      nativeQueryNodes: 'not-an-authorized-native-array',
      timeoutMs: 1000,
    }),
    /Invalid native selector-wait internal field/,
  )
  await assert.rejects(
    source.runAutomation('queryEvaluate', {
      nativeRootHandle: 'not-an-integer',
      query: [],
      all: true,
      function: 'function(nodes){return nodes.length;}',
    }),
    /Invalid native scoped selector request/,
  )
  await assert.rejects(
    source.runAutomation('queryEvaluate', {
      nativeRootHandle: root.id,
      nativeHandle: root.id,
      query: [],
      all: true,
      function: 'function(nodes){return nodes.length;}',
    }),
    /Invalid native scoped selector request/,
  )
  checks.push('native selector/callback errors and reserved-field rejection remain genuine')
  const value = await root.evaluateHandle(function (node, arg) {
    return { node, arg, receiver: this === globalThis }
  }, exact)
  const nodeProperty = await value.getProperty('node')
  assert.equal(await nodeProperty.evaluate((node, parent) => node === parent, root), true)
  assert.equal(
    await value.evaluate(
      (value) => !!value && typeof value === 'object' && Reflect.get(value, 'receiver') === true,
    ),
    true,
  )
  assert.equal(
    await value.evaluate((value, expected) => {
      if (!value || typeof value !== 'object') throw new Error('actual value missing')
      return Reflect.get(value, 'arg') === expected
    }, exact),
    true,
  )
  await nodeProperty.dispose()
  await value.dispose()
  await root.dispose()
  await assert.rejects(root.$('span'), /disposed|no object with guid/)
  checks.push(
    'JSHandle.evaluateHandle preserves genuine object/node identity and disposal invalidates scoped selectors',
  )
  await page.setContent(
    '<iframe srcdoc="<section id=scope><button id=child>Child</button></section>"></iframe>',
  )
  await page.frameLocator('iframe').locator('#child').waitFor({ timeout: 1000 })
  const frame = page.frames().find((frame) => frame.parentFrame())
  assert(frame)
  const childRoot = await frame.$('#scope')
  assert(childRoot)
  const child = await childRoot.$('button')
  assert(child)
  assert.equal(await child.evaluate((node) => node === document.querySelector('#child')), true)
  assert.equal(
    await childRoot.$eval('button', (node) => node === document.querySelector('#child')),
    true,
  )
  await child.dispose()
  await childRoot.dispose()
  checks.push('scoped child-frame handles and callbacks preserve genuine renderer-frame identity')
  await page.setContent('<section id="many"></section>')
  await page.evaluate(() => {
    for (let index = 0; index < 1025; index++)
      document.querySelector('#many').appendChild(document.createElement('span'))
  })
  const many = await page.$('#many')
  assert(many)
  await assert.rejects(many.$$('span'), /Native handle property limit exceeded/)
  assert.equal(await many.$$eval('span', (nodes) => nodes.length), 1025)
  checks.push(
    'bounded retained-handle enumeration rejects capacity while actual scalar array evaluation still observes the complete native result',
  )
  await many.evaluate((node) => {
    if (!(node instanceof Element)) throw new Error('actual root missing')
    node.replaceChildren(document.createElement('span'), document.createElement('span'))
  })
  const abort = new AbortController()
  const projected = createNativePage(source, abort.signal)
  const projectedRoot = new NativeElementHandle(projected, many.id, 'JSHandle@actual retained root')
  const pending = projectedRoot.$$eval('span', async (nodes) => {
    Reflect.set(globalThis, 'actualScopedCallbackStarted', true)
    await new Promise((resolve) => setTimeout(resolve, 250))
    Reflect.set(globalThis, 'actualScopedCallbackFinished', true)
    return nodes.length
  })
  const rejected = assert.rejects(pending, /abort|cancel/i)
  for (let attempt = 0; attempt < 100; attempt++) {
    if (await page.evaluate(() => Reflect.get(globalThis, 'actualScopedCallbackStarted') === true))
      break
    await new Promise((resolve) => setTimeout(resolve, 10))
  }
  assert.equal(
    await page.evaluate(() => Reflect.get(globalThis, 'actualScopedCallbackStarted') === true),
    true,
  )
  abort.abort()
  await rejected
  for (let attempt = 0; attempt < 100; attempt++) {
    if (await page.evaluate(() => Reflect.get(globalThis, 'actualScopedCallbackFinished') === true))
      break
    await new Promise((resolve) => setTimeout(resolve, 10))
  }
  assert.equal(
    await page.evaluate(() => Reflect.get(globalThis, 'actualScopedCallbackFinished') === true),
    true,
  )
  assert.equal(await many.$$eval('span', (nodes) => nodes.length), 2)
  await many.dispose()
  checks.push(
    'AbortSignal projections cancel native scoped callbacks while the canonical retained root survives and late actual browser completion releases temporary ownership',
  )
  await page.setContent('<section id="cleanup"><span>A</span><span>B</span></section>')
  for (const kind of ['scoped', 'locator', 'properties']) {
    const gate = new AbortController(),
      projection = createNativePage(source, gate.signal)
    const cleanupRoot =
      kind === 'properties'
        ? await projection.evaluateHandle('({a:{},b:{},c:{}})')
        : await projection.$('#cleanup')
    assert(cleanupRoot)
    const before = z
      .object({ activeHandles: z.number() })
      .parse(await source.runAutomation('diagnostics'))
    const original = projection.runAutomation.bind(projection)
    let properties = 0,
      aborted = false
    projection.runAutomation = async (command, params = {}, signal) => {
      if (
        command === 'handle' &&
        !aborted &&
        ((kind !== 'properties' && params.action === 'dispose') ||
          (kind === 'properties' && params.action === 'property' && ++properties === 2))
      ) {
        aborted = true
        gate.abort(new Error('Actual caller cancellation at ' + kind + ' cleanup boundary'))
      }
      return original(command, params, signal)
    }
    if (kind === 'scoped') {
      assert(cleanupRoot instanceof NativeElementHandle)
      await assert.rejects(cleanupRoot.$$('span'), /Actual caller cancellation/)
    } else if (kind === 'locator')
      await assert.rejects(
        projection.locator('span').elementHandles(),
        /Actual caller cancellation/,
      )
    else await assert.rejects(cleanupRoot.getProperties(), /Actual caller cancellation/)
    assert.equal(aborted, true)
    const after = z
      .object({
        activeHandles: z.number(),
        nativeHandleDisposals: z.number(),
        nativeHandleDisposalPhysical: z.number(),
        nativeHandleDisposalHandles: z.number(),
      })
      .parse(await source.runAutomation('diagnostics'))
    assert.equal(after.activeHandles, before.activeHandles)
    assert.equal(
      after.nativeHandleDisposals +
        after.nativeHandleDisposalPhysical +
        after.nativeHandleDisposalHandles,
      0,
    )
    await page.runAutomation('handle', { action: 'dispose', handle: cleanupRoot.id })
  }
  checks.push(
    'actual caller cancellation at scoped/locator temporary-array disposal and partial property lookup cleans every undelivered native handle through the live source',
  )
  const saturationRoot = await page.$('#cleanup')
  assert(saturationRoot)
  for (const handles of [
    [],
    [saturationRoot.id, saturationRoot.id],
    ['not-an-id'],
    Array.from({ length: 1025 }, (_, index) => index + 1),
  ])
    await assert.rejects(
      source.runAutomation('handle', { action: 'disposeMany', handles }),
      /Invalid native handle disposal batch/,
    )
  let startedCount = 0
  const started = latch(),
    release = latch()
  const capacityBinding = await page.exposeFunction('retainedCleanupCapacity', async () => {
    if (++startedCount === 64) started.open()
    await release.promise
    return true
  })
  const evaluations = Array.from({ length: 64 }, () => page.evaluate('retainedCleanupCapacity()'))
  await started.promise
  assert.equal(startedCount, 64)
  await saturationRoot.dispose()
  release.open()
  assert.deepEqual(
    await Promise.all(evaluations),
    Array.from({ length: 64 }, () => true),
  )
  await capacityBinding.dispose()
  checks.push(
    'genuine native handle disposal uses reserved control admission while64 real renderer evaluations await host callbacks',
  )
  await page.setContent('<section id="held"></section>')
  await page.evaluate(() => {
    for (let index = 0; index < 100; index++)
      document.querySelector('#held').appendChild(document.createElement('span'))
  })
  const heldGate = new AbortController(),
    heldProjection = createNativePage(source, heldGate.signal),
    heldRoot = await heldProjection.$('#held')
  assert(heldRoot)
  const holdStarted = latch()
  const holdBinding = await page.exposeFunction('retainedCleanupHoldStarted', () =>
    holdStarted.open(),
  )
  const heldOriginal = heldProjection.runAutomation.bind(heldProjection)
  let held = false,
    busy = page.evaluate('undefined')
  await busy
  heldProjection.runAutomation = async (command, params = {}, signal) => {
    if (command === 'handle' && params.action === 'dispose' && !held) {
      held = true
      busy = page.evaluate(
        '(()=>{globalThis.retainedCleanupHoldStarted();const end=Date.now()+3500;while(Date.now()<end){};})()',
      )
      await holdStarted.promise
      began = Date.now()
      heldGate.abort(new Error('Actual caller abort while renderer release is held'))
    }
    return heldOriginal(command, params, signal)
  }
  let began = 0
  let cleanupError
  try {
    await heldRoot.$$('span')
    assert.fail('Actual held cleanup must reject')
  } catch (error) {
    cleanupError = error
  }
  assert(cleanupError instanceof AggregateError)
  assert(cleanupError.errors[0] instanceof Error)
  assert.match(cleanupError.errors[0].message, /Actual caller abort/)
  assert.equal(cleanupError.errors.length, 2)
  const abortElapsed = Date.now() - began
  assert(
    abortElapsed >= 900 && abortElapsed < 1800,
    'Caller cancellation must publish before the real3500ms renderer release',
  )
  const heldState = z
    .object({
      nativeHandleDisposals: z.number(),
      nativeHandleDisposalPhysical: z.number(),
      nativeHandleDisposalHandles: z.number(),
      activeHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(heldState.nativeHandleDisposals, 1)
  assert.equal(heldState.nativeHandleDisposalPhysical, 1)
  assert.equal(heldState.nativeHandleDisposalHandles, 101)
  assert.equal(heldState.activeHandles, 102)
  await busy
  for (let attempt = 0; attempt < 500; attempt++) {
    const physical = z
      .object({ nativeHandleDisposals: z.number() })
      .parse(await source.runAutomation('diagnostics'))
    if (!physical.nativeHandleDisposals) break
    await new Promise((resolve) => setTimeout(resolve, 10))
  }
  const settled = z
    .object({
      nativeHandleDisposals: z.number(),
      nativeHandleDisposalPhysical: z.number(),
      nativeHandleDisposalHandles: z.number(),
      nativeHandleDisposalFailures: z.number(),
      activeHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.deepEqual(settled, {
    nativeHandleDisposals: 0,
    nativeHandleDisposalPhysical: 0,
    nativeHandleDisposalHandles: 0,
    nativeHandleDisposalFailures: 0,
    activeHandles: 1,
  })
  await page.runAutomation('handle', { action: 'dispose', handle: heldRoot.id })
  await holdBinding.dispose()
  checks.push(
    'one101-handle native cleanup batch retains actual group metadata and one physical release past a bounded caller timeout, then genuinely drains every ACK',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      nativeSelectorWaits: z.number(),
      nativeSelectorWaitPhysical: z.number(),
      nativeHandleDisposals: z.number(),
      nativeHandleDisposalPhysical: z.number(),
      nativeHandleDisposalHandles: z.number(),
      nativeHandleDisposalFailures: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  const report = {
    cleanupOwnership: { held: heldState, settled, abortElapsed },
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks,
    state,
  }
  await writeFile(directory + '/report.json', JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
