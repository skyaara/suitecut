// SPDX-License-Identifier: MIT
/* global document, Element, window */
import assert from 'node:assert/strict'
import console from 'node:console'
import { createHash } from 'node:crypto'
import { mkdir, readFile, rm, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'
import process from 'node:process'

import * as z from 'zod'

import { createNativePage, NativeSelectors, launchNativeBrowser } from '../dist/native.js'

const directory = resolve('.suitecut/native-selector-mixed-worlds')
await mkdir(directory, { recursive: true })
await rm(`${directory}/report.json`, { force: true })
const executablePath = z.string().parse(process.env.SUITECUT_NATIVE_EXECUTABLE)
let diagnostics = ''
const source = await launchNativeBrowser({
  executablePath,
  width: 640,
  height: 480,
  headless: true,
  onDiagnostic: (message) => {
    if (diagnostics.length < 1024 * 1024)
      diagnostics += message.slice(0, 1024 * 1024 - diagnostics.length)
  },
})
const checks = []
try {
  const page = createNativePage(source)
  const selectors = new NativeSelectors(source)
  await selectors.register('mixedMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (window.nativeSelectorMarker !== 'main')
        throw new Error('Main selector ran in another world')
      return root.querySelectorAll(selector)
    },
  }))
  await selectors.register(
    'mixedIsolated',
    () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (window.nativeSelectorMarker !== undefined)
          throw new Error('Isolated selector observed author JavaScript')
        return root.querySelectorAll(selector)
      },
    }),
    { contentScript: true },
  )
  await page.setContent(`
    <section id=left><button id=one><span>First</span></button><button id=two>Second</button></section>
    <section id=right><button id=three><span>Third</span></button></section><section id=empty></section>
    <form id=forms><input id=account></form>
  `)
  await page.evaluate(() => {
    window.nativeSelectorMarker = 'main'
    document.querySelector('#one').onclick = () => {
      window.nativeSelectorClicked = true
    }
  })
  await selectors.register(
    'mixedStateful',
    () => {
      let calls = 0
      return {
        query(root, selector) {
          return root.querySelector(selector)
        },
        queryAll(root, selector) {
          calls++
          const nodes = root.querySelectorAll(selector)
          for (const node of nodes) {
            if (!(node instanceof Element)) throw new Error('Factory expected actual DOM elements')
            node.setAttribute('data-native-factory-calls', String(calls))
          }
          return nodes
        },
      }
    },
    { contentScript: true },
  )
  assert.equal(await page.locator('mixedStateful=#one').count(), 1)
  assert.equal(await page.locator('mixedStateful=#one').count(), 1)
  assert.equal(
    await page.evaluate(() =>
      document.querySelector('#one').getAttribute('data-native-factory-calls'),
    ),
    '2',
  )
  checks.push('native custom factory closure state persists in its actual frame execution world')
  const forward = page.locator('mixedMain=#left >> mixedIsolated=button')
  const reverse = page.locator('mixedIsolated=#left >> mixedMain=button')
  assert.equal(await forward.count(), 2)
  assert.equal(await reverse.count(), 2)
  assert.deepEqual(await forward.evaluateAll((nodes) => nodes.map((node) => node.id)), [
    'one',
    'two',
  ])
  assert.equal(
    await forward
      .first()
      .evaluate(
        (node) => node === document.querySelector('#one') && window.nativeSelectorMarker === 'main',
      ),
    true,
  )
  checks.push(
    'raw selector chains execute each factory in its real declared world in both directions',
  )
  assert.equal(
    await page.locator('mixedMain=button').and(page.locator('mixedIsolated=#one')).count(),
    1,
  )
  assert.deepEqual(
    await page
      .locator('mixedMain=#three')
      .or(page.locator('mixedIsolated=#one'))
      .evaluateAll((nodes) => nodes.map((node) => node.id)),
    ['one', 'three'],
  )
  checks.push(
    'native mixed-world intersection and union preserve actual node identity and document order',
  )
  assert.deepEqual(
    await page
      .locator('mixedMain=section')
      .locator(page.locator('mixedIsolated=button').first())
      .evaluateAll((nodes) => nodes.map((node) => node.id)),
    ['one', 'three'],
  )
  checks.push('nested locator queries preserve nth scope for each actual parent')
  assert.equal(
    await page
      .locator('mixedMain=section')
      .filter({ has: page.locator('mixedIsolated=button') })
      .count(),
    2,
  )
  assert.deepEqual(
    await page
      .locator('mixedMain=section')
      .filter({ hasNot: page.locator('mixedIsolated=button') })
      .evaluateAll((nodes) => nodes.map((node) => node.id)),
    ['empty'],
  )
  assert.deepEqual(
    await page
      .locator('mixedMain=section')
      .filter({
        has: page
          .locator('mixedIsolated=button')
          .filter({ has: page.locator('mixedMain=span'), hasText: 'Third' }),
      })
      .evaluateAll((nodes) => nodes.map((node) => node.id)),
    ['right'],
  )
  checks.push('native has and hasNot filters preserve candidates across nested realm switches')
  assert.deepEqual(
    await page
      .locator('*mixedMain=section >> mixedIsolated=#one')
      .evaluateAll((nodes) => nodes.map((node) => node.id)),
    ['left'],
  )
  checks.push(
    'captured ancestors retain actual backend identity when descendant selection crosses worlds',
  )
  const handle = await forward.first().elementHandle()
  assert(handle)
  try {
    assert.equal(await forward.first().evaluate((node, same) => node === same, handle), true)
  } finally {
    await handle.dispose()
  }
  const callbackHandle = await forward.first().evaluateHandle((node) => node)
  try {
    assert.equal(
      await callbackHandle.evaluate((node) => node === document.querySelector('#one')),
      true,
    )
  } finally {
    await callbackHandle.dispose()
  }
  checks.push('mixed plans return genuine retained native handles and accept real handle arguments')
  await forward.first().click()
  assert.equal(await page.evaluate(() => window.nativeSelectorClicked === true), true)
  await page.locator('mixedMain=#forms >> mixedIsolated=#account').fill('mixed worlds')
  assert.equal(await page.locator('#account').inputValue(), 'mixed worlds')
  checks.push(
    'native pointer and form actions inspect actual mixed-plan nodes in the utility world',
  )
  await assert.rejects(
    page.locator('mixedMain=#left >> mixedIsolated=][').count(),
    /selector|SyntaxError/u,
  )
  assert.equal(await reverse.count(), 2)
  checks.push('malformed atomic selector errors propagate and do not poison later plans')
  await page.setContent(
    `<div id=frames><iframe srcdoc="<main><button id=child>Child</button></main><script>window.nativeSelectorMarker='main'</script>"></iframe></div>`,
  )
  await page.evaluate(() => {
    window.nativeSelectorMarker = 'main'
  })
  const child = page
    .frameLocator('mixedMain=#frames >> mixedIsolated=iframe')
    .locator('mixedMain=main >> mixedIsolated=button')
  assert.equal(await child.count(), 1)
  assert.equal(
    await child.evaluate(
      (node) => node === document.querySelector('#child') && window.nativeSelectorMarker === 'main',
    ),
    true,
  )
  const childHandle = await child.elementHandle()
  assert(childHandle)
  try {
    assert.equal(
      await childHandle.evaluate((node) => node === document.querySelector('#child')),
      true,
    )
  } finally {
    await childHandle.dispose()
  }
  checks.push('mixed frame-owner and child-node plans preserve native frame and handle ownership')
  assert.equal(
    await page
      .frameLocator('mixedMain=#frames >> mixedIsolated=#absent')
      .locator('mixedMain=main >> mixedIsolated=button')
      .count(),
    0,
  )
  checks.push('missing mixed frame paths produce the native immediate zero count snapshot')
  const state = z
    .object({
      otherOperations: z.number(),
      evaluationGroups: z.number(),
      activeHandles: z.number(),
      pendingHandles: z.number(),
    })
    .parse(await source.runAutomation('diagnostics'))
  assert.deepEqual(state, {
    otherOperations: 0,
    evaluationGroups: 0,
    activeHandles: 0,
    pendingHandles: 0,
  })
  const report = {
    cefVersion: source.cefVersion,
    binarySha256: createHash('sha256')
      .update(await readFile(executablePath))
      .digest('hex'),
    checks,
    state,
  }
  await writeFile(`${directory}/report.json`, JSON.stringify(report, null, 2) + '\n')
  console.log(JSON.stringify(report, null, 2))
} finally {
  await source.close()
  await writeFile(`${directory}/native-stderr.log`, diagnostics)
}
