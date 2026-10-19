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

const directory = resolve('.suitecut/native-selector-worlds')
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
  await page.setContent('<button id=save>Save</button><input id=account><section></section>')
  await page.evaluate(() => {
    window.nativeSelectorMarker = 'main'
    document.querySelector('#save').onclick = () => {
      window.nativeSelectorClicked = true
    }
    document.querySelector('section').innerHTML = Array.from(
      { length: 131 },
      (_, index) => `<span data-index="${index}">${index}</span>`,
    ).join('')
  })
  await selectors.register('worldMain', () => ({
    query(root, selector) {
      return root.querySelector(selector)
    },
    queryAll(root, selector) {
      if (window.nativeSelectorMarker !== 'main') throw new Error('Main selector world changed')
      return root.querySelectorAll(selector)
    },
  }))
  await selectors.register(
    'worldIsolated',
    () => ({
      query(root, selector) {
        return root.querySelector(selector)
      },
      queryAll(root, selector) {
        if (window.nativeSelectorMarker !== undefined)
          throw new Error('Isolated selector observed page JavaScript')
        return root.querySelectorAll(selector)
      },
    }),
    { contentScript: true },
  )
  const isolated = page.locator('worldIsolated=#save')
  assert.equal(await isolated.count(), 1)
  assert.equal(await page.locator('worldMain=#save').count(), 1)
  assert.equal(
    await page.locator('[data-selector="worldMain=none"] >> worldIsolated=button').count(),
    0,
  )
  checks.push('actual isolated and main selector execution; quoted prefixes do not choose a world')
  assert.equal(
    await isolated.evaluate(
      (element) =>
        element === document.querySelector('#save') &&
        element instanceof Element &&
        window.nativeSelectorMarker === 'main',
    ),
    true,
  )
  checks.push('isolated selection adopts the actual backend node for main-world locator evaluation')
  assert.deepEqual(
    await page.locator('worldIsolated=span').evaluateAll((elements) => ({
      count: elements.length,
      last: elements[130] === document.querySelector('[data-index="130"]'),
      main: window.nativeSelectorMarker === 'main',
    })),
    { count: 131, last: true, main: true },
  )
  checks.push('real node adoption crosses the native 128-node transfer batch boundary')
  const element = await isolated.elementHandle()
  assert(element)
  try {
    assert.equal(await element.evaluate((node) => node === document.querySelector('#save')), true)
    assert.equal(await isolated.evaluate((node, same) => node === same, element), true)
  } finally {
    await element.dispose()
  }
  const handle = await isolated.evaluateHandle((node) => node)
  try {
    assert.equal(await handle.evaluate((node) => node === document.querySelector('#save')), true)
  } finally {
    await handle.dispose()
  }
  const handles = await page.locator('worldIsolated=span').elementHandles()
  assert.equal(handles.length, 131)
  try {
    assert.equal(
      await handles[130].evaluate((node) => node === document.querySelector('[data-index="130"]')),
      true,
    )
  } finally {
    for (const value of handles) await value.dispose()
  }
  checks.push(
    'native element, element-array, and callback-returned handles retain actual main-world nodes',
  )
  await isolated.click()
  assert.equal(await page.evaluate(() => window.nativeSelectorClicked === true), true)
  await page.locator('worldMain=#save').click()
  await page.locator('worldMain=#account').fill('main')
  assert.equal(await page.locator('#account').inputValue(), 'main')
  await page.locator('worldIsolated=#account').fill('isolated')
  assert.equal(await page.locator('#account').inputValue(), 'isolated')
  checks.push(
    'native pointer and form actions preserve the chosen selector world and adopt real controls',
  )
  assert.equal(await page.locator('worldMain=section >> worldIsolated=span').count(), 131)
  checks.push('mixed custom selector factories execute their actual declared worlds')
  await page.setContent(
    '<iframe srcdoc="<button id=child>Child</button><script>window.nativeSelectorMarker=\'main\'</script>"></iframe>',
  )
  await page.evaluate(() => {
    window.nativeSelectorMarker = 'main'
  })
  const child = page.frameLocator('worldIsolated=iframe').locator('worldIsolated=button')
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
  checks.push('isolated frame-path resolution and returned handles stay in the actual child frame')
  const waiting = page.locator('worldIsolated=#later').evaluateHandle((node) => node)
  await page.waitForTimeout(40)
  await page.evaluate(() => {
    const later = document.createElement('button')
    later.id = 'later'
    document.body.append(later)
  })
  const waitingHandle = await waiting
  try {
    assert.equal(
      await waitingHandle.evaluate((node) => node === document.querySelector('#later')),
      true,
    )
  } finally {
    await waitingHandle.dispose()
  }
  checks.push(
    'missing-node callback handles rerun isolated selection under the original native deadline',
  )
  await page.goto('data:text/html,<button id=save>After navigation</button>')
  await page.evaluate(() => {
    window.nativeSelectorMarker = 'main'
  })
  assert.equal(await isolated.count(), 1)
  assert.equal(
    await isolated.evaluate(
      (node) => node === document.querySelector('#save') && window.nativeSelectorMarker === 'main',
    ),
    true,
  )
  checks.push('world selection and adoption use fresh document contexts after navigation')
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
