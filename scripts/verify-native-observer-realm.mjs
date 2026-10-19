// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import {
  createNativePage,
  expectNative,
  launchNativeBrowser,
  NativeSelectors,
} from '../dist/native.js'

const directory = resolve('.suitecut/native-observer-realm')
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
  const selectors = new NativeSelectors(source)
  await page.setContent(
    '<button>Actually rendered</button><div role="searchbox" aria-readonly="true">Search</div>',
  )
  await page.evaluate(() => Reflect.set(globalThis, 'actualSelectorMain', true))
  await selectors.register('observerMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (Reflect.get(globalThis, 'actualSelectorMain') !== true)
        throw new Error('Main selector lost actual author world')
      return root.querySelectorAll(selector)
    },
  }))
  const custom = page.locator('observerMain=button')
  const handle = await custom.elementHandle()
  assert(handle)
  await page.evaluate(() => {
    globalThis.getComputedStyle = () => ({ display: 'none', visibility: 'hidden' })
    Element.prototype.checkVisibility = () => false
  })
  assert.equal(await custom.count(), 1)
  assert.equal(await page.locator('button').isVisible(), true)
  assert.equal(await custom.isVisible(), true)
  assert.equal(await handle.isVisible(), true)
  await expectNative(custom).toBeVisible({ timeout: 1000 })
  await expectNative(custom).toHaveAccessibleName('Actually rendered', { timeout: 1000 })
  assert.equal(await custom.filter({ visible: true }).isVisible(), true)
  assert.equal(
    await custom.evaluate(
      (node) =>
        node === document.querySelector('button') &&
        Reflect.get(globalThis, 'actualSelectorMain') === true,
    ),
    true,
  )
  checks.push(
    'custom selection retains main realm while native visibility/name metadata and native visible filter observe actual isolated adopted nodes',
  )
  await page.evaluate(() => {
    document.querySelector('button').style.visibility = 'hidden'
    globalThis.getComputedStyle = () => ({ display: 'block', visibility: 'visible' })
    Element.prototype.checkVisibility = () => true
  })
  assert.equal(await custom.isVisible(), false)
  await expectNative(custom).toBeHidden({ timeout: 1000 })
  checks.push(
    'author-forged visible styles cannot expose a genuinely hidden custom-selected native node',
  )
  const searchbox = await page.getByRole('searchbox').elementHandle()
  assert(searchbox)
  assert.equal(await searchbox.isEditable(), false)
  await page.evaluate(() =>
    document.querySelector('[role=searchbox]').setAttribute('aria-readonly', 'false'),
  )
  assert.equal(await searchbox.isEditable(), true)
  await searchbox.waitForElementState('editable', { timeout: 1000 })
  await searchbox.dispose()
  await handle.dispose()
  checks.push(
    'compiled searchbox inherits readonly support and fixed native states follow actual aria-readonly changes',
  )
  await page.setContent('<button>One</button><button>Two</button>')
  await assert.rejects(page.locator('observerMain=button').isVisible(), /strict mode violation/)
  await assert.rejects(page.locator('hey=what').isVisible(), /Unknown engine/)
  checks.push('strictness and malformed selector errors remain real native failures')
  await page.setContent(
    "<iframe srcdoc=\"<button>Child</button><script>globalThis.actualSelectorMain=true;globalThis.getComputedStyle=()=>({display:'none',visibility:'hidden'});Element.prototype.checkVisibility=()=>false</script>\"></iframe>",
  )
  const child = page.frameLocator('iframe').locator('observerMain=button')
  assert.equal(await child.isVisible(), true)
  await expectNative(child).toHaveAccessibleName('Child', { timeout: 1000 })
  checks.push(
    'actual child-frame custom selector and isolated observer preserve their genuine owning renderer frame',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
      nativeElementStates: z.number(),
      nativeElementStatePhysical: z.number(),
      nativeElementStateReleases: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  for (const [key, value] of Object.entries(state)) assert.equal(value, 0, key)
  const report = {
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
