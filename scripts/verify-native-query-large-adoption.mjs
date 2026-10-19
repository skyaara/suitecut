// SPDX-License-Identifier: MIT
/* global document, Element */
import assert from 'node:assert/strict'
import { Buffer } from 'node:buffer'
import console from 'node:console'
import { mkdir, writeFile } from 'node:fs/promises'
import { performance } from 'node:perf_hooks'
import process from 'node:process'
import { setTimeout as delay } from 'node:timers/promises'

import * as z from 'zod'

import { createNativePage, launchNativeBrowser } from '../dist/native.js'

const directory = '.suitecut/native-query-large-adoption'
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
const schema = z.object({
  otherOperations: z.number(),
  activeHandles: z.number(),
  pendingHandles: z.number(),
  evaluationGroups: z.number(),
  nativeSelectorWaits: z.number(),
  nativeSelectorWaitPhysical: z.number(),
  pendingDevTools: z.number(),
  queuedDevTools: z.number(),
  queuedDevToolsBytes: z.number(),
})
const checks = []
try {
  const page = createNativePage(source)
  const nodes = 25000,
    originalDeadlineMs = 120000
  page.setDefaultTimeout(originalDeadlineMs)
  await page.setContent('<section id="scope"></section>')
  await page.evaluate((count) => {
    const fragment = document.createDocumentFragment()
    for (let i = 0; i < count; i++) {
      const node = document.createElement('span')
      node.className = 'row'
      node.id = 'row-' + i
      fragment.appendChild(node)
    }
    document.querySelector('#scope').appendChild(fragment)
    Element.prototype.matches = function () {
      return false
    }
    Reflect.set(globalThis, 'largeAdoptionAuthorMarker', true)
  }, nodes)
  const before = schema.parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(before)) assert.equal(value, 0, name)
  const samples = []
  let finished = false
  const began = performance.now()
  const pending = page
    .locator('span.row')
    .evaluateAll((selected) => {
      const native = document.querySelectorAll('span.row')
      if (
        selected.length !== native.length ||
        Reflect.get(globalThis, 'largeAdoptionAuthorMarker') !== true
      )
        throw new Error('Actual native count or author realm differs')
      const ids = []
      for (let i = 0; i < selected.length; i++) {
        if (selected[i] !== native[i] || selected[i].ownerDocument !== document)
          throw new Error('Actual ordered backend node identity differs at ' + i)
        ids.push(selected[i].id)
      }
      return ids
    })
    .then(
      (value) => {
        finished = true
        return { value }
      },
      (error) => {
        finished = true
        return {
          error:
            error instanceof Error
              ? error
              : new Error('Native large adoption failed', { cause: error }),
        }
      },
    )
  while (!finished) {
    assert(samples.length < 5000)
    samples.push({
      elapsedMs: performance.now() - began,
      ...schema.parse(await source.runAutomation('diagnostics')),
    })
    await delay(25)
  }
  const result = await pending,
    elapsedMs = performance.now() - began
  if ('error' in result) throw result.error
  const ids = z.array(z.string()).length(nodes).parse(result.value)
  assert.deepEqual(
    ids,
    Array.from({ length: nodes }, (_, i) => 'row-' + i),
  )
  assert(elapsedMs < originalDeadlineMs)
  checks.push(
    'all25000 actual wrappers match genuine browser querySelectorAll identities/order in the author callback despite author matches replacement; all25000 returned IDs match the independently generated fixture',
  )
  for (let attempt = 0; attempt < 500; attempt++) {
    const state = schema.parse(await source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) break
    await delay(5)
  }
  const afterSuccess = schema.parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(afterSuccess)) assert.equal(value, 0, name)
  checks.push(
    'the original120000ms deadline is retained throughout actual multi-batch preparation and recorded elapsed runtime, followed by real native physical/group/queue cleanup',
  )
  await page.evaluate(() => Reflect.set(globalThis, 'largeAdoptionLateCallback', false))
  page.setDefaultTimeout(1)
  const deadlineBegan = performance.now()
  await assert.rejects(
    page.locator('span.row').evaluateAll((selected) => {
      Reflect.set(globalThis, 'largeAdoptionLateCallback', true)
      return selected.length
    }),
    /Timeout|timed out/,
  )
  const deadlineElapsedMs = performance.now() - deadlineBegan
  assert(deadlineElapsedMs < 1500)
  page.setDefaultTimeout(originalDeadlineMs)
  for (let attempt = 0; attempt < 500; attempt++) {
    const state = schema.parse(await source.runAutomation('diagnostics'))
    if (Object.values(state).every((value) => value === 0)) break
    await delay(5)
  }
  assert.equal(
    await page.evaluate(() => Reflect.get(globalThis, 'largeAdoptionLateCallback') === false),
    true,
  )
  const final = schema.parse(await source.runAutomation('diagnostics'))
  for (const [name, value] of Object.entries(final)) assert.equal(value, 0, name)
  checks.push(
    'a separate25000-node request preserves its actual1ms deadline and never enters the author callback after cancellation; actual ACKs and all tracked physical/group/dispatch bytes drain',
  )
  const peak = {}
  for (const sample of samples)
    for (const [name, value] of Object.entries(sample))
      if (name !== 'elapsedMs')
        Reflect.set(peak, name, Math.max(Number(Reflect.get(peak, name) ?? 0), value))
  await writeFile(
    directory + '/report.json',
    JSON.stringify(
      {
        checks,
        nodes,
        originalDeadlineMs,
        elapsedMs,
        deadlineRequestedMs: 1,
        deadlineElapsedMs,
        returnedIdsUTF8Bytes: Buffer.byteLength(JSON.stringify(ids)),
        byteAccounting:
          'queuedDevToolsBytes is the actual native dispatch queue byte counter; renderer heap and remote-object heap are not measured',
        before,
        peak,
        afterSuccess,
        final,
        samples,
      },
      null,
      2,
    ) + '\n',
  )
  console.log(
    JSON.stringify({
      checks: checks.length,
      nodes,
      originalDeadlineMs,
      elapsedMs,
      deadlineElapsedMs,
      peak,
      final,
    }),
  )
} finally {
  await source.close()
  await writeFile(directory + '/native-stderr.log', stderr)
}
