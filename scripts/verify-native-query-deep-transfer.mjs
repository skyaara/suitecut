// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = '.suitecut/native-query-deep-transfer'
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
try {
  const page = createNativePage(source)
  await page.setContent('<section id=root></section>')
  await page.evaluate(() => {
    const root = document.querySelector('#root')
    for (let i = 0; i < 257; i++) {
      const node = document.createElement('b')
      node.id = 'deep-' + i
      root.appendChild(node)
    }
    Element.prototype.matches = function () {
      return false
    }
    Reflect.set(globalThis, 'deepTransferAuthorMarker', true)
  })
  assert.deepEqual(
    await page.$$eval('b', (nodes) => {
      const expected = document.querySelectorAll('b')
      return nodes.map((node, index) => {
        if (
          node !== expected[index] ||
          node.ownerDocument !== document ||
          Reflect.get(globalThis, 'deepTransferAuthorMarker') !== true
        )
          throw new Error('Actual backend identity/order or author callback realm differs')
        return node.id
      })
    }),
    Array.from({ length: 257 }, (_, i) => 'deep-' + i),
  )
  checks.push(
    'actual large utility selection crosses128-node adoption batches with every ordered main wrapper equal to the genuine native DOM node',
  )
  await page.evaluate(() => {
    const root = document.querySelector('#root')
    root.innerHTML = ''
    for (let i = 0; i < 129; i++) {
      const node = document.createElementNS('http://www.w3.org/2000/svg', 'g')
      node.id = 'actual\ud800-' + i
      node.setAttribute('class', 'class\udfff')
      node.setAttribute('data-exact', 'value\ud800\0\\"😀')
      node.setAttribute('__suitecutNativeUTF16Scalar', 'd800')
      root.appendChild(node)
    }
  })
  const result = await page.$$eval('g', (nodes) => {
    const expected = document.querySelectorAll('g')
    return nodes.map((node, index) => {
      if (node !== expected[index] || node.ownerDocument !== document)
        throw new Error('Actual UTF16 SVG node identity differs')
      return {
        id: node.id,
        value: node.getAttribute('data-exact'),
        attributes: {
          __suitecutNativeUTF16Scalar: node.getAttribute('__suitecutNativeUTF16Scalar'),
        },
      }
    })
  })
  assert.deepEqual(
    result,
    Array.from({ length: 129 }, (_, i) => ({
      id: 'actual\ud800-' + i,
      value: 'value\ud800\0\\"😀',
      attributes: { __suitecutNativeUTF16Scalar: 'd800' },
    })),
  )
  checks.push(
    'actual native SVG metadata preserves isolated UTF16 DOM values and author scalar-tag attribute dictionaries without interpreting them as private captions',
  )
  assert.equal(
    await page
      .locator('g')
      .evaluateAll(
        (nodes) =>
          nodes.length === 129 && nodes.every((node) => node === document.getElementById(node.id)),
      ),
    true,
  )
  checks.push(
    'repeated genuine large-node preparation preserves actual identity after lossless metadata transport',
  )
  const schema = z.object({
    otherOperations: z.number(),
    activeHandles: z.number(),
    pendingHandles: z.number(),
    evaluationGroups: z.number(),
    nativeSelectorWaits: z.number(),
    nativeSelectorWaitPhysical: z.number(),
  })
  for (let attempt = 0; attempt < 200; attempt++) {
    const state = schema.parse(await source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) break
    await delay(5)
  }
  const state = schema.parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(state)) assert.equal(value, 0, name)
  checks.push(
    'all actual large-array node preparation groups and physical requests retire after callback delivery',
  )
  await writeFile(directory + '/report.json', JSON.stringify({ checks, state }, null, 2) + '\n')
  console.log(JSON.stringify({ checks: checks.length, state }))
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
assert.equal(stderr, '', 'Native deep transfer diagnostics must remain empty')
