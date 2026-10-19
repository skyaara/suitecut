// SPDX-License-Identifier: MIT
/* global document, setTimeout, queueMicrotask, performance, HTMLElement */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeSelectors } from '../dist/native.js'

const directory = resolve('.suitecut/native-selector-wait')
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
  await page.setContent('<div id="target" style="display:none">Actual hidden</div>')
  const attached = await page.waitForSelector('#target', { state: 'attached', timeout: 1000 })
  assert(attached)
  assert.equal(await attached.isVisible(), false)
  await page.evaluate(() =>
    setTimeout(() => {
      const node = document.querySelector('#target')
      if (!(node instanceof HTMLElement)) throw new Error('actual target missing')
      node.style.display = 'block'
    }, 30),
  )
  const visible = await page.waitForSelector('#target', { timeout: 1500 })
  assert(visible)
  assert.equal(await visible.evaluate((node, other) => node === other, attached), true)
  await attached.dispose()
  await visible.dispose()
  checks.push(
    'attached and default visible waits retain the same actual hidden-to-visible DOM node',
  )
  await page.setContent(
    '<span class="many" style="display:none">First</span><span class="many">Second</span>',
  )
  await assert.rejects(
    page.waitForSelector('.many', { strict: true, state: 'attached', timeout: 1000 }),
    /strict mode violation/,
  )
  await assert.rejects(page.waitForSelector('.many', { timeout: 80 }), /Timeout 80ms exceeded/)
  const first = await page.waitForSelector('.many', { state: 'attached', timeout: 1000 })
  assert(first)
  assert.equal(await first.textContent(), 'First')
  await first.dispose()
  checks.push(
    'default nonstrict selection uses the first actual match while strict mode rejects multiplicity before visibility',
  )
  assert.equal(await page.waitForSelector('.absent', { state: 'detached', timeout: 1000 }), null)
  assert.equal(await page.waitForSelector('.absent', { state: 'hidden', timeout: 1000 }), null)
  await page.evaluate(() =>
    setTimeout(() => document.querySelectorAll('.many').forEach((node) => node.remove()), 30),
  )
  assert.equal(await page.waitForSelector('.many', { state: 'detached', timeout: 1000 }), null)
  await assert.rejects(
    page.waitForSelector('unknown-engine=x', { timeout: 1000 }),
    /Unknown engine/,
  )
  await assert.rejects(page.waitForSelector('##', { timeout: 1000 }), /Unexpected token/)
  checks.push(
    'hidden and detached return null directly and genuine selector failures propagate without false success',
  )
  const selectors = new NativeSelectors(source)
  await page.evaluate(() => Reflect.set(globalThis, 'waitAuthorMarker', true))
  await selectors.register('waitMain', () => ({
    query(root, s) {
      return root.querySelector(s)
    },
    queryAll(root, s) {
      if (Reflect.get(globalThis, 'waitAuthorMarker') !== true)
        throw new Error('actual main world marker missing')
      return root.querySelectorAll(s)
    },
  }))
  await selectors.register(
    'waitUtility',
    () => ({
      query(root, s) {
        return root.querySelector(s)
      },
      queryAll(root, s) {
        if (Reflect.get(globalThis, 'waitAuthorMarker') !== undefined)
          throw new Error('actual isolated world saw author marker')
        return root.querySelectorAll(s)
      },
    }),
    { contentScript: true },
  )
  await page.evaluate(() =>
    setTimeout(
      () => document.body.insertAdjacentHTML('beforeend', '<button id="later">Later</button>'),
      30,
    ),
  )
  for (const engine of ['waitMain', 'waitUtility']) {
    const node = await page.waitForSelector(engine + '=#later', { timeout: 1500 })
    assert(node)
    assert.equal(
      await node.evaluate(
        (node) =>
          node === document.querySelector('#later') &&
          Reflect.get(globalThis, 'waitAuthorMarker') === true,
      ),
      true,
    )
    await node.dispose()
  }
  checks.push(
    'real custom factories retain declared execution worlds and returned nodes are genuinely adopted to the main world',
  )
  await page.evaluate(() =>
    setTimeout(
      () =>
        document.body.insertAdjacentHTML('beforeend', '<div id="element-later">Fresh query</div>'),
      30,
    ),
  )
  const later = await page.locator('waitUtility=#element-later').elementHandle({ timeout: 1500 })
  assert(later)
  assert.equal(await later.textContent(), 'Fresh query')
  await later.dispose()
  checks.push(
    'empty isolated element-handle attempts rerun genuine selection rather than retaining a stale array snapshot',
  )
  await page.setContent('<iframe srcdoc="<span id=child>Child</span>"></iframe>')
  await page.frameLocator('iframe').locator('#child').waitFor({ timeout: 1000 })
  const frame = page.frames().find((frame) => frame.parentFrame())
  assert(frame)
  const child = await frame.waitForSelector('#child', { timeout: 1000 })
  assert(child)
  assert.equal(await child.evaluate((node) => node === document.querySelector('#child')), true)
  await child.dispose()
  checks.push('frame waits bind the actual returned node to its genuine child renderer frame')
  const detached = frame.waitForSelector('#never', { timeout: 2000 })
  const detachedResult = assert.rejects(detached, /detached|destroyed/)
  await page.evaluate(() => document.querySelector('iframe').remove())
  await detachedResult
  checks.push(
    'a pending wait for a fixed child frame fails on genuine frame destruction and releases its original ownership',
  )
  await page.setContent('<iframe srcdoc="<p>Recovered child</p>"></iframe>')
  const navigationWait = page.waitForSelector('#navigated', { timeout: 3000 })
  await page.goto(
    'data:text/html;charset=utf-8,' +
      encodeURIComponent('<p id="navigated">Actual new document</p>'),
  )
  const navigated = await navigationWait
  assert(navigated)
  assert.equal(
    await navigated.evaluate((node) => node === document.querySelector('#navigated')),
    true,
  )
  await navigated.dispose()
  checks.push(
    'main-frame selector waits survive genuine document navigation while adopting only nodes from the new document',
  )
  await page.setContent('<iframe srcdoc="<p>Recovered child</p>"></iframe>')
  const abort = new AbortController()
  const projected = createNativePage(source, abort.signal)
  const waiting = projected.waitForSelector('#never', { timeout: 5000 })
  setTimeout(() => abort.abort(), 40)
  await assert.rejects(waiting, /abort|cancel/i)
  await assert.rejects(page.waitForSelector('#never', { timeout: 30 }), /Timeout|timed out/)
  const recovered = await page.waitForSelector('iframe', { timeout: 1000 })
  assert(recovered)
  await recovered.dispose()
  checks.push(
    'logical cancellation and original native timeout settle while later selector waits recover',
  )
  await page.setContent('<button id="race">Original retained node</button>')
  await selectors.register('waitRace', () => ({
    query(root, s) {
      return root.querySelector(s)
    },
    queryAll(root, s) {
      const node = root.querySelector(s)
      if (node?.textContent === 'Original retained node')
        queueMicrotask(() => {
          const replacement = node.cloneNode(true)
          replacement.textContent = 'Actual replacement'
          node.replaceWith(replacement)
        })
      return node ? [node] : []
    },
  }))
  const original = await page.waitForSelector('waitRace=#race', {
    state: 'attached',
    timeout: 1000,
  })
  assert(original)
  assert.equal(await original.textContent(), 'Original retained node')
  assert.equal(await original.evaluate((node) => node.isConnected === true), false)
  assert.equal(await page.locator('#race').textContent(), 'Actual replacement')
  await original.dispose()
  checks.push(
    'atomic attached wait returns the exact selected original node when a genuine queued DOM replacement occurs before handle serialization',
  )
  const saturation = await Promise.allSettled(
    Array.from({ length: 64 }, () => page.waitForSelector('#never', { timeout: 500 })),
  )
  assert.equal(saturation.length, 64)
  for (const entry of saturation) {
    assert.equal(entry.status, 'rejected')
    if (entry.status === 'rejected')
      assert.match(z.instanceof(Error).parse(entry.reason).message, /Timeout|timed out/)
  }
  checks.push(
    'sixty-four genuinely concurrent selector waits reach their original native deadlines without capacity false positives',
  )
  const heldAbort = new AbortController()
  const heldPage = createNativePage(source, heldAbort.signal)
  const busy = page.evaluate(() => {
    const start = performance.now()
    while (performance.now() - start < 500) {
      continue
    }
    return true
  })
  const held = Array.from({ length: 16 }, () =>
    heldPage.waitForSelector('#never', { timeout: 3000 }),
  )
  const heldSettled = Promise.allSettled(held)
  await new Promise((resolve) => setTimeout(resolve, 40))
  heldAbort.abort()
  const canceled = await heldSettled
  for (const entry of canceled) assert.equal(entry.status, 'rejected')
  const during = z
    .object({ nativeSelectorWaits: z.number(), nativeSelectorWaitPhysical: z.number() })
    .parse(await source.runAutomation('diagnostics'))
  assert(
    during.nativeSelectorWaits > 0,
    'actual canceled owners must remain admitted until real renderer ACK',
  )
  assert(
    during.nativeSelectorWaitPhysical > 0,
    'actual protocol phases must remain physically owned after logical abort',
  )
  assert.equal(await busy, true)
  let recoveredState
  for (let attempt = 0; attempt < 100; attempt++) {
    recoveredState = z
      .object({ nativeSelectorWaits: z.number(), nativeSelectorWaitPhysical: z.number() })
      .parse(await source.runAutomation('diagnostics'))
    if (!recoveredState.nativeSelectorWaits && !recoveredState.nativeSelectorWaitPhysical) break
    await new Promise((resolve) => setTimeout(resolve, 10))
  }
  assert.deepEqual(recoveredState, { nativeSelectorWaits: 0, nativeSelectorWaitPhysical: 0 })
  checks.push(
    'actual canceled renderer-blocked waits retain native admission and physical commands until genuine ACK, then release both ledgers',
  )
  await assert.rejects(
    source.runAutomation('handle', {
      action: 'create',
      query: [{ engine: 'selector-wait', state: 'attached', strict: false }],
      selectorState: 'attached',
      timeoutMs: 1000,
    }),
    /Invalid native locator query step/,
  )
  await assert.rejects(
    source.runAutomation('handle', {
      action: 'create',
      query: [{ engine: 'css', selector: 'body' }],
      nativeSelectorWaitState: 'attached',
      timeoutMs: 1000,
    }),
    /Invalid native selector-wait internal field/,
  )
  checks.push(
    'public requests cannot inject the private native wait gate or its internal Source field',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      nativeSelectorWaits: z.number(),
      nativeSelectorWaitPhysical: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  const report = {
    physicalAfterLogicalCancel: during,
    physicalAfterActualAck: recoveredState,
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
