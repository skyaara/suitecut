// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser, NativeSelectors } from '../dist/native.js'

const directory = resolve('.suitecut/native-legacy-eval-selectors')
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
    '<span id="first">First</span><span id="second">Second</span><div id="host"></div>',
  )
  await page.evaluate(() => {
    document.querySelector('#host').attachShadow({ mode: 'open' }).innerHTML =
      '<span id="shadow">Shadow text</span>'
    Reflect.set(globalThis, 'actualMainMarker', true)
  })
  assert.equal(await page.$eval('css=span', (node) => node.id), 'first')
  assert.equal(await page.$eval('css=#host >> css=span', (node) => node.textContent), 'Shadow text')
  assert.deepEqual(await page.$$eval('css=span', (nodes) => nodes.map((node) => node.id)), [
    'first',
    'second',
    'shadow',
  ])
  checks.push(
    'legacy scalar and array evaluation use full native CSS prefixes, shadow traversal, chaining, document ordering and first-match scalar semantics',
  )
  assert.equal(await page.$eval('text=Shadow text', (node) => node.id), 'shadow')
  assert.equal(await page.$eval('id=first', (node) => node.textContent), 'First')
  assert.equal(await page.$eval('"Second"', (node) => node.id), 'second')
  checks.push('real native text, id and quoted text selectors reach the same actual DOM nodes')
  assert.equal(await page.$$eval('css=.missing', (nodes) => nodes.length), 0)
  await assert.rejects(
    page.$eval('css=.missing', (node) => node.id),
    /No element matches css=.missing/,
  )
  await assert.rejects(
    page.$eval('missing-engine=foo', (node) => node.id),
    /Unknown engine/,
  )
  await assert.rejects(
    page.$eval('css=span', () => {
      throw new Error('actual callback failure')
    }),
    /actual callback failure/,
  )
  checks.push('empty snapshots and genuine selector/callback failures retain their public outcomes')
  const handle = await page.locator('#shadow').elementHandle()
  assert(handle)
  const value = 'actual\ud800\0\\"\udfff😀'
  assert.equal(
    await page.$eval(
      'css=#shadow',
      (node, { same, value }) =>
        node === same && Reflect.get(globalThis, 'actualMainMarker') === true ? value : null,
      { same: handle, value },
    ),
    value,
  )
  await handle.dispose()
  checks.push(
    'nested native handle arguments keep real node identity and typed values retain exact isolated UTF16 units',
  )
  const selectors = new NativeSelectors(source)
  await selectors.register('legacyMain', () => ({
    query(root, s) {
      return root.querySelector(s)
    },
    queryAll(root, s) {
      if (Reflect.get(globalThis, 'actualMainMarker') !== true)
        throw new Error('actual main selector marker missing')
      return root.querySelectorAll(s)
    },
  }))
  await selectors.register(
    'legacyUtility',
    () => ({
      query(root, s) {
        return root.querySelector(s)
      },
      queryAll(root, s) {
        if (Reflect.get(globalThis, 'actualMainMarker') !== undefined)
          throw new Error('actual utility selector observed author state')
        return root.querySelectorAll(s)
      },
    }),
    { contentScript: true },
  )
  for (const engine of ['legacyMain', 'legacyUtility'])
    assert.equal(
      await page.$eval(
        engine + '=#first',
        (node) =>
          node === document.querySelector('#first') &&
          node instanceof Element &&
          Reflect.get(globalThis, 'actualMainMarker') === true,
      ),
      true,
    )
  checks.push(
    'declared main and isolated custom selection keep their worlds while scalar callbacks use genuinely adopted main-world nodes',
  )
  await page.setContent('<iframe srcdoc="<span id=child>Child</span>"></iframe>')
  await page.frameLocator('iframe').locator('#child').waitFor({ timeout: 1000 })
  const frame = page.frames().find((frame) => frame.parentFrame())
  assert(frame)
  assert.equal(
    await frame.$eval('css=span', (node) => node === document.querySelector('#child')),
    true,
  )
  assert.deepEqual(await frame.$$eval('text=Child', (nodes) => nodes.map((node) => node.id)), [
    'child',
  ])
  checks.push(
    'inherited frame scalar and array selector evaluation retain exact native child-frame identity',
  )
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
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
