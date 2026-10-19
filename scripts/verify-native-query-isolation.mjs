// SPDX-License-Identifier: MIT
/* global document, Element, setTimeout */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeSelectors } from '../dist/native.js'

const directory = '.suitecut/native-query-isolation'
await mkdir(directory, { recursive: true })
let stderr = ''
const source = await launchNativeBrowser({
  executablePath: z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE),
  headless: true,
  onDiagnostic: (message) => {
    stderr += message
    assert(stderr.length < 1048576)
  },
})
const checks = []
/** A native callback coordination latch with no renderer result substitution. */
function latch() {
  const gate = new AbortController()
  return {
    open: () => gate.abort(),
    promise: new Promise((resolve) =>
      gate.signal.addEventListener('abort', () => resolve(undefined), { once: true }),
    ),
  }
}
try {
  const page = createNativePage(source)
  await page.setContent(
    '<section id="root"><button id="one">One</button><button id="two">Two</button></section>',
  )
  await page.evaluate(() => {
    Reflect.set(globalThis, 'queryIsolationAuthorMarker', true)
    Element.prototype.matches = function () {
      return false
    }
  })
  assert.deepEqual(
    await page.$$eval('button', (nodes) => {
      const ids = []
      for (const node of nodes) ids.push(node.id)
      return ids
    }),
    ['one', 'two'],
  )
  assert.equal(await page.$eval('button', (node) => node.id), 'one')
  assert.equal(await page.locator('button').count(), 2)
  checks.push(
    'builtins select the actual native nodes in utility despite author Element.matches replacement',
  )
  assert.equal(
    await page
      .locator('#one')
      .evaluate(
        (node) =>
          node === document.querySelector('#one') &&
          node instanceof Element &&
          Reflect.get(globalThis, 'queryIsolationAuthorMarker') === true,
      ),
    true,
  )
  assert.deepEqual(
    await page.locator('button').evaluateAll((nodes) => {
      const ids = []
      for (const node of nodes) ids.push(node.id)
      return { ids, main: Reflect.get(globalThis, 'queryIsolationAuthorMarker') === true }
    }),
    { ids: ['one', 'two'], main: true },
  )
  checks.push(
    'genuine author callbacks receive exact main-world DOM wrappers and author global identity after utility selection',
  )
  const immediate = await page.$('button')
  assert(immediate)
  assert.equal(await immediate.evaluate((node) => node === document.querySelector('#one')), true)
  await immediate.dispose()
  const strict = await page.locator('#two').elementHandle()
  assert(strict)
  assert.equal(await strict.evaluate((node) => node === document.querySelector('#two')), true)
  await strict.dispose()
  const evaluated = await page.locator('#one').evaluateHandle((node) => ({
    node,
    main: Reflect.get(globalThis, 'queryIsolationAuthorMarker') === true,
  }))
  assert.equal(
    await evaluated.evaluate(
      (value) =>
        !!value &&
        typeof value === 'object' &&
        Reflect.get(value, 'node') === document.querySelector('#one') &&
        Reflect.get(value, 'main') === true,
    ),
    true,
  )
  await evaluated.dispose()
  checks.push(
    'immediate, strict and evaluateHandle construction retain the actual selected node and original author callback context',
  )
  const selectors = new NativeSelectors(source)
  await selectors.register('isolationMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'queryIsolationAuthorMarker') !== true)
        throw new Error('Actual main realm missing')
      return root.querySelectorAll(selector)
    },
  }))
  await selectors.register(
    'isolationUtility',
    () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (Reflect.get(globalThis, 'queryIsolationAuthorMarker') !== undefined)
          throw new Error('Actual utility realm missing')
        return root.querySelectorAll(selector)
      },
    }),
    { contentScript: true },
  )
  for (const engine of ['isolationMain', 'isolationUtility'])
    assert.deepEqual(
      await page.$$eval(engine + '=section >> button', (nodes) => {
        const ids = []
        for (const node of nodes) ids.push(node.id)
        return ids
      }),
      ['one', 'two'],
    )
  checks.push(
    'custom factories execute in their declared real worlds while subsequent builtin selection and final callback adoption remain isolated',
  )
  await selectors.register(
    'isolationDetached',
    () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        const node = root.querySelector(selector)
        if (!node) return []
        const clone = node.cloneNode(true)
        clone.textContent = 'Replacement'
        node.replaceWith(clone)
        return [node]
      },
    }),
    { contentScript: true },
  )
  assert.deepEqual(
    await page.$eval('isolationDetached=#two', (node) => ({
      same: node === document.querySelector('#two'),
      connected: node.isConnected,
      text: node.textContent,
    })),
    { same: false, connected: false, text: 'Two' },
  )
  assert.equal(await page.$eval('#two', (node) => node.textContent), 'Replacement')
  checks.push(
    'native backend adoption retains a genuine detached selected node rather than reacquiring an application replacement',
  )
  await page.evaluate(() => {
    const root = document.querySelector('#root')
    for (let i = 0; i < 1025; i++) {
      const node = document.createElement('i')
      node.id = 'many-' + i
      root.appendChild(node)
    }
  })
  const all = await page.$$eval('i', (nodes) => {
    const ids = []
    for (const node of nodes) {
      if (node.ownerDocument !== document) throw new Error('Actual main document wrapper missing')
      ids.push(node.id)
    }
    return ids
  })
  assert.deepEqual(
    all,
    Array.from({ length: 1025 }, (_, index) => 'many-' + index),
  )
  checks.push(
    'native actual-node adoption spans128-node transfer batches and1024 nodes without losing order or callback realm',
  )
  await page.evaluate(() => {
    Reflect.set(globalThis, 'nativeArrayInheritedWrites', 0)
    Object.defineProperty(Array.prototype, '0', {
      configurable: true,
      set() {
        Reflect.set(
          globalThis,
          'nativeArrayInheritedWrites',
          Number(Reflect.get(globalThis, 'nativeArrayInheritedWrites')) + 1,
        )
      },
    })
  })
  assert.equal(
    await page
      .locator('i')
      .evaluateAll(
        (nodes) =>
          nodes.length === 1025 &&
          nodes[0] === document.getElementById('many-0') &&
          nodes[1024] === document.getElementById('many-1024') &&
          Reflect.get(globalThis, 'nativeArrayInheritedWrites') === 0,
      ),
    true,
  )
  await page.evaluate(() => {
    Reflect.deleteProperty(Array.prototype, '0')
  })
  checks.push(
    'actual destination node arrays own every native slot and cannot be rewritten by author numeric prototype setters',
  )
  await page.evaluate(() => {
    setTimeout(() => {
      const node = document.createElement('b')
      node.id = 'later'
      document.body.appendChild(node)
    }, 60)
  })
  assert.equal(
    await page.locator('#later').evaluate((node) => node === document.querySelector('#later')),
    true,
  )
  page.setDefaultTimeout(65)
  await assert.rejects(
    page.locator('#never').evaluate((node) => node.id),
    /Timeout|timed out/,
  )
  page.setDefaultTimeout(30000)
  checks.push(
    'missing builtin queries release prior actual groups before retry and retain the original native deadline',
  )
  const abort = new AbortController(),
    projected = createNativePage(source, abort.signal),
    started = latch(),
    release = latch()
  let called = 0
  const binding = await page.exposeFunction('queryIsolationCapacity', async () => {
    if (++called === 64) started.open()
    await release.promise
    return true
  })
  const pending = Array.from({ length: 64 }, () =>
    projected.runAutomation('queryEvaluate', {
      selector: '#one',
      query: [{ engine: 'css', selector: '#one' }],
      function:
        "async function(node){if(node.id!=='one')throw new Error('Actual selected target missing');await globalThis.queryIsolationCapacity();return ['primitive',true];}",
    }),
  )
  const outcomes = pending.map((promise) =>
    promise.then(
      (value) => ({ value }),
      (error) => ({
        error:
          error instanceof Error
            ? error
            : new Error('Actual native callback rejected with a non-error', { cause: error }),
      }),
    ),
  )
  const premature = Promise.race(outcomes).then((result) => {
    throw new Error('Actual callback settled before capacity was admitted', { cause: result })
  })
  const deadline = AbortSignal.timeout(8000),
    expired = new Promise((resolve, reject) =>
      deadline.addEventListener(
        'abort',
        () => reject(new Error('Actual native callback capacity admission timed out')),
        { once: true },
      ),
    )
  await Promise.race([started.promise, premature, expired])
  assert.equal(called, 64)
  abort.abort(new Error('Actual query cancellation'))
  for (const result of await Promise.all(outcomes)) {
    assert('error' in result)
    assert.match(result.error.message, /Actual query cancellation/)
  }
  const held = z
    .object({
      nativeSelectorWaits: z.number(),
      nativeSelectorWaitPhysical: z.number(),
      activeHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.equal(held.nativeSelectorWaits, 64)
  assert.equal(held.nativeSelectorWaitPhysical, 64)
  assert.equal(held.activeHandles, 0)
  await assert.rejects(
    page.locator('#one').evaluate((node) => node.id),
    /selected-node physical preparation capacity/,
  )
  release.open()
  for (let attempt = 0; attempt < 500; attempt++) {
    const state = z
      .object({ nativeSelectorWaits: z.number() })
      .parse(await source.runAutomation('diagnostics'))
    if (!state.nativeSelectorWaits) break
    await delay(5)
  }
  await binding.dispose()
  assert.equal(
    await page.locator('#one').evaluate((node) => node === document.querySelector('#one')),
    true,
  )
  checks.push(
    '64 genuine canceled author callbacks retain their physical quota until actual host completion and native ACKs; fresh selection admission is bounded and recovers afterward',
  )
  for (let attempt = 0; attempt < 200; attempt++) {
    const state = z
      .object({ nativeSelectorWaits: z.number() })
      .parse(await source.runAutomation('diagnostics'))
    if (!state.nativeSelectorWaits) break
    await delay(5)
  }
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      nativeSelectorWaits: z.number(),
      nativeSelectorWaitPhysical: z.number(),
      bindingCalls: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  checks.push('all actual query/callback/handle/group and physical ownership returns to zero')
  await writeFile(
    directory + '/report.json',
    JSON.stringify({ checks, state, held }, null, 2) + '\n',
  )
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
